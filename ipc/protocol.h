#ifndef IQOPTIONTESTTASK_PROTOCOL_H
#define IQOPTIONTESTTASK_PROTOCOL_H

#include <cstddef>
#include <vector>
#include <string>
#include <climits>
#include <cassert>
#include <algorithm>

#include "../utils/types.h"
#include "../utils/binary_storage.h"

namespace IpcProto {

using id_t = int;
using message_code_t = unsigned char;
using error_code_t = unsigned int;
using protocol_version_t = unsigned int;
using monetary_t = long;
using message_size_t = unsigned short;

// --------------------------------------------------------------------- //
/*
 *  Protocol related numeric constants
 */
// --------------------------------------------------------------------- //

struct ProtocolConstants {
    static constexpr protocol_version_t version {1};
    static constexpr protocol_version_t invalidVersion {0};
    static constexpr id_t invalidUserId {-1};
    static constexpr message_code_t invalidMessageCode {static_cast<message_code_t>(-1)};

    enum class ClientMessageCode : message_code_t {
        HANDSHAKE = 111,

        USER_REGISTERED = 1,
        USER_RENAMED = 2,
        USER_DEAL_WON = 3,
        USER_CONNECTED = 4,
        USER_DISCONNECTED = 5
    };

    enum class ServiceMessageCode : message_code_t {
        PROTOCOL_ERROR = 1,
        USER_RATING = 2
    };

    enum class ProtocolError : error_code_t {
        PROTOCOL_VERSION_UNSUPPORTED = 1,
        USER_UNRECOGNIZED = 2,
        MULTIPLE_REGISTRATION = 3
    };

    struct RatingDimensions {
        static constexpr int topPositions {10};
        static constexpr int competitionDistance {10}; // how many positions before and after the user's one to fetch
    };
};

// --------------------------------------------------------------------- //
/*
 *  Incoming (client-to-service) message classes
 */
// --------------------------------------------------------------------- //

class HandshakeMsg {
public:

    HandshakeMsg () : m_protoVersion{ProtocolConstants::invalidVersion} {}
    HandshakeMsg (protocol_version_t version) : m_protoVersion{version} {}

    void init (BinaryIStream& buffer) {
        buffer >> m_protoVersion;
    }

    void store (BinaryOStream& buffer) const {
        buffer << m_protoVersion;
    }

    const protocol_version_t& version () const { return m_protoVersion; }

private:

    protocol_version_t m_protoVersion;
};

// --------------------------------------------------------------------- //

class GenericIdMsg {
public:

    GenericIdMsg () = default;
    GenericIdMsg (id_t userId) : m_userId{userId} {}

    void init (BinaryIStream& buffer) {
        buffer >> m_userId;
    }

    void store (BinaryOStream& buffer) const {
        buffer << m_userId;
    }

    id_t id () const { return m_userId; }

private:

    id_t m_userId {ProtocolConstants::invalidUserId};
};

// --------------------------------------------------------------------- //

class GenericIdNameMsg : public GenericIdMsg {
public:

    using GenericIdMsg::GenericIdMsg;

#ifdef PASS_NAMES_AROUND

    GenericIdNameMsg (id_t userId, buffer_t&& userName)
    : GenericIdMsg(userId)
    , m_userName(userName) {
        assert(m_userName.size() <= UCHAR_MAX);
    }

    GenericIdNameMsg (id_t userId, const std::string& userName)
    : GenericIdMsg(userId) {
        assert(userName.length() <= UCHAR_MAX);

        m_userName.resize(userName.length());
        memcpy(m_userName.data(), userName.data(), m_userName.size());
    }

    void init (BinaryIStream& buffer) {
        GenericIdMsg::init(buffer);

        buffer >> m_userName;
    }

    void store (BinaryOStream& buffer) const {
        GenericIdMsg::store(buffer);

        buffer << m_userName;
    }

    const buffer_t& name () const { return m_userName; }

#endif // PASS_NAMES_AROUND

private:

#ifdef PASS_NAMES_AROUND
    buffer_t m_userName;
#endif
};

// --------------------------------------------------------------------- //

class UserDealWonMsg : public GenericIdMsg {
public:

