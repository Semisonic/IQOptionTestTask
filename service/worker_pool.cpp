#include <iostream>

#include "worker_pool.h"
#include "../ipc/protocol.h"

// --------------------------------------------------------------------- //
/*
 *  WorkerPool methods
 */
// --------------------------------------------------------------------- //

WorkerPool::WorkerPool (const CoreRatingData& coreData, CoreDataSyncBlock& syncBlock,
                        ServerIpcTransport& transport)
    : m_coreData(coreData), m_syncBlock(syncBlock), m_transport(transport) {}

// --------------------------------------------------------------------- //

WorkerPool::~WorkerPool () {
    for (auto& workerHandle : m_workerHandles) {
        try {
            if (workerHandle.valid()) {
                workerHandle.get();
            }
        } catch (const std::exception& e) {
            std::cerr << "Worker pool exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Worker pool exception: unknown exception" << std::endl;
        }
    }
}

// --------------------------------------------------------------------- //

void WorkerPool::start (JobQueue &m_jobQueue) {
    auto concurrencyFactor = m_jobQueue.concurrencyFactor();

    m_syncBlock.dataReaderCount.store(concurrencyFactor, std::memory_order_relaxed);
    m_workerHandles.reserve(concurrencyFactor);

    for (auto i = 0; i < concurrencyFactor; ++i) {
        m_workerHandles.push_back(std::async(std::launch::async, &WorkerPool::doWork, this, m_jobQueue.getConsumer(i)));
    }
}

// --------------------------------------------------------------------- //

struct RatingBufferData {
    RatingBufferData (BinaryOStream&& b) : buffer{std::move(b)}, base{buffer.getPos()} {}

    BinaryOStream buffer;
    BinaryOStream::pos_t base;
    BinaryOStream::pos_t topRatingsEnd {0};
};

