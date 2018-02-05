#include "storage.h"
#include <list>
#include <map>
#include <random>
#include <set>
#include <iostream>

#include "name_generator.h"

struct FullUserDataEx : public FullUserData {
    bool ratingReceived {false};
    std::list<monetary_t> winningsHistory;
};

constexpr int historyLength = 6;

using FullUserDataPtr = std::unique_ptr<FullUserDataEx>;

struct ValidationReport {
    int incomingRatings {0};
    int incomingErrors {0};

    int validRatings {0};
    int validErrors {0};

    struct {
        int timingMissedWithinSecond {0};
        int outdatedWinnings {0};
    } almostValidRatings;

    struct {
        int ratingFullyMessed {0};
        int ratingSizeWrong {0};
        int userNotFound {0};
        int userPositionWrong {0};
        int topPositionsWrong {0};
        int surroundingsWrong {0};
        int timingMessed {0};
    } invalidRatings;

    int failures {0};
};

// --------------------------------------------------------------------- //
/*
 *  UserDataStorage::Impl class
 */
// --------------------------------------------------------------------- //

class UserDataStorage::Impl {

    using UserDataMap = std::map<id_t, FullUserDataPtr>;

    // 0 - active, connected
    // 1 - active, disconnected
    // 2 - silent, connected
    // 3 - silent, disconnected
    using UserArray = std::array<UserDataMap, 4>;
    using RatingVector = std::vector<FullUserData*>;
    using MapIndexSet = std::set<int>; // indexes are from the map array
    using IndexMap = std::map<id_t, FullUserDataEx*>;

    using RatingMultimap = std::multimap<monetary_t, FullUserData*, std::greater<monetary_t>>;

public:

    Impl () : m_gen {std::random_device{}()} {}

    void setNextMinuteData (const Impl& newData) {
        publishValidationReport();

        // deep copy of user data and build the index
        {
            m_index.clear();
            auto itMapTo = m_users.begin();

            for (const auto& itMapFrom : newData.m_users) {
                itMapTo->clear();

                for (const auto& user : itMapFrom) {
                    auto newUser = itMapTo->emplace(user.first, std::make_unique<FullUserDataEx>(*user.second.get()));
                    m_index.emplace(newUser.first->first, newUser.first->second.get());
                }

                ++itMapTo;
            }
        }

        // that works, but I'd never do stuff like that in production code =)
        memset(&m_report, 0, sizeof(ValidationReport));

        // rebuilding rating
        recalculateRating();
    }