    UserDealWonMsg () = default;
    UserDealWonMsg (id_t userId, monetary_t winnings, chrono_t = chrono_t{})
    : GenericIdMsg(userId)
    , m_winnings {winnings} {
        /*
         *  Note that the timestamp is ignored even if provided. That's because the deals
         *  are accumulated within a weekly time frame on the server side based on the "received" time.
         *  This is a conscious tradeoff between absolute rating accuracy and service performance. If we
         *  assume that the deal info comes to the service as it is produced, without long-term caching
         *  (and this assumption is suitable since we must produce rating updates once a minute), then
         *  the absolute majority of all the deal data will be correctly aggregated within its corresponding
         *  week without any timestamp data coming from outside.
         */
    }

    monetary_t amount () const { return m_winnings; }

    void init (BinaryIStream& buffer) {
        GenericIdMsg::init(buffer);

        buffer >> m_winnings;
    }

    void store (BinaryOStream& buffer) const {
        GenericIdMsg::store(buffer);

        buffer << m_winnings;
    }

private:

    monetary_t m_winnings {0};
};

class UserRegisteredMsg : public GenericIdNameMsg { public: using GenericIdNameMsg::GenericIdNameMsg; };
class UserRenamedMsg : public GenericIdNameMsg { public: using GenericIdNameMsg::GenericIdNameMsg; };
class UserConnectedMsg : public GenericIdMsg { public: using GenericIdMsg::GenericIdMsg; };
class UserDisconnectedMsg : public GenericIdMsg { public: using GenericIdMsg::GenericIdMsg; };

// --------------------------------------------------------------------- //
/*
 *  UserMsgCodePrefixer class
 *
 *  a small utility helper template for writing the message codes to the buffer
 */

template<class MessageType>
class UserMsgCodePrefixer {};

template<>
class UserMsgCodePrefixer<HandshakeMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::HANDSHAKE); }
};

template<>
class UserMsgCodePrefixer<UserDealWonMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::USER_DEAL_WON); }
};

template<>
class UserMsgCodePrefixer<UserRegisteredMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::USER_REGISTERED); }
};

template<>
class UserMsgCodePrefixer<UserRenamedMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::USER_RENAMED); }
};

template<>
class UserMsgCodePrefixer<UserConnectedMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::USER_CONNECTED); }
};

template<>
class UserMsgCodePrefixer<UserDisconnectedMsg> {
public: static void prefix (BinaryOStream& buffer) { buffer << static_cast<message_code_t>(ProtocolConstants::ClientMessageCode::USER_DISCONNECTED); }
};

// --------------------------------------------------------------------- //
/*
*  Outgoing (service-to-client) messages
*/
// --------------------------------------------------------------------- //
/*
 *  Protocol error message classes
 *
 *  CAUTION! The error message classes are built with polymorphism in mind,
 *  so their serialization methods must put their corresponding error codes
 *  into the buffer.
 *  However, to restore an error message object from the
 *  serialized state one must read its error code first, and only then call
 *  the 'init' method of appropriate class. Hence, the 'init' method must not
 *  attempt to read the error code from the buffer.
 */

class GenericProtocolError {
public:

    virtual ~GenericProtocolError () {}

    ProtocolConstants::ProtocolError getErrorCode () const { return m_errorCode; }

    virtual void init (BinaryIStream& buffer) = 0;
    virtual void store (BinaryOStream& buffer) const = 0;

protected:

    GenericProtocolError (ProtocolConstants::ProtocolError code) : m_errorCode {code} {}

private:

    ProtocolConstants::ProtocolError m_errorCode;
};

// --------------------------------------------------------------------- //

class GenericUserIdError : public GenericProtocolError {
public:

    GenericUserIdError (ProtocolConstants::ProtocolError code) : GenericProtocolError(code) {}

    GenericUserIdError (ProtocolConstants::ProtocolError code, id_t userId)
    : GenericProtocolError(code), m_userId {userId} {}

    id_t getUserId () const { return m_userId; }

    void init (BinaryIStream& buffer) override {
        buffer >> m_userId;
    }

    void store (BinaryOStream& buffer) const override {
        buffer << static_cast<error_code_t>(getErrorCode()) << m_userId;
    }

private:

