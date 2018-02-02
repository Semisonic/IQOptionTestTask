#ifndef IQOPTIONTESTTASK_SPINLOCK_H
#define IQOPTIONTESTTASK_SPINLOCK_H

#include <atomic>

class Spinlock {
public:

    void lock () noexcept {
        while (locked.test_and_set(std::memory_order_acquire));
    }

    void unlock () noexcept {
        locked.clear(std::memory_order_release);
    }

private:

    std::atomic_flag locked = ATOMIC_FLAG_INIT;
};

#endif //IQOPTIONTESTTASK_SPINLOCK_H