    id_t getRandomUser (unsigned int userFlags) {
        MapIndexSet maps {0, 1, 2, 3};

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED))) {
            maps.erase(0);
            maps.erase(2);
        }

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::DISCONNECTED))) {
            maps.erase(1);
            maps.erase(3);
        }

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::ACTIVE))) {
            maps.erase(0);
            maps.erase(1);
        }

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::SILENT))) {
            maps.erase(2);
            maps.erase(3);
        }

        auto userCount = getCumulativeSize(maps);
        std::uniform_int_distribution<> dis(0, userCount - 1);
        auto index = dis(m_gen);

        return getUserByIndex(maps, index);
    }

    int getUserGroupSize (unsigned int userFlags) const {
        MapIndexSet maps {0, 1, 2, 3};

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::CONNECTED))) {
            maps.erase(0);
            maps.erase(2);
        }

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::DISCONNECTED))) {
            maps.erase(1);
            maps.erase(3);
        }

        if (!(userFlags & static_cast<unsigned int>(UserDataStorage::UserFlags::ACTIVE))) {
            maps.erase(0);
            maps.erase(1);
        }

        return getCumulativeSize(maps);
    }

    id_t getFakeUserId () const {
        static int count {0};

        return UserDataConstants::invalidId + count--;
    }

    BasicUserData* generateNewUser () {
        static id_t newUserId {0};

        FullUserDataPtr newUser {new FullUserDataEx};
        auto userData = newUser.get();

        userData->id = newUserId;
        userData->name = NameGenerator::newName();

        m_users[3].emplace(newUserId, std::move(newUser));
        m_index.emplace(newUserId++, userData);

        return userData;
    }

    void importNewUser (BasicUserData* ud) {
        FullUserDataPtr newUser {new FullUserDataEx};
        auto userData = newUser.get();

        userData->id = ud->id;
        userData->name = ud->name;

        m_users[3].emplace(ud->id, std::move(newUser));
        m_index.emplace(ud->id, userData);
    }

    BasicUserData* renameUser (id_t id, const std::string& newName) {
        auto userData = m_index.find(id);

        assert(userData != m_index.end());

        userData->second->name = newName;

        return userData->second;
    }

    BasicUserData* connectUser (id_t id, unsigned char second) {
        auto userData = m_index.find(id);

        assert(userData != m_index.end());

        userData->second->secondConnected = second;

        if (userData->second->winnings) {
            findAndMigrate(m_users[1], m_users[0], id);
        } else {
            findAndMigrate(m_users[3], m_users[2], id);
        }

        return userData->second;
    }

    BasicUserData* disconnectUser (id_t id) {
        auto userData = m_index.find(id);

        assert(userData != m_index.end());

        userData->second->secondConnected = UserDataConstants::invalidSecond;

        if (userData->second->winnings) {
            findAndMigrate(m_users[0], m_users[1], id);
            userData->second->ratingReceived = false; // to prevent false positive unsolicited ratings received warnings
        } else {
            findAndMigrate(m_users[2], m_users[3], id);
        }

        return userData->second;
    }

    FullUserData* fixUserWinnings (id_t id, monetary_t winnings) {
        auto userData = m_index.find(id);

        assert(userData != m_index.end());

        if (userData->second->winnings == 0) {
            if (userData->second->secondConnected == UserDataConstants::invalidSecond) {
                findAndMigrate(m_users[3], m_users[1], id);
            } else {
                findAndMigrate(m_users[2], m_users[0], id);
            }
        }

        userData->second->winningsHistory.push_back(userData->second->winnings);

        if (userData->second->winningsHistory.size() > historyLength) {
            userData->second->winningsHistory.pop_front();
        }

        userData->second->winnings += winnings;
    }

    void validateError (const ErrorPtr& error) {
        using ProtocolError = IpcProto::ProtocolConstants::ProtocolError;

        ++m_report.incomingErrors;

        switch (error->getErrorCode()) {
        case ProtocolError::MULTIPLE_REGISTRATION:
            {
                IpcProto::MultipleRegistrationError* e = static_cast<IpcProto::MultipleRegistrationError*>(error.get());

                ++m_report.failures;
                std::cout << "--- Unexpected multiple registration error: id = " << e->getUserId() << std::endl;

                break;
            }
        case ProtocolError::USER_UNRECOGNIZED:
            {
                IpcProto::UserUnrecognizedError* e = static_cast<IpcProto::UserUnrecognizedError*>(error.get());

                if (e->getUserId() < 0) {
                    // fake user id, no wonder it didn't get recognized
                    ++m_report.validErrors;
                    break;
                }

                auto user = m_index.find(e->getUserId());

                ++m_report.failures;
                std::cout << "--- Unexpected user unrecognized error: id = " << e->getUserId() << std::endl;

                if (user == m_index.end()) {
                    std::cout << "~~~~~ WTF! I don't recognize this user either!!" << std::endl;
                }

                break;
            }
        default: assert(false);
        }
    }

    void validateRating (const IpcProto::RatingPackMessage& rating, connect_time_t currentSecond) {
        bool failure {false};
        ++m_report.incomingRatings;

        using RatingDimensions = IpcProto::ProtocolConstants::RatingDimensions;

        // 1) checking user id validity
        if (rating.getUserId() < 0 || rating.getUserId() >= m_index.size()) {
            ++m_report.invalidRatings.ratingFullyMessed;
            std::cout << "!!! Rating error: complete mess (user id = " << rating.getUserId() << ")" << std::endl;

            ++m_report.failures;

            // can't expect to trust other sections of the rating, no point in continuing
            return;
        }

        auto userData = m_index.find(rating.getUserId());

        // 2) checking rating size
        if (rating.getRatingLength() != m_rating.size()) {
            failure = true;
            ++m_report.invalidRatings.ratingSizeWrong;
            std::cout << "! Rating error: wrong rating size ("
                      << rating.getRatingLength() << " instead of " << m_rating.size() << ")" << std::endl;
        }

        // 3) checking the user's place
        if (userData != m_index.end()) {
            userData->second->ratingReceived = true;

            // 6) check timing
            if (userData->second->secondConnected != currentSecond) {
                if (std::abs(currentSecond - userData->second->secondConnected) == 1) {
                    ++m_report.almostValidRatings.timingMissedWithinSecond;
                } else {
                    failure = true;
                    ++m_report.invalidRatings.timingMessed;
                    std::cout << "! Rating error: invalid timing ("
                              << int{currentSecond} << " instead of " << int{userData->second->secondConnected} << ")" << std::endl;
                }

            }

            if (userData->second->rating != UserDataConstants::invalidRating) {
                // user wasn't added after the rating had been recalculated
                if (userData->second->rating != rating.getRatingPos()) {
                    failure = true;
                    ++m_report.invalidRatings.userPositionWrong;
                    std::cout << "! Rating error: wrong user position ("
                              << rating.getRatingPos() << " instead of " << userData->second->rating << ")" << std::endl;
                }
            } else {
                if (rating.getRatingPos() != rating.getRatingLength()) {
                    failure = true;
                    ++m_report.invalidRatings.userPositionWrong;
                    std::cout << "! Rating error: wrong user position ("
                              << rating.getRatingPos() << " instead of " << m_rating.size() << ")" << std::endl;
                }
            }
        } else {
            failure = true;
            ++m_report.invalidRatings.userNotFound;
            std::cout << "! Rating error: user not found (id = "
                      << rating.getUserId() << ")" << std::endl;

        }

        // 4) check top positions
        {
            const IpcProto::RatingPackMessage::rating_pack_t& ratings = rating.getRatings();
            auto topPosCount = std::min(static_cast<int>(ratings.size()), RatingDimensions::topPositions);

            for (auto i = 0; i < topPosCount; ++i) {
                auto result = validateSingleRating(ratings[i], i);

                if (result) {
                    failure = true;
                    ++m_report.invalidRatings.topPositionsWrong;

                    reportSingleRating(ratings[i], i, result, "top user");
                }
            }
        }

        // 5) check surroundings
        if (rating.getRatingLength() > RatingDimensions::topPositions) {
            const IpcProto::RatingPackMessage::rating_pack_t& ratings = rating.getRatings();
            auto expectedPlace = std::max(RatingDimensions::topPositions, rating.getRatingPos() - RatingDimensions::competitionDistance);

            for (auto i = RatingDimensions::topPositions; i < ratings.size(); ++i, ++expectedPlace) {
                auto result = validateSingleRating(ratings[i], expectedPlace);

                if (result) {
                    failure = true;
                    ++m_report.invalidRatings.surroundingsWrong;

                    reportSingleRating(ratings[i], expectedPlace, result, "surrounding user");
                }
            }
        }

        if (failure) {
            ++m_report.failures;
        } else {
            ++m_report.validRatings;
        }
    }

