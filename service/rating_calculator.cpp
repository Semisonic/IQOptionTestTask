#include <cassert>
#include <algorithm>
#include <cstring>
#include "rating_calculator.h"
#include "core_data.h"
#include "job_queue.h"

// --------------------------------------------------------------------- //
/*
 *  Helper classes
 */
// --------------------------------------------------------------------- //

enum class RatingChangeType {
    NewPosition = 0,
    OldPosition = 1
};

struct RatingPatchEntry {
    RatingPatchEntry (int ea) : elementsAfter { ea } {}
    RatingPatchEntry (FullUserData* ud, int ea, RatingChangeType ct, monetary_t aw)
            : userData(ud), elementsAfter(ea), changeType(ct), amountWon(aw) {}

    FullUserData* userData { nullptr };
    int elementsAfter;
    RatingChangeType changeType { RatingChangeType::OldPosition };

    bool operator< (const RatingPatchEntry& right) const {
        return (elementsAfter != right.elementsAfter)
                ? elementsAfter < right.elementsAfter
                : ((changeType != right.changeType)
                    ? changeType < right.changeType
                    : amountWon < right.amountWon);
    }

private:

    monetary_t amountWon { 0 };
};

using RatingPatchSet = std::multiset<RatingPatchEntry>;

// --------------------------------------------------------------------- //
/*
 *  RatingCalculatorImpl class
 */
// --------------------------------------------------------------------- //

class RatingCalculatorImpl {
public:

    RatingCalculatorImpl (CoreRatingData& ud, IterationData& id, IncomingDataBuffer& ib, JobQueue& jq)
    : m_userData(ud), m_iterationData(id), m_incomingBuffer(ib), m_jobQueue(jq) {}

    void recalculate (bool dropOldRating);

private:

    void dropRating ();

    void processRegistrations ();
    void processRenames ();
    void processConnectionChanges ();
    void processDeals ();

private:

    bool userExists (id_t userId) {
        return (m_userData.silentUsers.find(userId) != m_userData.silentUsers.end() ||
                m_userData.activeUsers.find(userId) != m_userData.activeUsers.end());
    }

private:

    // rating utility tools

    int ratingElementsAfter (monetary_t winnings) const {
        auto comp = [](const monetary_t& left, const FullUserData*const& right) { return left > right->amountWon; };
        auto pos = std::upper_bound(m_userData.rating.begin(), m_userData.rating.end(), winnings, comp);

        return static_cast<int>(std::distance(pos, m_userData.rating.end()));
    }

    void ratingMoveBlock (int pos, int length, int offset) {
        if (length == 0 || offset == 0) return;

        assert(pos < m_userData.rating.size() &&
               length > 0 &&
               offset > 0 &&
               pos + length + offset < m_userData.rating.size());

        FullUserData** source = m_userData.rating.data() + pos;
        FullUserData** dest = source + offset;

        std::memmove(dest, source, sizeof(FullUserData*) * length);
    }

    void ratingInsert (int pos, FullUserData* value) {
        assert(pos < m_userData.rating.size());

        m_userData.rating[pos] = value;
    }

    void ratingRefreshPositions () {
        for (int pos = 0; pos < m_userData.rating.size(); ++pos) {
            m_userData.rating[pos]->rating = pos;
        }
    }

private:

    CoreRatingData& m_userData;
    IterationData& m_iterationData;
    IncomingDataBuffer& m_incomingBuffer;
    JobQueue& m_jobQueue;

