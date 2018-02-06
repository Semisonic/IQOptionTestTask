#include <iostream>
#include <future>
#include "strategy.h"
#include "../utils/date_time.h"
#include "name_generator.h"

// --------------------------------------------------------------------- //
/*
 *  Helper types and classes
 */
// --------------------------------------------------------------------- //

enum MessageCode {
    MC_USER_REGISTERED = 0,
    MC_USER_RENAMED,
    MC_USER_CONNECTED,
    MC_USER_DISCONNECTED,
    MC_USER_DEAL_WON,
    MC_FAKE_USER
};

using MessageRequestList = std::list<MessageCode>;
using MessageMinuteMap = std::array<MessageRequestList, 60>;

struct Strategy::MessageDistribution {
    MessageMinuteMap distrib;
};

// --------------------------------------------------------------------- //
/*
 *  Strategy methods
 */
// --------------------------------------------------------------------- //

Strategy::Strategy (StrategyConfig config) : m_config {config} {
}
Strategy::~Strategy () {}

// --------------------------------------------------------------------- //

void Strategy::run (const std::string& host, const std::string& port) {
    auto mainThreadFUd {false};
    auto responseThreadFUd {false};

    try {
        m_transport.launch(host, port);

        m_taskHandle = std::async(std::launch::async, &Strategy::processResponses, this);

        auto messageBuffer = m_transport.createAdaptedMessageBuffer();
        auto pos = messageBuffer.getPos();

        // initial registration
        {
            for (auto i = 0; i < m_config.usersAtStart; ++i) {
                auto newUser = m_curMinData.generateNewUser();

                { std::lock_guard lg {m_dataAccess}; m_prevMinData.importNewUser(newUser); }
#ifdef PASS_NAMES_AROUND
                IpcProto::UserRegisteredMsg msg(newUser->id, newUser->name);
#else
                IpcProto::UserRegisteredMsg msg(newUser->id);
#endif
                IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                msg.store(messageBuffer);
                m_transport.writeMessage(messageBuffer);
                messageBuffer.rewind(pos);
            }
        }

        std::this_thread::sleep_until(DateTime::nextFullSecond());
        auto currentSecond = DateTime::currentSecondIndex();
        auto steadyIntervalStart = std::chrono::steady_clock::now();

        for (;;) {
            generateNewDistribution(currentSecond);

            for (; currentSecond < 60 && !m_badFlag.load(std::memory_order_relaxed); ++currentSecond) {
                for (auto newMsg : m_distrib->distrib[currentSecond]) {
                    messageBuffer.rewind(pos);
                    switch (newMsg) {
                        case MC_USER_REGISTERED: {
                            auto newUser = m_curMinData.generateNewUser();

                            { std::lock_guard lg {m_dataAccess}; m_prevMinData.importNewUser(newUser); }
#ifdef PASS_NAMES_AROUND
                            IpcProto::UserRegisteredMsg msg(newUser->id, newUser->name);
#else
                            IpcProto::UserRegisteredMsg msg(newUser->id);
#endif
                            IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                            msg.store(messageBuffer);
                            break;
                        }
                        case MC_USER_RENAMED: {
                            auto userId = m_curMinData.getRandomUser(static_cast<unsigned int>(UserDataStorage::UserFlags::ANYONE));
                            auto newName = NameGenerator::newName();
                            m_curMinData.renameUser(userId, newName);

                            { std::lock_guard lg {m_dataAccess}; m_prevMinData.renameUser(userId, newName); }
#ifdef PASS_NAMES_AROUND
                            IpcProto::UserRenamedMsg msg(userId, newName);
#else
                            IpcProto::UserRenamedMsg msg(userId);
#endif
                            IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                            msg.store(messageBuffer);
                            break;
                        }
                        case MC_USER_CONNECTED: {
                            auto userId = m_curMinData.getRandomUser(static_cast<unsigned int>(UserDataStorage::UserFlags::DISCONNECTED_ANY));
                            m_curMinData.connectUser(userId, currentSecond);

                            { std::lock_guard lg {m_dataAccess}; m_prevMinData.connectUser(userId, currentSecond); }

                            IpcProto::UserConnectedMsg msg(userId);
                            IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                            msg.store(messageBuffer);
                            break;
                        }
                        case MC_USER_DISCONNECTED: {
                            auto userId = m_curMinData.getRandomUser(static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED_ANY));
                            m_curMinData.disconnectUser(userId);

                            { std::lock_guard lg {m_dataAccess}; m_prevMinData.disconnectUser(userId); }

                            IpcProto::UserDisconnectedMsg msg(userId);
                            IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                            msg.store(messageBuffer);
                            break;
                        }
                        case MC_USER_DEAL_WON: {
                            auto userId = m_curMinData.getRandomUser(static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED_ANY));
                            auto winnings = getRandomWinnings();
                            m_curMinData.fixUserWinnings(userId, winnings);

                            // won deals is the only thing we report only to the new minute storage, to keep the prev minute rating valid
                            //{ std::lock_guard lg {m_dataAccess}; m_prevMinData.disconnectUser(userId); }

                            IpcProto::UserDealWonMsg msg(userId, winnings);
                            IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                            msg.store(messageBuffer);
                            break;
                        }
                        case MC_FAKE_USER: {
                            std::uniform_int_distribution<> dis(1, 4);
                            auto selectFakeActionType = dis(m_gen);
                            auto fakeId = m_curMinData.getFakeUserId();

                            switch (selectFakeActionType) {
                                case 1: {
#ifdef PASS_NAMES_AROUND
                                    IpcProto::UserRenamedMsg msg(fakeId, "Mr Fake");
#else
                                    IpcProto::UserRenamedMsg msg(fakeId);
#endif
                                    IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                                    msg.store(messageBuffer);
                                    break;
                                }
                                case 2: {
                                    IpcProto::UserConnectedMsg msg(fakeId);
                                    IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                                    msg.store(messageBuffer);
                                    break;
                                }
                                case 3: {
                                    IpcProto::UserDisconnectedMsg msg(fakeId);
                                    IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                                    msg.store(messageBuffer);
                                    break;
                                }
                                case 4: {
                                    IpcProto::UserDealWonMsg msg(fakeId, 666);
                                    IpcProto::UserMsgCodePrefixer<decltype(msg)>::prefix(messageBuffer);

                                    msg.store(messageBuffer);
                                    break;
                                }
                                default: assert(false);

                            }
                            break;
                        }
                        default: assert(false);
                    }

                    m_transport.writeMessage(messageBuffer);
                }

                steadyIntervalStart += std::chrono::seconds{1};
                auto now = std::chrono::steady_clock::now();

                if (now < steadyIntervalStart) {
                    std::this_thread::sleep_until(steadyIntervalStart);
                }
            }

            if (m_badFlag.load(std::memory_order_relaxed)) {
                break;
            }

            std::this_thread::sleep_until(DateTime::nextFullSecond());
            steadyIntervalStart = std::chrono::steady_clock::now();
            currentSecond = DateTime::currentSecondIndex();

            { std::lock_guard lg(m_dataAccess); m_prevMinData.setNextMinuteData(m_curMinData); }
        }

    } catch (...) {
        m_badFlag.store(true, std::memory_order_relaxed);
        mainThreadFUd = true;

        try {
            m_taskHandle.get();
        } catch (...) {
            responseThreadFUd = true;
        }
    }

    if (mainThreadFUd) {
        std::cout << "Main thread screwed up" << std::endl;
    }

    if (responseThreadFUd) {
        std::cout << "Response thread screwed up" << std::endl;
    }
}

