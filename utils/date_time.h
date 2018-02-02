#ifndef IQOPTIONTESTTASK_DATE_TIME_H
#define IQOPTIONTESTTASK_DATE_TIME_H

#include <chrono>

#include "../utils/types.h"

class DateTime {
    using days = std::chrono::duration
            <int, std::ratio_multiply<std::ratio<24>, std::chrono::hours::period>>;
    using weeks = std::chrono::duration
            <int, std::ratio_multiply<std::ratio<7>, days::period>>;

    using minutes = std::chrono::minutes;
    using seconds = std::chrono::seconds;

public:

    static chrono_t currentWeekStart () {
        chrono_t now = std::chrono::system_clock::now();

        now -= days{4};
        auto sysWeekStart = std::chrono::floor<weeks>(now);
        auto properWeekStart = std::chrono::time_point_cast<days>(sysWeekStart) + days{4};

        return std::chrono::time_point_cast<chrono_t::duration>(properWeekStart);
    }

    static chrono_t nextFullMinute () {
        chrono_t now = std::chrono::system_clock::now();

        return std::chrono::time_point_cast<chrono_t::duration>(std::chrono::ceil<minutes>(now));
    }

    static chrono_t nextFullSecond () {
        chrono_t now = std::chrono::system_clock::now();

        return std::chrono::time_point_cast<chrono_t::duration>(std::chrono::ceil<seconds>(now));
    }

    static unsigned char currentSecondIndex () {
        chrono_t now = std::chrono::system_clock::now();
        auto secondsFromTheMinuteStart = std::chrono::duration_cast<seconds>(
                std::chrono::floor<seconds>(now)-std::chrono::floor<minutes>(now));

        return static_cast<unsigned char>(secondsFromTheMinuteStart.count());
    }
};

#endif //IQOPTIONTESTTASK_DATE_TIME_H
