#pragma once
#include "yobot_typedef.h"
#include "yobotdata_new.h"

namespace yobot {
	namespace clanbattle {
		namespace detail {

            struct ChallengerDetail
            {
                bool is_continue;
                std::uint64_t behalf;
                bool tree;
                std::string msg;
            };

            struct Challenge
            {
                std::uint64_t userId;
                std::time_t time;
                int lap;
                int bossNum;
                std::int64_t bossHP;
                std::int64_t damage;
                bool isContinue;
                std::string message;
                std::uint64_t behafId;
            };

            struct status
            {
                std::int64_t lap;
                std::string gameServer;
                json challengerList;
                json subscribeList;
                json thisLapHPList;
                json nextLapHPList;
            };

            class Group
            {
            public:
                Group(std::uint64_t groupID) noexcept;
                ~Group() = default;
                Group(const Group&) = delete;
                Group(Group&) = delete;
                Group(Group&&) = default;

            public:
                explicit operator bool() const;

            public:
                Group* create();

                void addMember(const std::uint64_t userId);

                void deleteMemeber(const std::uint64_t userId);

                status getStatus();

                void setStatus(const std::int64_t lap, const json& thisLapBossHealth, const json& nextLapBossHealth);

                std::string getGameServer();

                void setChallenger(std::string_view bossNum, std::uint64_t userId, const ChallengerDetail& detail);

                void removeChallenger(std::string_view bossNum, std::uint64_t userId);

                void pushChallenge(const Challenge& challenge);

                void popChallenge();

                void clearChallenge();

            private:
                void updateStatusInternal(const std::function<void(status&)>& func);

                void updateChanllengerList(const std::function<void(json&)>& func);

            private:
                std::shared_ptr<DB_Pool> m_pool;
                std::uint64_t m_groupID;
                data::ClanGroup m_clanGroup;
                data::ClanChallenge m_clanChallenge;
                data::ClanMember m_clanMember;
            };
		}
	}
}