    RatingPatchSet m_ratingPatches;
    int m_freshRatings { 0 };
};

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::recalculate (bool dropOldRating) {
    if (dropOldRating) {
        dropRating();
    }

    processRegistrations();
    processRenames();
    processConnectionChanges();
    processDeals();

    int oldRatingVectorSize = static_cast<int>(m_userData.rating.size());
    auto offset = m_freshRatings;

    m_userData.rating.resize(static_cast<RatingVector::size_type>(oldRatingVectorSize + offset));

    auto oldPositions = 0;
    auto lengthDone = 0;

    for (auto& ratingPatch : m_ratingPatches) {
        int blockLength = 0;

        if (ratingPatch.changeType == RatingChangeType::OldPosition) {
            blockLength = ratingPatch.elementsAfter - lengthDone - oldPositions;
            ++oldPositions;
            lengthDone += blockLength;

            ratingMoveBlock(oldRatingVectorSize - ratingPatch.elementsAfter + 1, blockLength, offset);
            ++offset;
        } else if (ratingPatch.changeType == RatingChangeType::NewPosition) {
            blockLength = ratingPatch.elementsAfter - lengthDone - oldPositions;
            lengthDone += blockLength;

            int position = oldRatingVectorSize - ratingPatch.elementsAfter;

            ratingMoveBlock(position, blockLength, offset);
            --offset;
            ratingInsert(position + offset, ratingPatch.userData);
        }
    }

    ratingRefreshPositions();
}

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::dropRating () {
    for (auto& activeUser : m_userData.activeUsers) {
        m_userData.silentUsers.insert(std::make_pair(activeUser.first, BasicUserData(std::move(*activeUser.second.get()))));
    }

    m_userData.activeUsers.clear();

    for (auto& chronoSet : m_iterationData.usersOnline) {
        chronoSet.clear();
    }

    m_userData.rating.clear();
}

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::processRegistrations () {
    auto freshDeal = m_incomingBuffer.dealsWon.end();

    for (auto& newReg : m_incomingBuffer.usersRegistered) {
        BasicUserData newSilentUser;
        id_t userId = UserDataConstants::invalidId;

#ifdef PASS_NAMES_AROUND
        userId = newReg.first;
#else
        userId = newReg;
#endif
        if (userExists(userId)) {
            // protocol error, trying to register a user already registered
            ErrorPtr error {new IpcProto::MultipleRegistrationError {userId}};
            m_jobQueue.enqueueErrorJob(std::move(error));

            continue;
        }

#ifdef PASS_NAMES_AROUND
        newSilentUser.name = std::move(newReg.second);
#endif
        m_userData.silentUsers.insert(std::make_pair(userId, std::move(newSilentUser)));
    }

    m_incomingBuffer.usersRegistered.clear();
}

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::processRenames () {
#ifdef PASS_NAMES_AROUND
    for (auto& newName : m_incomingBuffer.usersRenamed) {
        auto activeUser = m_userData.activeUsers.find(newName.first);

        if (activeUser != m_userData.activeUsers.end()) {
            activeUser.second->name = std::move(newName.second);

            continue;
        }

        auto silentUser = m_userData.silentUsers.find(newName.first);

        if (silentUser != m_userData.silentUsers.end()) {
            silentUser->name = std::move(newName.second);

            continue;
        }

        // protocol error, trying to rename a user not previously registered
        m_jobQueue.enqueueError(USER_UNRECOGNIZED, newName.first);
    }

    m_incomingBuffer.usersRenamed.clear();
#endif
}

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::processConnectionChanges () {
    for (auto& connChange : m_incomingBuffer.connectionChanges) {
        assert(connChange.second < 60 || connChange.second == UserDataConstants::invalidSecond);

        auto activeUser = m_userData.activeUsers.find(connChange.first);

        if (activeUser != m_userData.activeUsers.end()) {
            auto& second = activeUser->second->secondConnected;

            if (second < 60) {
                // user was connected before, removing old record
                m_iterationData.usersOnline[second].erase(activeUser->second.get());
            }

            // modifying the user's connection status
            second = connChange.second;

            if (second < 60) {
                // user reconnected back, putting him where he belongs
                m_iterationData.usersOnline[second].insert(activeUser->second.get());
            }

            continue;
        }

        auto silentUser = m_userData.silentUsers.find(connChange.first);

        if (silentUser != m_userData.silentUsers.end()) {
            silentUser->second.secondConnected = connChange.second;

            continue;
        }

        // protocol error, trying to (dis)connect a user not previously registered
        ErrorPtr error {new IpcProto::UserUnrecognizedError {connChange.first}};
        m_jobQueue.enqueueErrorJob(std::move(error));
    }

    m_incomingBuffer.connectionChanges.clear();
}

