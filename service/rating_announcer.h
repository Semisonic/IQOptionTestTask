#ifndef IQOPTIONTESTTASK_RATING_ANNOUNCER_H
#define IQOPTIONTESTTASK_RATING_ANNOUNCER_H

#include <vector>
#include <thread>
#include <future>

#include "core_data.h"

struct JobQueue;
class RatingCalculator;

using RatingCalculatorPtr = std::unique_ptr<RatingCalculator>;

class RatingAnnouncer {
public:

    RatingAnnouncer (const IterationData& userDistribution,
                     JobQueue& queue,
                     RatingCalculatorPtr&& calculator,
                     SystemStopSignals& stopSignals,
                     chrono_t& ratingExpirationDate);
    ~RatingAnnouncer ();

    void start ();

private:

    void doWork ();

    void announce (const ChronoSet& userBundle);

private:

    const IterationData& m_userDistribution;
    JobQueue& m_queue;
    RatingCalculatorPtr m_calculator;
    SystemStopSignals& m_stopSignals;
    chrono_t& m_ratingExpirationDate;

    std::future<void> m_taskHandle;
};

#endif //IQOPTIONTESTTASK_RATING_ANNOUNCER_H
