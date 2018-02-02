#include <atomic>
#include <memory>
#include <vector>
#include "job_queue.h"
#include "core_data.h"

// --------------------------------------------------------------------- //
/*
 *  MPSCQueue class
 *
 *  Non-intrusive MPSC queue based on DV-MPSC algorithm, as per https://int08h.com/post/ode-to-a-vyukov-queue/
 */
// --------------------------------------------------------------------- //

template <typename T>
class MPSCQueue {

    struct Node {
        Node () = default;
        Node (T&& v) : value{std::move(v)} {}
        ~Node () {
            delete next.exchange(nullptr, std::memory_order_relaxed);
        }

        std::atomic<Node*> next;
//        std::unique_ptr<T> value;
        T value;
    };

public:

    MPSCQueue () : stub{new Node()}, head{stub.get()}, tail{stub.get()} {
        stub->next.store(nullptr);
    }

    void push (T&& newVal) {
        Node* newNode {new Node{std::move(newVal)}};

        newNode->next.store(nullptr, std::memory_order_relaxed);
        Node* prev = tail.exchange(newNode, std::memory_order_acq_rel);
        prev->next.store(newNode, std::memory_order_release);
    }

    bool tryPop (T& val) {
        std::unique_ptr<Node> head_copy {head.load(std::memory_order_relaxed)};
        Node* next = head_copy->next.load(std::memory_order_acquire);

        if (next != nullptr) {
            head.store(next, std::memory_order_relaxed);
            val = std::move(next->value);

            return true;
        }

        return false;
    }

private:

    std::unique_ptr<Node> stub;
    std::atomic<Node*> head;
    std::atomic<Node*> tail;
};

// --------------------------------------------------------------------- //
/*
 *  JobQueue::Impl class
 *
 *  where all actual queue fun takes place
 */
// --------------------------------------------------------------------- //

struct QueuePack {
    MPSCQueue<ErrorPtr> errorQueue;
    MPSCQueue<id_t> userIdQueue;
    MPSCQueue<const FullUserData*> userDataQueue;
};

class JobQueue::Impl {
public:

    Impl (size_t concurrencyFactor)
    : m_queues(concurrencyFactor){
    }

    void enqueueErrorJob (ErrorPtr&& error) {
        static thread_local int currentQueueIndex {0};

        m_queues[currentQueueIndex++].errorQueue.push(std::move(error));

        if (currentQueueIndex == m_queues.size()) { currentQueueIndex = 0; }
    }

    void enqueueRatingJob (id_t userId) {
        static thread_local int currentQueueIndex {0};

        m_queues[currentQueueIndex++].userIdQueue.push(std::move(userId));

        if (currentQueueIndex == m_queues.size()) { currentQueueIndex = 0; }
    }

    void enqueueRatingJob (const FullUserData* userData) {
        static thread_local int currentQueueIndex {0};

        m_queues[currentQueueIndex++].userDataQueue.push(std::move(userData));

        if (currentQueueIndex == m_queues.size()) { currentQueueIndex = 0; }
    }

    QueuePack& getQueuePack (int packIndex) {
        assert(packIndex >= 0 && packIndex < m_queues.size());

        return m_queues[packIndex];
    }

private:

    std::vector<QueuePack> m_queues;
};

// --------------------------------------------------------------------- //
/*
 *  JobQueue::QueueConsumer methods
 */
// --------------------------------------------------------------------- //

JobQueue::QueueConsumer::QueueConsumer (QueuePack& queuePack)
    : m_queuePack{queuePack} {}

ErrorPtr JobQueue::QueueConsumer::dequeueError () {
    ErrorPtr errorJob {};

    m_queuePack.errorQueue.tryPop(errorJob);

    return errorJob;
}

id_t JobQueue::QueueConsumer::dequeueUserId () {
    id_t userId {UserDataConstants::invalidRating};

    m_queuePack.userIdQueue.tryPop(userId);

    return userId;
}

const FullUserData* JobQueue::QueueConsumer::dequeueUserData () {
    const FullUserData* userData {nullptr};

    m_queuePack.userDataQueue.tryPop(userData);

    return userData;
}

// --------------------------------------------------------------------- //
/*
 *  JobQueue methods
 */
// --------------------------------------------------------------------- //

JobQueue::JobQueue (int concurrencyFactor)
    : m_concurrencyFactor{concurrencyFactor}, m_impl{std::make_unique<JobQueue::Impl>(concurrencyFactor)} {}

JobQueue::~JobQueue () {
    // this is required to compile JobQueue with a member of incomplete type
}

void JobQueue::enqueueErrorJob (ErrorPtr &&error) {
    m_impl->enqueueErrorJob(std::move(error));
}

void JobQueue::enqueueRatingJob (id_t userId) {
    m_impl->enqueueRatingJob(userId);
}

void JobQueue::enqueueRatingJob (const FullUserData *userData) {
    m_impl->enqueueRatingJob(userData);
}

JobQueue::QueueConsumer JobQueue::getConsumer (int concurrencyIndex) {
    assert(concurrencyIndex >= 0 && concurrencyIndex < m_concurrencyFactor);

    return QueueConsumer(m_impl->getQueuePack(concurrencyIndex));
}