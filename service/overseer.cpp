#include <iostream>
#include "overseer.h"

#include "../ipc/transport.h"
#include "message_builder.h"
#include "job_queue.h"
#include "message_dispatcher.h"
#include "rating_announcer.h"
#include "rating_calculator.h"
#include "worker_pool.h"

// --------------------------------------------------------------------- //
/*
 *  Helper values and types
 */
// --------------------------------------------------------------------- //

static constexpr int workerPoolConcurrency {2};

struct PluggableInfrastructure {
    PluggableInfrastructure (CoreRatingData& coreData, CoreDataSyncBlock& syncBlock, IterationData& iterationData);

    // order of fields matters, the ones below often depend on the ones above

    ServerIpcTransport transport;
    JobQueue jobQueue;

    MessageBattery messageBattery;
    MessageBuilder messageBuilder;
    IncomingDataDoubleBuffer incomingData;
    MessageDispatcher messageDispatcher;

    RatingAnnouncer ratingAnnouncer;
    WorkerPool workerPool;
};

PluggableInfrastructure::PluggableInfrastructure (CoreRatingData& coreData, CoreDataSyncBlock& syncBlock,
                                                  IterationData& iterationData)
: transport(std::make_unique<Spinlock>())
, jobQueue {workerPoolConcurrency}
, messageBuilder {messageBattery, transport}
, messageDispatcher {jobQueue, incomingData.buffers[incomingData.currentBufferIndex]}
, ratingAnnouncer {iterationData, jobQueue,
                   std::make_unique<RatingCalculator>(coreData, syncBlock, iterationData, incomingData, jobQueue),
                   syncBlock.stopSignals, coreData.expirationDate}
, workerPool {coreData.activeUsers, coreData.rating, syncBlock, transport} {
    // whew, that was a long initialization list...
    // the complexity is to ensure that each object has access only to the data it actually requires - and nothing more
}

// --------------------------------------------------------------------- //
/*
 *  Overseer class methods
 */
// --------------------------------------------------------------------- //

Overseer::Overseer () {}
Overseer::~Overseer () {}

void Overseer::run (unsigned short portNumberToBindTo) {
    using ClientMessageCode = IpcProto::ProtocolConstants::ClientMessageCode;

    for (;;) {
        try {
            // initializing the service internal modules
            m_pluggable = std::make_unique<PluggableInfrastructure>(m_coreData, m_syncBlock, m_iterationData);

            // launching the transport system
            // if that succeeds, we're having a working protocol-level connection to (some) client
            m_pluggable->transport.launch(portNumberToBindTo);

            // claiming the current incoming data buffer as in use
            IncomingDataBuffer* inData = m_pluggable->incomingData.currentBuffer.load(std::memory_order_relaxed);
            inData->bufferWriterCount.fetch_add(1, std::memory_order_relaxed);

            // launching the async processing
            m_pluggable->ratingAnnouncer.start();
            m_pluggable->workerPool.start(m_pluggable->jobQueue);

            MessageBattery& b = m_pluggable->messageBattery;
            MessageDispatcher& md = m_pluggable->messageDispatcher;

            while (!m_syncBlock.stopSignals.badFlag.load(std::memory_order_relaxed)) {
                ClientMessageCode c = m_pluggable->messageBuilder.build();

                {
                    // release sequence end: recalculator thread -> message dispatcher thread
                    IncomingDataBuffer* newBuffer = m_pluggable->incomingData.currentBuffer.load(std::memory_order_acquire);

                    if (newBuffer != inData) {
                        // release sequence start: message dispatcher thread -> recalculator thread
                        inData->bufferWriterCount.fetch_sub(1, std::memory_order_release);
                        inData = newBuffer;
                        inData->bufferWriterCount.fetch_add(1, std::memory_order_relaxed);

                        m_pluggable->messageDispatcher.setBuffer(*inData);
                    }
                }

                switch (c) {
                case ClientMessageCode::USER_REGISTERED: md.dispatch(b.userRegisteredMsg); break;
                case ClientMessageCode::USER_RENAMED: md.dispatch(b.userRenamedMsg); break;
                case ClientMessageCode::USER_CONNECTED: md.dispatch(b.userConnectedMsg); break;
                case ClientMessageCode::USER_DISCONNECTED: md.dispatch(b.userDisconnectedMsg); break;
                case ClientMessageCode::USER_DEAL_WON: md.dispatch(b.userDealWonMsg); break;
                default: assert(false);
                }
            }
        } catch (const transport_error_recoverable& e) {
            std::cerr << "Overseer exception: recoverable transport error" << std::endl;

            m_syncBlock.stopSignals.signalError(false);
        } catch (const std::exception& e) {
            std::cerr << "Overseer exception: " << e.what() << std::endl;

            m_syncBlock.stopSignals.signalError();
        } catch (...) {
            std::cerr << "Unknown overseer exception" << std::endl;

            m_syncBlock.stopSignals.signalError();
        }

        // cleanup and waiting on the async tasks to stop
        m_pluggable.reset(nullptr);

        if (m_syncBlock.stopSignals.unrecoverableError.load(std::memory_order_relaxed)) {
            std::cerr << "Can't recover from the error, terminating the service..." << std::endl;

            return;
        }

        std::cerr << "Attempting recovery..." << std::endl;
    }
}
