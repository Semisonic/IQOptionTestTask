#ifndef IQOPTIONTESTTASK_STORAGE_H
#define IQOPTIONTESTTASK_STORAGE_H

#include <memory>
#include "../ipc/protocol.h"

// --------------------------------------------------------------------- //
/*
 *  Helper types and structures
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

struct BasicUserData {
    id_t id {UserDataConstants::invalidId};
    std::string name;
    connect_time_t secondConnected {UserDataConstants::invalidSecond};
};

struct FullUserData : public BasicUserData {
    monetary_t winnings {0};
    int rating {UserDataConstants::invalidRating};
};

using ErrorPtr = std::unique_ptr<IpcProto::GenericProtocolError>;

// --------------------------------------------------------------------- //
/*
 *  UserDataStorage class
 *
 *  stores the user data and validates service response
 */
// --------------------------------------------------------------------- //

class UserDataStorage {

    class Impl;

public:

    enum class UserFlags : unsigned int {
        CONNECTED = (1<<0),
        DISCONNECTED = (1<<1),
        ACTIVE = (1<<2),
        SILENT = (1<<3),
        CONNECTED_ANY = CONNECTED | ACTIVE | SILENT,
        DISCONNECTED_ANY = DISCONNECTED | ACTIVE | SILENT,
        ACTIVE_ANY = ACTIVE | CONNECTED | DISCONNECTED,
        SILENT_ANY = SILENT | CONNECTED | DISCONNECTED,

        ANYONE = UINT_MAX
    };

public:

    UserDataStorage ();
    ~UserDataStorage ();

    UserDataStorage (const UserDataStorage&) = delete;
    UserDataStorage (UserDataStorage&&) = delete;
    UserDataStorage& operator= (const UserDataStorage&) = delete;
    UserDataStorage& operator= (UserDataStorage&&) = delete;


    void setNextMinuteData (const UserDataStorage& uds);

    id_t getRandomUser (unsigned int userFlags) const;
    int getUserGroupSize (unsigned int userFlags) const;
    id_t getFakeUserId () const;

    BasicUserData* generateNewUser ();
    void importNewUser (BasicUserData* ud);

    BasicUserData* renameUser (id_t id, const std::string& newName);
    BasicUserData* connectUser (id_t id, unsigned char second);
    BasicUserData* disconnectUser (id_t id);
    FullUserData* fixUserWinnings (id_t id, monetary_t winnings);

    void validateError (const ErrorPtr& error);
    void validateRating (const IpcProto::RatingPackMessage& rating, connect_time_t currentSecond);

private:

    std::unique_ptr<Impl> m_impl;
};

#endif //IQOPTIONTESTTASK_STORAGE_H