// --------------------------------------------------------------------- //

void Strategy::generateNewDistribution (unsigned char currentSecond) {
    m_distrib.reset(new MessageDistribution);
    std::uniform_int_distribution<> dis(currentSecond, 59);

    // new users
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::ANYONE))
                    * m_config.newUsers * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_USER_REGISTERED);
        }
    }

    // renames
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::ANYONE))
                    * m_config.renames * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_USER_RENAMED);
        }
    }

    // connects
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::DISCONNECTED_ANY))
                    * m_config.connects * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_USER_CONNECTED);
        }
    }

    // disconnects
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED_ANY))
                    * m_config.disconnects * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_USER_DISCONNECTED);
        }
    }

    // deals won
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED_ANY))
                    * m_config.wonDeals * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_USER_DEAL_WON);
        }
    }

    // fake users
    {
        int count = m_curMinData.getUserGroupSize(static_cast<unsigned int>(UserDataStorage::UserFlags::ANYONE))
                    * m_config.fakeUserOperations * (60 - currentSecond) / 60.;

        for (auto i = 0; i < count; ++i) {
            m_distrib->distrib[dis(m_gen)].emplace_back(MC_FAKE_USER);
        }
    }
}

// --------------------------------------------------------------------- //

void Strategy::processResponses () {
    using MC = IpcProto::ProtocolConstants::ServiceMessageCode;
    using EC = IpcProto::ProtocolConstants::ProtocolError;

    try {
        ErrorPtr error;
        IpcProto::RatingPackMessage rating;
        buffer_t msgStorage;

        while (!m_badFlag.load(std::memory_order_relaxed)) {
            BinaryIStream buffer = m_transport.receive(msgStorage);
            IpcProto::message_code_t mc;
            auto currentSecond = DateTime::currentSecondIndex();

            buffer >> mc;

            switch (static_cast<MC>(mc)) {
                case MC::USER_RATING:
                    rating.init(buffer);
                    { std::lock_guard lg(m_dataAccess); m_prevMinData.validateRating(rating, currentSecond); }
                    break;
                case MC::PROTOCOL_ERROR: {
                    IpcProto::error_code_t ec;
                    buffer >> ec;

                    switch (static_cast<EC>(ec)) {
                        case EC::PROTOCOL_VERSION_UNSUPPORTED:
                            error.reset(new IpcProto::UnsupportedProtocolVersionError);
                            error->init(buffer);
                            std::cout << "\\\\\\ Protocol version mismatch, we're screwed ///" << std::endl;
                            throw "";
                        case EC::MULTIPLE_REGISTRATION:
                            error.reset(new IpcProto::MultipleRegistrationError);
                            error->init(buffer);
                            break;
                        case EC::USER_UNRECOGNIZED:
                            error.reset(new IpcProto::UserUnrecognizedError);
                            error->init(buffer);
                            break;
                        default: assert(false);
                    }

                    { std::lock_guard lg(m_dataAccess); m_prevMinData.validateError(error); }
                }
            }
        }

    } catch (const transport_error_recoverable&) {
        std::cout << "=== Recoverable transport error (should not happen nonetheless)" << std::endl;
        m_badFlag.store(true, std::memory_order_relaxed);
    } catch (const BinaryIStream::storage_underflow&) {
        std::cout << "=== Buffer underflow - the nasty protocol level error" << std::endl;
        m_badFlag.store(true, std::memory_order_relaxed);
    } catch (...) {
        m_badFlag.store(true, std::memory_order_relaxed);
    }
}

// --------------------------------------------------------------------- //

monetary_t Strategy::getRandomWinnings () {
    std::uniform_int_distribution<> dis(1,500);

    return dis(m_gen);
}