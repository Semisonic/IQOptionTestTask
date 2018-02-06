#ifndef IQOPTIONTESTTASK_STRATEGY_H
#define IQOPTIONTESTTASK_STRATEGY_H

#include <future>
#include <random>
#include "../utils/spinlock.h"
#include "../ipc/transport.h"
#include "storage.h"

class Strategy {
    struct MessageDistribution;

public:

    struct StrategyConfig {
        int usersAtStart {100};

        // per minute changes (based on the amount of users/eligible users)
        double newUsers {1./15};
        double renames {1./20};
        double connects {15./50};
        double disconnects {1./5};
        double wonDeals {1./2};
        double fakeUserOperations {1./20};
    };

public:

    Strategy (StrategyConfig config);
    ~Strategy ();

    void run (const std::string& host, const std::string& port);

private:

    void generateNewDistribution (unsigned char currentSecond);
    void processResponses ();

    monetary_t getRandomWinnings ();

private:

    StrategyConfig m_config;
    Spinlock m_dataAccess;
    ClientIpcTransport m_transport;
    std::future<void> m_taskHandle;
    std::atomic<bool> m_badFlag {false};
    std::mt19937 m_gen;

    UserDataStorage m_prevMinData;
    UserDataStorage m_curMinData;

    std::unique_ptr<MessageDistribution> m_distrib;
};

#endif //IQOPTIONTESTTASK_STRATEGY_H