void WorkerPool::doWork (JobQueue::QueueConsumer&& consumer) {
    try {
        RatingBufferData ratingBuffer {m_transport.createAdaptedRatingBuffer()};
        BinaryOStream errorBuffer {m_transport.createAdaptedErrorBuffer()};
        auto errorBufferBase = errorBuffer.getPos();

        cacheTopRatings(ratingBuffer);

        for (;;) {
            auto newJobs = false;

            if (m_syncBlock.stopSignals.badFlag.load(std::memory_order_relaxed)) {
                break;
            }

            if (m_syncBlock.refreshInProgress.load(std::memory_order_relaxed)) {
                depleteUserDataMessages(ratingBuffer, consumer);

                {
                    std::unique_lock<std::mutex> lock(m_syncBlock.dataLock);

                    m_syncBlock.dataReaderCount.fetch_sub(1, std::memory_order_relaxed);
                    m_syncBlock.dataRefreshedTrigger.wait(lock, [this]()->bool{
                        return !m_syncBlock.refreshInProgress.load(std::memory_order_relaxed);
                    });
                }

                m_syncBlock.dataReaderCount.fetch_add(1, std::memory_order_relaxed);

                cacheTopRatings(ratingBuffer);
            }

            // process error queue first
            {
                ErrorPtr error {};

                while ((error = consumer.dequeueError())) {
                    processError(errorBuffer, errorBufferBase, error);

                    newJobs = true;
                }
            }

            // process id-based rating queue
            {
                UserIdPromise userIdPromise {UserDataConstants::invalidId, false};

                while ((userIdPromise = consumer.dequeueUserIdPromise()).first != UserDataConstants::invalidId) {
                    if (!processRating(ratingBuffer, userIdPromise)) {
                        ErrorPtr error {new IpcProto::UserUnrecognizedError {userIdPromise.first}};

                        processError(errorBuffer, errorBufferBase, error);
                    }

                    newJobs = true;
                }
            }

            auto someUserDataMessagesProcessed = depleteUserDataMessages(ratingBuffer, consumer);
            newJobs = newJobs || someUserDataMessagesProcessed;

            if (!newJobs) {
                // thread has nothing to do, letting other threads try
                //std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
        }
    } catch (const transport_error_recoverable&) {
        m_syncBlock.stopSignals.signalError(false);
        m_syncBlock.dataReaderCount.fetch_sub(1, std::memory_order_relaxed);

        throw;
    } catch (...) {
        m_syncBlock.stopSignals.signalError();
        m_syncBlock.dataReaderCount.fetch_sub(1, std::memory_order_relaxed);

        throw;
    }
}

// --------------------------------------------------------------------- //

bool WorkerPool::depleteUserDataMessages (RatingBufferData& bufferData, JobQueue::QueueConsumer &consumer) {
    const FullUserData* userData = nullptr;
    auto newJobs {false};

    while ((userData = consumer.dequeueUserData()) != nullptr) {
        processRating(bufferData, userData);

        newJobs = true;
    }

    return newJobs;
}

// --------------------------------------------------------------------- //

void WorkerPool::processRating (RatingBufferData& bufferData, const FullUserData* userData) {
    processRatingImpl(bufferData, userData->id, userData->rating);
}

// --------------------------------------------------------------------- //

bool WorkerPool::processRating (RatingBufferData& bufferData, UserIdPromise userIdPromise) {
    auto activeUser = m_coreData.activeUsers.find(userIdPromise.first);

    if (activeUser != m_coreData.activeUsers.end()) {
        processRatingImpl(bufferData, activeUser->second->id, activeUser->second->rating);

        return true;
    }

    if (m_coreData.silentUsers.find(userIdPromise.first) != m_coreData.silentUsers.end() || userIdPromise.second) {
        // user is not in the rating, giving him the "one past the last" place
        processRatingImpl(bufferData, userIdPromise.first, static_cast<int>(m_coreData.rating.size()));

        return true;
    }

    return false;
}

// --------------------------------------------------------------------- //

void WorkerPool::processError (BinaryOStream& buffer, BinaryOStream::pos_t pos, const ErrorPtr& error) {
    error->store(buffer);

    m_transport.blockedWriteMessage(buffer);

    buffer.rewind(pos);
}

// --------------------------------------------------------------------- //

void WorkerPool::cacheTopRatings (RatingBufferData& bufferData) {
    constexpr auto& topPositions = IpcProto::ProtocolConstants::RatingDimensions::topPositions;
    using StorageBuilder = IpcProto::RatingPackMessage::StorageBuilder;

    bufferData.buffer.rewind(bufferData.base);

    StorageBuilder::storePackHeader(bufferData.buffer, UserDataConstants::invalidId, 0, 0);
    auto maxTopRating = std::min(topPositions, static_cast<int>(m_coreData.rating.size()));

    for (auto i = 0; i < maxTopRating; ++i) {
        auto userData = m_coreData.rating[i];

        StorageBuilder::storePackEntry(bufferData.buffer, userData->id, userData->amountWon
#ifdef PASS_NAMES_AROUND
                                       , userData->name
#endif
                                      );
    }

    bufferData.topRatingsEnd = bufferData.buffer.getPos();
}

// --------------------------------------------------------------------- //

void WorkerPool::processRatingImpl (RatingBufferData& bufferData, id_t id, int rating) {
    constexpr auto& topPositions = IpcProto::ProtocolConstants::RatingDimensions::topPositions;
    constexpr auto& competitionDistance = IpcProto::ProtocolConstants::RatingDimensions::competitionDistance;
    using StorageBuilder = IpcProto::RatingPackMessage::StorageBuilder;

    assert(rating <= m_coreData.rating.size());
    assert(bufferData.buffer.getPos() == bufferData.topRatingsEnd);

    auto ratingRangeBegin = std::max(topPositions, rating - competitionDistance); // that's an element index
    auto ratingRangeEnd = std::min(static_cast<int>(m_coreData.rating.size()), rating + competitionDistance + 1);

    for (auto i = ratingRangeBegin; i < ratingRangeEnd; ++i) {
        auto userData = m_coreData.rating[i];

        StorageBuilder::storePackEntry(bufferData.buffer, userData->id, userData->amountWon
#ifdef PASS_NAMES_AROUND
                                       , userData->name
#endif
                                       );
    }

    bufferData.buffer.setPos(bufferData.base);
    StorageBuilder::storePackHeader(bufferData.buffer, id, static_cast<int>(m_coreData.rating.size()), rating);

    m_transport.blockedWriteMessage(bufferData.buffer);

    // buffer must be restored to the "top ratings only" state, otherwise cache will be broken
    bufferData.buffer.rewind(bufferData.topRatingsEnd);
}