private:

    void recalculateRating () {
        RatingMultimap ratingBuilder;
        auto proc = [&ratingBuilder](FullUserData* ud) { ratingBuilder.emplace(ud->winnings, ud); };

        forEachInMaps(MapIndexSet {0, 1}, proc);

        m_rating.resize(ratingBuilder.size());
        auto ratingPlace {0};

        for (auto user : ratingBuilder) {
            m_rating[ratingPlace] = user.second;
            user.second->rating = ratingPlace++;
        }

        // ratingPlace by now is a rating length
        auto proc2 = [](FullUserData* ud) { ud->rating = UserDataConstants::invalidRating; };
        forEachInMaps(MapIndexSet {2, 3}, proc2);
    }

    void publishValidationReport () {
        auto unsolicitedRatingsReceived {0};
        auto requestedRatingsMissed {0};

        auto proc1 = [&requestedRatingsMissed](const FullUserDataEx* ud) { if (!ud->ratingReceived) ++requestedRatingsMissed; };
        auto proc2 = [&unsolicitedRatingsReceived](const FullUserDataEx* ud) { if (ud->ratingReceived) ++unsolicitedRatingsReceived; };

        forEachInMaps(MapIndexSet{0}, proc1);
        forEachInMaps(MapIndexSet{1,3}, proc2);

        std::cout << "********************** Minutely validation report **********************" << std::endl
                  << "* Incoming ratings: " << m_report.incomingRatings << std::endl
                  << "* Valid ratings: " << m_report.validRatings << std::endl
                  << "* Incoming errors: " << m_report.incomingErrors << std::endl
                  << "* Valid errors: " << m_report.validErrors << std::endl
                  << "** Almost valid ratings **" << std::endl
                  << "* Time missed for less than a second: " << m_report.almostValidRatings.timingMissedWithinSecond << std::endl
                  << "* Winnings outdated but correct: " << m_report.almostValidRatings.outdatedWinnings << std::endl
                  << "*********** !!! Failures !!! ***********" << std::endl
                  << "* Invalid ratings: " << m_report.incomingRatings - m_report.validRatings << std::endl
                  << "* Invalid errors: " << m_report.incomingErrors - m_report.validErrors << std::endl
                  << "* Unsolicited ratings received: " << unsolicitedRatingsReceived << std::endl
                  << "* Requested ratings missed: " << requestedRatingsMissed << std::endl
                  << "* Failures overall: " << m_report.failures << std::endl
                  << "***** Invalid rating details *****" << std::endl
                  << "* Total mess: " << m_report.invalidRatings.ratingFullyMessed << std::endl
                  << "* Rating size wrong: " << m_report.invalidRatings.ratingSizeWrong << std::endl
                  << "* User not found: " << m_report.invalidRatings.userNotFound << std::endl
                  << "* User position wrong: " << m_report.invalidRatings.userPositionWrong << std::endl
                  << "* Top positions wrong: " << m_report.invalidRatings.topPositionsWrong << std::endl
                  << "* Surroundings wrong: " << m_report.invalidRatings.surroundingsWrong << std::endl
                  << "* Timing wrong: " << m_report.invalidRatings.timingMessed << std::endl
                  << "********************** Report end **********************" << std::endl;
    }

