#ifndef IQOPTIONTESTTASK_WORKER_POOL_H
#define IQOPTIONTESTTASK_WORKER_POOL_H

#include <future>
#include <vector>

#include "core_data.h"
#include "job_queue.h"
#include "../ipc/transport.h"

struct RatingBufferData;

class WorkerPool {
public:

    WorkerPool (const CoreRatingData& coreData, CoreDataSyncBlock& syncBlock,
                ServerIpcTransport& transport);
    ~WorkerPool ();

    void start (JobQueue& m_jobQueue);

private:

    void doWork (JobQueue::QueueConsumer&& consumer);

    bool depleteUserDataMessages (RatingBufferData& bufferData, JobQueue::QueueConsumer& consumer);

    void processRating (RatingBufferData& bufferData, const FullUserData* userData);
    bool processRating (RatingBufferData& bufferData, UserIdPromise userIdPromise);
    void processError (BinaryOStream& buffer, BinaryOStream::pos_t pos, const ErrorPtr& error);

    void cacheTopRatings (RatingBufferData& bufferData);

    void processRatingImpl (RatingBufferData& bufferData, id_t id, int rating);

private:

    const CoreRatingData& m_coreData;
    CoreDataSyncBlock& m_syncBlock;
    ServerIpcTransport& m_transport;

    std::vector<std::future<void>> m_workerHandles;
};

#endif //IQOPTIONTESTTASK_WORKER_POOL_H
