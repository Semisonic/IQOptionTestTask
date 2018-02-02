#ifndef IQOPTIONTESTTASK_CORE_DATA_H
#define IQOPTIONTESTTASK_CORE_DATA_H

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <map>
#include <set>

#include "../ipc/protocol.h"

// --------------------------------------------------------------------- //
/*
 *  Basic types and constants
 */
// --------------------------------------------------------------------- //

using id_t = IpcProto::id_t;
using monetary_t = IpcProto::monetary_t;
using connect_time_t = unsigned char;

struct UserDataConstants {
    static constexpr connect_time_t invalidSecond {60};
    static constexpr id_t invalidId {IpcProto::ProtocolConstants::invalidUserId};
    static constexpr int invalidRating {-1};
};

// --------------------------------------------------------------------- //
/*
 *  Rating-related types
 */
// --------------------------------------------------------------------- //

class Overseer;

struct BasicUserData {
    BasicUserData () = default;
    BasicUserData (const BasicUserData&) = delete;
    BasicUserData (BasicUserData&&) = default;

    connect_time_t secondConnected { UserDataConstants::invalidSecond };

#ifdef PASS_NAMES_AROUND
    buffer_t name;
#endif // PASS_NAMES_AROUND
};

struct FullUserData : public BasicUserData {
    FullUserData (const FullUserData&) = delete;
    FullUserData (FullUserData&&) = delete;
    FullUserData (id_t userId, monetary_t winnings, BasicUserData&& basicData)
        : BasicUserData(std::move(basicData)), id {userId}, amountWon {winnings} {}

    id_t id { UserDataConstants::invalidId };
    monetary_t amountWon { 0 };
    int rating { UserDataConstants::invalidRating };
};

using SilentUsersMap = std::unordered_map<id_t, BasicUserData>;
using ActiveUsersMap = std::unordered_map<id_t, std::unique_ptr<FullUserData>>;
using RatingVector = std::vector<FullUserData*>;

struct CoreRatingData {
    SilentUsersMap silentUsers;
    ActiveUsersMap activeUsers;
    RatingVector rating;

    chrono_t expirationDate;
};

struct SystemStopSignals {
    void signalError (bool unrecoverable = true) {
        if (unrecoverable) unrecoverableError.store(true, std::memory_order_relaxed);
        badFlag.store(true, std::memory_order_relaxed);
    }

    std::atomic_bool unrecoverableError;
    std::atomic_bool badFlag;
};

struct CoreDataSyncBlock {
    std::mutex dataLock;
    std::condition_variable dataRefreshedTrigger;
    std::atomic_bool refreshInProgress;
    std::atomic_int dataReaderCount;

    SystemStopSignals stopSignals;
};

// --------------------------------------------------------------------- //
/*
 *  User/connection time binding types
 */
// --------------------------------------------------------------------- //

using ChronoSet = std::unordered_set<const FullUserData*>;

struct IterationData {
    std::array<ChronoSet, 60> usersOnline;
};

// --------------------------------------------------------------------- //
/*
 *  Incoming data buffer types
 */
// --------------------------------------------------------------------- //

using ConnectionsMap = std::map<id_t, connect_time_t>;

#ifdef PASS_NAMES_AROUND
using UserNameMap = std::map<id_t, buffer_t>;
#else
using UserRoster = std::set<id_t>;
#endif

using DealsMap = std::map<id_t, monetary_t>;

struct IncomingDataBuffer {

#ifdef PASS_NAMES_AROUND
    UserNameMap usersRegistered;
    UserNameMap usersRenamed;
#else
    UserRoster usersRegistered;
#endif // PASS_NAMES_AROUND

    ConnectionsMap connectionChanges;
    DealsMap dealsWon;

    std::atomic_int bufferWriterCount { 0 };
};

struct IncomingDataDoubleBuffer {
    IncomingDataBuffer buffers[2];
    int currentBufferIndex { 0 };
    std::atomic<IncomingDataBuffer*> currentBuffer { &buffers[0] };
};

#endif //IQOPTIONTESTTASK_CORE_DATA_H