private:

    id_t getUserByIndex (const MapIndexSet& mapIndexes, int userIndex) const {
        for (auto mi : mapIndexes) {
            if (userIndex >= m_users[mi].size()) {
                userIndex -= m_users[mi].size();

                continue;
            }

            for (const auto& user : m_users[mi]) {
                if (!userIndex--) {
                    return user.first;
                }
            }
        }

        assert(false);
    }

    int getCumulativeSize (const MapIndexSet& mapIndexes) const {
        int result {0};

        for (auto mi : mapIndexes) {
            result += m_users[mi].size();
        }

        return result;
    }

    static void findAndMigrate (UserDataMap& mapFrom, UserDataMap& mapTo, id_t id) {
        auto user = mapFrom.find(id);
        assert(user != mapFrom.end());

        mapTo.insert(mapFrom.extract(user));
    }

    template <typename Processor>
    void forEachInMaps (const MapIndexSet& mapIndexes, Processor proc) {
        for (auto mi : mapIndexes) {
            for (auto& user : m_users[mi]) {
                proc(user.second.get());
            }
        }
    }

    // 0 - OK
    // 1 - user not found
    // 2 - wrong position
    // 4 - wrong winnings
    // 8 - wrong name
    int validateSingleRating (const IpcProto::RatingPackMessage::RatingEntry& rating, int expectedPlace) {
        auto userData = m_index.find(rating.id);
        auto result {0};

        if (userData == m_index.end()) {
            return 1;
        }

        if (userData->second->rating != expectedPlace) {
            result |= 2;
        }

        if (userData->second->winnings != rating.winnings) {
            auto history = std::find(userData->second->winningsHistory.rbegin(), userData->second->winningsHistory.rend(), rating.winnings);

            if (history != userData->second->winningsHistory.rend()) {
                // winnings were found in the history, might be not a total error after all
                ++m_report.almostValidRatings.outdatedWinnings;
                result &= ~2;
            } else {
                result |= 4;
            }
        }

#ifdef PASS_NAMES_AROUND
        if (userData->second->name != std::string((char*)rating.name.data(), rating.name.size())) {
            result |= 8;
        }
#endif
        return result;
    }

    void reportSingleRating (const IpcProto::RatingPackMessage::RatingEntry& rating, int expectedPlace, int validationResult,
                             const std::string& positionMoniker) {
        if (validationResult & 1) {
            std::cout << "! Rating error: " << positionMoniker << " not found (id = "
                      << rating.id << ")" << std::endl;
        }

        if (validationResult & 2) {
            std::cout << "! Rating error: " << positionMoniker << " position wrong (" << expectedPlace << " instead of "
                      << m_index.find(rating.id)->second->rating << ")" << std::endl;
        }

        if (validationResult & 4) {
            std::cout << "! Rating error: " << positionMoniker << " winnings wrong (" << rating.winnings << " instead of "
                      << m_index.find(rating.id)->second->winnings << ")" << std::endl;
        }

#ifdef PASS_NAMES_AROUND
        if (validationResult & 8) {
            std::cout << "! Rating error: " << positionMoniker << " name wrong (\"" << std::string((char*)rating.name.data(), rating.name.size())
                      << "\" instead of \"" << m_index.find(rating.id)->second->name << "\")" << std::endl;
        }
#endif
    }