// --------------------------------------------------------------------- //

void RatingCalculatorImpl::processDeals () {
    for (auto& newDeal : m_incomingBuffer.dealsWon) {
        auto activeUser = m_userData.activeUsers.find(newDeal.first);

        if (activeUser != m_userData.activeUsers.end()) {
            // user had rating before

            auto userProfile = activeUser->second.get();

            m_ratingPatches.insert(RatingPatchEntry(static_cast<int>(m_userData.rating.size()) - userProfile->rating - 1));

            userProfile->amountWon += newDeal.second;

            m_ratingPatches.insert(RatingPatchEntry(userProfile,
                                                    ratingElementsAfter(userProfile->amountWon),
                                                    RatingChangeType::NewPosition,
                                                    userProfile->amountWon));
            continue;
        }

        auto silentUser = m_userData.silentUsers.find(newDeal.first);

        if (silentUser != m_userData.silentUsers.end()) {
            // user had no rating previously

            std::unique_ptr<FullUserData> userProfile { new FullUserData(newDeal.first, newDeal.second,
                                                                         std::move(silentUser->second)) };

            if (userProfile->secondConnected != UserDataConstants::invalidSecond) {
                // user is connected, should put him onto the announcement list
                m_iterationData.usersOnline[userProfile->secondConnected].insert(userProfile.get());
            }

            ++m_freshRatings;

            m_ratingPatches.insert(RatingPatchEntry(userProfile.get(),
                                                    ratingElementsAfter(userProfile->amountWon),
                                                    RatingChangeType::NewPosition,
                                                    userProfile->amountWon));
            m_userData.activeUsers.insert(std::make_pair(newDeal.first, std::move(userProfile)));
            m_userData.silentUsers.erase(silentUser);
        }

        // protocol error, trying to process a deal on a user not previously registered
        ErrorPtr error {new IpcProto::UserUnrecognizedError {newDeal.first}};
        m_jobQueue.enqueueErrorJob(std::move(error));
    }
}

// --------------------------------------------------------------------- //
/*
 *  RatingCalculator methods
 */
// --------------------------------------------------------------------- //

RatingCalculator::RatingCalculator (CoreRatingData& userData, CoreDataSyncBlock& coreSync,
                                    IterationData& iterationData, IncomingDataDoubleBuffer& incomingData,
                                    JobQueue& jobQueue)
: m_userData(userData), m_coreSync(coreSync)
, m_iterationData(iterationData) , m_incomingData(incomingData)
, m_jobQueue(jobQueue) {
}

void RatingCalculator::recalculate (bool dropOldRating) {
    // switch incoming data buffers
    // release sequence start: recalculator thread -> message dispatcher thread
    IncomingDataBuffer* inData = m_incomingData.currentBuffer.exchange(&(m_incomingData.buffers[1-m_incomingData.currentBufferIndex]),
                                                                        std::memory_order_release);
    m_incomingData.currentBufferIndex = 1 - m_incomingData.currentBufferIndex;

    // put worker threads to sleep
    // no release sequence required: just telling the worker threads to proceed to waiting
    m_coreSync.refreshInProgress.store(true, std::memory_order_relaxed);

    // wait till the buffer is out of use
    // release sequence end: message dispatcher thread -> recalculator thread
    while (inData->bufferWriterCount.load(std::memory_order_acquire));

    // wait till the worker threads are asleep
    // no release sequence required: worker threads don't modify any shared data
    while (m_coreSync.dataReaderCount.load(std::memory_order_relaxed));

    RatingCalculatorImpl impl(m_userData, m_iterationData, *inData, m_jobQueue);

    impl.recalculate(dropOldRating);

    {
        std::lock_guard lg(m_coreSync.dataLock);

        // protection against a theoretically possible (albeit hardly feasible) race condition
        // when a worker thread decrements the reader counter but doesn't proceed to waiting yet
        m_coreSync.refreshInProgress.store(false, std::memory_order_relaxed);
    }

    m_coreSync.dataRefreshedTrigger.notify_all();
}
