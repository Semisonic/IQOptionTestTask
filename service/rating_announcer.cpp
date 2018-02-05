#include <iostream>
#include "rating_announcer.h"
#include "rating_calculator.h"
#include "job_queue.h"
#include "../utils/date_time.h"

RatingAnnouncer::RatingAnnouncer (const IterationData& userDistribution,
                                  JobQueue& queue,
                                  RatingCalculatorPtr&& calculator,
                                  SystemStopSignals& stopSignals,
                                  chrono_t& ratingExpirationDate)
: m_userDistribution {userDistribution} , m_queue {queue}, m_calculator {std::move(calculator)}
, m_stopSignals {stopSignals}, m_ratingExpirationDate(ratingExpirationDate) {
    assert(m_calculator);
}

RatingAnnouncer::~RatingAnnouncer () {
    try {
        if (m_taskHandle.valid()) {
            m_taskHandle.get();
        }
    } catch (const std::exception& e) {
        std::cerr << "Rating announcer exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Rating announcer exception: unknown exception" << std::endl;
    }
}

void RatingAnnouncer::start () {
    m_taskHandle = std::async(std::launch::async, &RatingAnnouncer::doWork, this);
}

void RatingAnnouncer::doWork () {
    try {
        auto dropOldRating {false};
        auto chronoSetIndex {0};

        {
            auto currentWeekStart = DateTime::currentWeekStart();

            if (!m_ratingExpirationDate.time_since_epoch().count()) {
                // it's our first run
                m_ratingExpirationDate = currentWeekStart;
            } else if (m_ratingExpirationDate != currentWeekStart) {
                dropOldRating = true;
            }

            std::this_thread::sleep_until(DateTime::nextFullSecond());

            chronoSetIndex = DateTime::currentSecondIndex();
        }

        auto weekJustTurned {false};
        auto steadyIntervalStart = std::chrono::steady_clock::now();

        for (;;) {
            m_calculator->recalculate(dropOldRating);

            if (dropOldRating) {
                dropOldRating = false;
                m_ratingExpirationDate = DateTime::currentWeekStart();
            }

            for (; chronoSetIndex < 60 && !m_stopSignals.badFlag.load(std::memory_order_relaxed); ++chronoSetIndex) {
                announce(m_userDistribution.usersOnline[chronoSetIndex]);

                steadyIntervalStart += std::chrono::seconds{1};

                auto now = std::chrono::steady_clock::now();

                if (now < steadyIntervalStart) {
                    std::this_thread::sleep_until(steadyIntervalStart);
                }
            }

            if (chronoSetIndex != 60) {
                // bad flag signaled
                break;
            }

            /*
             *  When week has turned, we work another minute serving old rating - because the rating we serve is always
             *  one minute behind. Then we drop it and start anew.
             *
             *  Sleeping till next full second allows us to sync our steady ticker with the system clock, so that
             *  we serve ratings at proper time moments. The downside is that during the first minute of the week only
             *  a part of the ratings might be served (but that's not a big deal since the ratings are fresh anyway)
             */

            if (weekJustTurned) {
                dropOldRating = true;
                weekJustTurned = false;

                std::this_thread::sleep_until(DateTime::nextFullSecond());
                chronoSetIndex = DateTime::currentSecondIndex();

                continue;
            }

            if (DateTime::currentWeekStart() > m_ratingExpirationDate) {
                weekJustTurned = true;
            }

            chronoSetIndex = 0;
        }
    } catch (...) {
        m_stopSignals.signalError();
    }
}

void RatingAnnouncer::announce (const ChronoSet& userBundle) {
    for (const auto userData : userBundle) {
        m_queue.enqueueRatingJob(userData);
    }
}