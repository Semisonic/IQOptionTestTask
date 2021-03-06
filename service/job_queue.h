#ifndef IQOPTIONTESTTASK_JOB_QUEUE_H
#define IQOPTIONTESTTASK_JOB_QUEUE_H

#include <memory>
#include <variant>
#include <cassert>

#include "core_data.h"

using ErrorPtr = std::unique_ptr<IpcProto::GenericProtocolError>;

// --------------------------------------------------------------------- //
/*
 *  JobQueue class
 */
// --------------------------------------------------------------------- //

struct QueuePack;
using UserIdPromise = std::pair<id_t, bool>;

class JobQueue {

    class Impl;

public:

    class QueueConsumer {

        friend class JobQueue;

    public:

        QueueConsumer (const QueueConsumer&) = delete;
        QueueConsumer (QueueConsumer&&) = default;

        ErrorPtr dequeueError ();
        UserIdPromise dequeueUserIdPromise ();
        const FullUserData* dequeueUserData ();

    private:

        QueueConsumer (QueuePack& queuePack);

    private:

        QueuePack& m_queuePack;
    };

public:

    JobQueue (int concurrencyFactor);
    ~JobQueue ();

    int concurrencyFactor () const { return m_concurrencyFactor; }

    // push methods

    void enqueueErrorJob (ErrorPtr&& error);
    void enqueueRatingJob (UserIdPromise userIdPromise);
    void enqueueRatingJob (const FullUserData* userData);

    // pop methods

    QueueConsumer getConsumer (int concurrencyIndex);

private:

    int m_concurrencyFactor;
    std::unique_ptr<Impl> m_impl;
};

#endif //IQOPTIONTESTTASK_JOB_QUEUE_H