private:

    UserArray m_users;
    IndexMap m_index;
    RatingVector m_rating;
    ValidationReport m_report;
    int m_userPromise {0};

    std::mt19937 m_gen;
};

// --------------------------------------------------------------------- //
/*
 *  UserDataStorage methods
 */
// --------------------------------------------------------------------- //

UserDataStorage::UserDataStorage () : m_impl {std::make_unique<UserDataStorage::Impl>()} {}
UserDataStorage::~UserDataStorage () {}

void UserDataStorage::setNextMinuteData (const UserDataStorage& uds) {
    m_impl->setNextMinuteData(*uds.m_impl.get());
}

id_t UserDataStorage::getRandomUser (unsigned int userFlags) const {
    m_impl->getRandomUser(userFlags);
}

int UserDataStorage::getUserGroupSize (unsigned int userFlags) const {
    m_impl->getUserGroupSize(userFlags);
}

id_t UserDataStorage::getFakeUserId () const {
    return m_impl->getFakeUserId();
}

BasicUserData* UserDataStorage::generateNewUser () {
    return m_impl->generateNewUser();
}

void UserDataStorage::importNewUser (BasicUserData *ud) {
    m_impl->importNewUser(ud);
}

BasicUserData* UserDataStorage::renameUser (id_t id, const std::string& newName) {
    return m_impl->renameUser(id, newName);
}

BasicUserData* UserDataStorage::connectUser (id_t id, unsigned char second) {
    return m_impl->connectUser(id, second);
}

BasicUserData* UserDataStorage::disconnectUser (id_t id) {
    return m_impl->disconnectUser(id);
}

FullUserData* UserDataStorage::fixUserWinnings (id_t id, monetary_t winnings) {
    return m_impl->fixUserWinnings(id, winnings);
}

void UserDataStorage::validateError (const ErrorPtr& error) {
    m_impl->validateError(error);
}

void UserDataStorage::validateRating (const IpcProto::RatingPackMessage& rating, connect_time_t currentSecond) {
    m_impl->validateRating(rating, currentSecond);
}