    id_t m_userId {ProtocolConstants::invalidUserId};
};

class UserUnrecognizedError : public GenericUserIdError {
public:

    UserUnrecognizedError () : GenericUserIdError (ProtocolConstants::ProtocolError::USER_UNRECOGNIZED) {}

    UserUnrecognizedError (id_t userId)
    : GenericUserIdError (ProtocolConstants::ProtocolError::USER_UNRECOGNIZED, userId) {}
};

class MultipleRegistrationError : public GenericUserIdError {
public:

    MultipleRegistrationError () : GenericUserIdError(ProtocolConstants::ProtocolError::MULTIPLE_REGISTRATION) {}

    MultipleRegistrationError (id_t userId)
    : GenericUserIdError (ProtocolConstants::ProtocolError::MULTIPLE_REGISTRATION, userId) {}
};

// --------------------------------------------------------------------- //

class UnsupportedProtocolVersionError : public GenericProtocolError {
public:

    UnsupportedProtocolVersionError ()
    : GenericProtocolError(ProtocolConstants::ProtocolError::PROTOCOL_VERSION_UNSUPPORTED)
    , m_expectedVersion {ProtocolConstants::invalidVersion} {}

    protocol_version_t getExpectedVersion () const { return m_expectedVersion; }

    void init (BinaryIStream& buffer) override {
        buffer >> m_expectedVersion;
    }

    void store (BinaryOStream& buffer) const override {
        buffer << static_cast<error_code_t>(getErrorCode()) << ProtocolConstants::version;
    }

private:

    protocol_version_t m_expectedVersion;
};

// --------------------------------------------------------------------- //
/*
 *  Rating message
 */

class RatingPackMessage {
public:

    struct RatingEntry {
        id_t id;
#ifdef PASS_NAMES_AROUND
        buffer_t name;
#endif
        monetary_t winnings;
    };

    using rating_pack_t = std::vector<RatingEntry>;

    class StorageBuilder {
    public:
        static void storePackHeader (BinaryOStream& buffer,
                                     id_t id, int ratingLength, int ratingPos) {
            buffer << id << ratingLength << ratingPos;
        }

        static void storePackEntry (BinaryOStream& buffer,
                                    id_t id, monetary_t winnings
#ifdef PASS_NAMES_AROUND
                                    , const buffer_t& name
#endif
                                    ) {
#ifdef PASS_NAMES_AROUND
            assert(name.size() <= UCHAR_MAX);
#endif
           buffer << id << winnings;

#ifdef PASS_NAMES_AROUND
            buffer << name;
#endif
        }
    };

public:

    id_t getUserId () const { return m_userId; }
    int getRatingLength () const { return m_ratingLength; }
    int getRatingPos () const { return m_ratingPos; }
    const rating_pack_t& getRatings () const { return m_ratings; }

public:

    void init (BinaryIStream& buffer) {
        constexpr auto& topPositions {ProtocolConstants::RatingDimensions::topPositions};
        constexpr auto& competitionDistance {ProtocolConstants::RatingDimensions::competitionDistance};

        buffer >> m_userId >> m_ratingLength >> m_ratingPos;

        assert(m_ratingLength >= 0 && m_ratingPos >= 0 && m_ratingPos <= m_ratingLength);

        auto ratingEntryCount =
                std::min(topPositions, m_ratingPos) + // the top positions
                (m_ratingPos > topPositions ? std::min(m_ratingPos - topPositions, competitionDistance) : 0) + // competition above
                std::min(m_ratingLength - m_ratingPos, competitionDistance + 1); // competition below + user himself

        m_ratings.resize(ratingEntryCount);

        for (auto i = 0; i < ratingEntryCount; ++i) {
            buffer >> m_ratings[i].id >> m_ratings[i].winnings;

#ifdef PASS_NAMES_AROUND
            buffer >> m_ratings[i].name;
#endif
        }
    }

private:

    id_t m_userId {ProtocolConstants::invalidUserId};
    int m_ratingLength {0};
    int m_ratingPos {0};
    rating_pack_t m_ratings;
};

} // namespace IpcProto

#endif //IQOPTIONTESTTASK_PROTOCOL_H
