#include <mimalloc-new-delete.h>
#include <mimalloc-override.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <httplib.h>
#include <tbb/tbb.h>
#include "yobotdata_new.h"
#include "default_config.h"
#include "yobotdata_new.sql.h"

using nlohmann::json;
using nlohmann::ordered_json;

constexpr auto VersionInfo = "Branch: " GIT_BRANCH "\nCommit: " GIT_VERSION "\nDate: " GIT_DATE;
constexpr auto TargetLocaleName = "zh_CN.UTF-8";
constexpr auto C_LocaleName = "C";
constexpr auto DB_Name = "yobotdata_new.db";
constexpr auto ConfigName = "yobot_config.json";
constexpr auto IconDir = "icon";
constexpr auto AuthorityErrorRespone = "权限错误";
constexpr auto Group404ErrorResponse = "未检测到数据，请先创建公会！";

namespace yobot {
    using twobot::Event::GroupMsg;
    using twobot::Event::PrivateMsg;
    using twobot::Event::ConnectEvent;
    using DB_Pool = sqlpp::sqlite3::connection_pool;
    using DB_Config = sqlpp::sqlite3::connection_config;
    using Message = std::variant<GroupMsg, PrivateMsg>;
    using Action = std::function<std::string(const Message&)>;
    using RegexAction = std::pair<const std::regex&, const Action&>;
    using RegexActionVector = std::vector<RegexAction>;
    using Instance = std::tuple<std::unique_ptr<twobot::BotInstance>, std::shared_ptr<DB_Pool>, ordered_json, RegexActionVector>;
    using BoosData = std::tuple<std::string_view, json::array_t, json::array_t, json::array_t, json::array_t>;

    static Instance& getInstance();

    namespace area {
        constexpr std::string_view cn = "cn";
        constexpr std::string_view tw = "tw";
        constexpr std::string_view jp = "jp";
    }

    namespace cqcode {
        inline constexpr std::string cq(const char* op, auto id)
        {
            return std::format("[CQ:{},qq={}]", op, id);
        }

        inline constexpr std::string at(auto id)
        {
            return cq("at", id);
        }

        inline constexpr std::string avatar(auto id)
        {
            return cq("avatar", id);
        }
    }

    static auto InitConfig() noexcept
    {
#ifdef _WIN32
        ::system("chcp 65001 && cls");
#endif
        std::cout << "Initializing...\n";
        std::setlocale(LC_ALL, TargetLocaleName);
        std::setlocale(LC_NUMERIC, C_LocaleName);
        std::locale::global(std::locale(TargetLocaleName));
        std::locale::global(std::locale(C_LocaleName, std::locale::numeric));
        auto dbConfig = std::make_shared<yobot::DB_Config>(DB_Name, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        if (!std::filesystem::exists(ConfigName) || std::filesystem::file_size(ConfigName) == 0)
        {
            std::ofstream(ConfigName) << DEFAULT_CONFIG;
        }
        auto globalConfig = ordered_json::parse(std::ifstream(ConfigName));
        auto accessToken = globalConfig["access_token"].get<std::string>();
        twobot::Config botConfig = {
            .host = "10.9.0.1",
            .api_port = 5700,
            .ws_port = globalConfig["port"],
            .token = (accessToken.empty() ? std::nullopt : std::make_optional(accessToken))
        };
        return std::make_tuple(botConfig, dbConfig, globalConfig);
    }

    static std::shared_ptr<yobot::DB_Pool> InitDatabase(const std::shared_ptr<yobot::DB_Config>& dbConfig) noexcept
    {
        auto dbPool = std::make_shared<yobot::DB_Pool>(dbConfig, 2);
        auto db = dbPool->get();
        for (auto&& line : YOBOT_DATA_NEW_SQL)
        {
            try
            {
                db.execute(line);
            }
            catch (sqlpp::exception ignore)
            {

            }
        }
        return dbPool;
    }

    namespace system {
        namespace detail {
            inline bool isSuperAdmin(uint64_t userId)
            {
                auto& globalConfig = std::get<2>(getInstance());
                auto& a = globalConfig["super-admin"].get_ref<const ordered_json::array_t&>();
                return std::find(a.begin(), a.end(), userId) != a.end();
            }

            inline bool isSuperAdmin(const Message& msg)
            {
                return std::visit([](auto&& x) {
                    return isSuperAdmin(x.user_id);
                }, msg);
            }

            inline void fetchBossData(BoosData& bossData, tbb::concurrent_unordered_set<json::number_integer_t>& idSet)
            {
                auto&& [itArea, itBossHP, itLapRange, itBossId, itBossName] = bossData;
                httplib::Client client("https://pcr.satroki.tech");
                client.set_follow_location(true);
                auto result = client.Get("/api/Quest/GetClanBattleInfos?s=" + std::string(itArea));
                if (result && result->status == httplib::OK_200)
                {
                    auto clanBattleInfo = json::parse(std::string_view(result->body));
                    auto& lastInfo = *(clanBattleInfo.rbegin());
                    auto& phases = lastInfo["phases"];
                    if (phases.is_array())
                    {
                        auto ait = phases.begin();
                        for (auto&& boss : (*ait)["bosses"])
                        {
                            auto id = boss["unitId"].get<json::number_integer_t>();
                            idSet.insert(id);
                            itBossId.push_back(id);
                            itBossName.push_back(boss["name"]);
                        }
                        for (; ait != phases.end(); ait++)
                        {
                            json::array_t bossHP;
                            for (auto&& boss : (*ait)["bosses"])
                            {
                                bossHP.push_back(boss["hp"]);
                            }
                            itBossHP.push_back(bossHP);
                            itLapRange.push_back(json::array({ (*ait)["lapFrom"], (*ait)["lapTo"] }));
                        }
                        *(itLapRange.rbegin()->rbegin()) = 999;
                    }
                }
            }

            inline void fetchBossIcon(tbb::concurrent_unordered_set<json::number_integer_t>::range_type range)
            {
                httplib::Client client("https://redive.estertion.win");
                client.set_follow_location(true);
                for (auto&& id : range)
                {
                    auto filename = std::to_string(id) + ".webp";
                    auto filepath = std::filesystem::path(std::string(IconDir) + "/" + filename);
                    if (!std::filesystem::exists(filepath))
                    {
                        auto result = client.Get("/icon/unit/" + filename);
                        if (result && result->status == httplib::OK_200)
                        {
                            std::ofstream(filepath, std::ios::binary) << result->body;
                        }
                    }
                }
            }

            void updateBossData()
            {
                static std::mutex mtUpdate;
                std::lock_guard lock(mtUpdate);
                std::vector<yobot::BoosData> vBossData = {
                    {yobot::area::cn, {}, {}, {}, {}},
                    {yobot::area::tw, {}, {}, {}, {}},
                    {yobot::area::jp, {}, {}, {}, {}}
                };
                tbb::concurrent_unordered_set<json::number_integer_t> idSet;
                tbb::parallel_for(0ULL, vBossData.size(), [&](std::size_t it) {
                    fetchBossData(vBossData[it], idSet);
                });
                tbb::parallel_for(idSet.range(), [](auto&& range) {
                    fetchBossIcon(range);
                });
                ordered_json jbossData;
                for (auto&& x : vBossData)
                {
                    auto& [a, b, c, d, e] = x;
                    jbossData["boss_hp"][a] = b;
                    jbossData["lap_range"][a] = c;
                    jbossData["boss_id"][a] = d;
                    jbossData["boss_name"][a] = e;
                }
                auto& globalConfig = std::get<2>(getInstance());
                globalConfig.merge_patch(jbossData);
                std::ofstream(ConfigName) << globalConfig.dump(4) << std::endl;
            }

            std::string statistics()
            {
                auto& dbPool = std::get<1>(getInstance());
                auto db = dbPool->get();
                yobot::data::User user = {};
                auto userCount = db(
                    select(count(user.qqid))
                    .from(user)
                    .where(user.deleted == 0)
                ).begin()->count.value();
                yobot::data::ClanGroup group = {};
                auto groupCount = db(
                    select(count(group.groupId))
                    .from(group)
                    .where(group.deleted == 0)
                ).begin()->count.value();
                return std::format("用户总数：{}\n群组总数：{}", userCount, groupCount);
            }
        }

        inline RegexAction showVersion()
        {
            static const std::regex rgx("version");
            static const Action act = [](const Message& msg) {
                return VersionInfo;
            };
            return { rgx,act };
        }

        inline RegexAction showStatistics()
        {
            static const std::regex rgx("统计");
            static const Action act = [](const Message& msg) -> std::string {
                if (detail::isSuperAdmin(msg))
                {
                    return detail::statistics();
                }
                return AuthorityErrorRespone;
            };
            return { rgx,act };
        }

        inline RegexAction updateData()
        {
            static const std::regex rgx("更新数据");
            static const Action act = [](const Message& msg) {
                if (detail::isSuperAdmin(msg))
                {
                    detail::updateBossData();
                    return "更新成功";
                }
                return AuthorityErrorRespone;
            };
            return { rgx,act };
        }
    }

    namespace clanbattle {
        namespace detail {
            struct ChallengerDetail
            {
                bool is_continue;
                std::uint64_t behalf;
                bool tree;
                std::string msg;
            };

            NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChallengerDetail, is_continue, behalf, tree, msg);

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

            class Group
            {
            public:
                Group(std::uint64_t groupID) noexcept
                    : m_pool(std::get<1>(getInstance()))
                    , m_groupID(groupID)
                    , m_clanGroup()
                {

                }
                ~Group() = default;
                Group(const Group&) = delete;
                Group(Group&) = delete;
                Group(Group&&) = default;

            public:
                using status = std::tuple<std::int64_t, std::string, json, json, json, json>;
                std::optional<status> getStatus()
                {
                    std::optional<status> ret = std::nullopt;
                    auto db = m_pool->get();
                    auto raws = db(
                        select(all_of(m_clanGroup))
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                    for (const auto& raw : raws)
                    {
                        if (raw.deleted)
                        {
                            break;
                        }
                        ret = std::make_optional<status>(
                            raw.bossCycle.value(),
                            raw.gameServer.value(),
                            json::parse(raw.challengingMemberList.value(), nullptr, false),
                            json::parse(raw.subscribeList.value(), nullptr, false),
                            json::parse(raw.nowCycleBossHealth.value()),
                            json::parse(raw.nextCycleBossHealth.value())
                        );
                    }
                    return ret;
                }

                void setStatus(const std::int64_t lap, const json& thisLapBossHealth, const json& nextLapBossHealth)
                {
                    auto db = m_pool->get();
                    db(
                        update(m_clanGroup)
                        .set(
                            m_clanGroup.challengingMemberList = sqlpp::null,
                            m_clanGroup.subscribeList = sqlpp::null,
                            m_clanGroup.bossCycle = lap,
                            m_clanGroup.nowCycleBossHealth = thisLapBossHealth.dump(),
                            m_clanGroup.nextCycleBossHealth = nextLapBossHealth.dump()
                        )
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                }

                void setChallenger(int bossNum, std::uint64_t userId, const ChallengerDetail& detail)
                {
                    updateChanllengerList([=](json& list) {
                        list[std::to_string(bossNum)][std::to_string(userId)] = detail;
                    });
                }


                void removeChallenger(int bossNum, std::uint64_t userId)
                {
                    updateChanllengerList([=](json& list) {
                        if (!list[std::to_string(bossNum)][std::to_string(userId)].is_null())
                        {
                            list[std::to_string(bossNum)].erase(std::to_string(userId));
                        }
                    });
                }


                void pushChallenge(const Challenge& challenge)
                {
                    auto db = m_pool->get();
                    db(
                        insert_into(m_clanChallenge)
                        .set(
                            m_clanChallenge.behalf = challenge.behafId,
                            m_clanChallenge.bid = select(m_clanGroup.battleId).from(m_clanGroup).where(m_clanGroup.groupId == m_groupID),
                            m_clanChallenge.bossCycle = challenge.lap,
                            m_clanChallenge.bossHealthRemain = challenge.bossHP,
                            m_clanChallenge.bossNum = challenge.bossNum,
                            m_clanChallenge.challengeDamage = challenge.damage,
                            m_clanChallenge.challengePcrdate = challenge.time,
                            m_clanChallenge.challengePcrtime = challenge.time,
                            m_clanChallenge.gid = m_groupID,
                            m_clanChallenge.isContinue = (int)challenge.isContinue,
                            m_clanChallenge.message = challenge.message,
                            m_clanChallenge.qqid = challenge.userId
                        )
                    );
                }

                void popChallenge()
                {
                    auto db = m_pool->get();
                    auto lastChallenge = db(
                        select(all_of(m_clanChallenge))
                        .from(m_clanChallenge.left_outer_join(m_clanGroup).on(m_clanChallenge.bid == m_clanGroup.battleId))
                        .where(m_clanChallenge.cid == 
                            select(max(m_clanChallenge.cid))
                            .from(m_clanChallenge)
                            .where(m_clanChallenge.gid == m_groupID)
                        )
                    );
                    for (auto&& x : lastChallenge)
                    {
                        
                    }
                }

            private:
                void updateStatusInternal()
                {
                   
                }

                void updateChanllengerList(const std::function<void(json&)> &func)
                {
                    auto db = m_pool->get();
                    auto raw = db(
                        select(m_clanGroup.challengingMemberList)
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                    auto list = raw.empty() ? json{} : json::parse(raw.begin()->challengingMemberList.value());
                    func(list);
                    db(
                        update(m_clanGroup)
                        .set(m_clanGroup.challengingMemberList = list.dump())
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                }

            private:
                std::shared_ptr<DB_Pool> m_pool;
                std::uint64_t m_groupID;
                data::ClanGroup m_clanGroup;
                data::ClanChallenge m_clanChallenge;
            };

            inline std::int8_t getPhase(const std::int64_t lap, const std::string& gameServer)
            {
                char ret = 0;
                auto& globalConfig = std::get<2>(getInstance());
                auto& phaseList = globalConfig["lap_range"][gameServer].get_ref<const ordered_json::array_t&>();
                for (auto&& range : phaseList)
                {
                    if (lap >= range[0] && lap <= range[1])
                    {
                        break;
                    }
                    ret++;
                }
                return ret;
            }

            std::string toText(Group::status& status)
            {
                std::cout << status << std::endl;
                auto& globalConfig = std::get<2>(getInstance());
                const auto&& [lap, gameServer, subList, chalList, thisHPList, nextHPList] = std::move(status);
                auto phase = getPhase(lap, gameServer);
                std::string message = std::format("现在是{}阶段，第{}周目：", (char)(phase + 'A'), lap);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][phase].get_ref<const ordered_json::array_t&>();
                for (size_t i = 1; i <= 5; i++)
                {
                    auto strI = std::to_string(i);
                    bool chanllenging = !chalList.is_discarded() && !chalList[strI].is_null();
                    auto& HPList = (thisHPList[strI] == 0 ? nextHPList : thisHPList);
                    auto HP = HPList[strI].get<std::int64_t>();
                    auto fullHP = lapHPList[i - 1].get<std::int64_t>();
                    auto percent = HP * 100 / fullHP;
                    percent = ((percent == 0) ? (HP != 0) : percent);
                    auto rate = percent / 10;
					rate = ((rate == 0) ? (HP != 0) : rate);
                    auto chalStr = (chanllenging ? "有" : "无");
					message += std::format("\n{}. {:02}%【{:■<{}}{:□<{}}】{}人", i, percent, "", rate, "", 10 - rate, chalStr);
                }
                return message;
            }

            inline std::string showProgess(const std::uint64_t groud_id)
            {
                if (auto status = Group(groud_id).getStatus())
                {
                    return toText(*status);
                }
                return Group404ErrorResponse;
            }

            bool isHPListLegal(json::array_t& thisHPList, json::array_t& nextHPList, bool isOverlap)
            {
                int singleZeroCount = 0;
                int doubleZeroCount = 0;
                for (int i = 0; i < 5; i++)
                {
                    auto& thisHP = thisHPList[i];
                    auto& nextHP = nextHPList[i];
                    if (!isOverlap)
                    {
                        if (thisHP == 0)
                        {
                            singleZeroCount++;
                            doubleZeroCount += (int)(thisHP == nextHP);
                        }
                    }
                    else
                    {
                        if (thisHP == 0)
                        {
                            doubleZeroCount++;
                        }
                    }
                }
                return singleZeroCount == 5 || doubleZeroCount == 5;
            }

            inline void filterHP(json& hp, const int unit, const std::int64_t fullHP)
            {
                hp = hp * unit;
                if (hp > fullHP)
                {
                    hp = fullHP;
                }
            }

            void filterHPList(json::array_t& thisHPList, json::array_t& nextHPList, const ordered_json::array_t& lapHPList, const bool isOverlap, const int unit)
            {
                for (int i = 0; i < 5; i++)
                {
                    auto& thisHP = thisHPList[i];
                    auto& nextHP = nextHPList[i];
                    auto fullHP = lapHPList[i].get<std::int64_t>();
                    if (!isOverlap)
                    {
                        if (thisHP != 0)
                        {
                            nextHP = fullHP;
                            filterHP(thisHP, unit, fullHP);
                        }
                        else
                        {
                            filterHP(nextHP, unit, fullHP);
                        }
                    }
                    else
                    {
                        if (thisHP != 0)
                        {
                            filterHP(thisHP, unit, fullHP);
                        }
                        nextHP = 0;
                    }
                }
            }

            bool checkAndFilterProgress(const std::string& gameServer, const int lap, const int unit, json::array_t& thisHPList, json::array_t& nextHPList)
            {
                if (lap < 1 || lap > 999)
                {
                    return false;
                }

                auto &globalConfig = std::get<2>(getInstance());
                auto thisPhase = getPhase(lap, gameServer);
                auto nextPhase = getPhase(lap + 1LL, gameServer);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][thisPhase].get_ref<const ordered_json::array_t&>();
                bool isOverlap = (thisPhase != nextPhase);

                if (isHPListLegal(thisHPList, nextHPList, isOverlap))
                {
                    return false;
                }
                    
                filterHPList(thisHPList, nextHPList, lapHPList, isOverlap, unit);

                return true;
            }
            
            inline json adaptHPList(const std::ranges::range auto& list)
            {
				json ret = {};
				for (int i = 0; i < 5; i++)
				{
					auto strI = std::to_string(i + 1);
					ret[strI] = list[i];
				}
				return ret;
            }

            bool setProgress(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(\[\s*(\d+),\s*([wWkK]|万|千),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\]),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\])\s*\]$)";
                static const std::regex parten(partenStr);
                std::smatch matches;
                try
                {
                    if (std::regex_search(msg.raw_message, matches, parten))
                    {
                        int lap = std::atoi(matches[1].str().c_str());
                        int unit = 10000;
                        static const std::regex partenUnitK("[kK]|千");
                        if (std::regex_match(matches[2].str(), partenUnitK))
                        {
                            unit = 1000;
                        }
                        json::array_t thisHPList = json::parse(matches[3].str());
                        json::array_t nextHPList = json::parse(matches[4].str());
                        auto group = detail::Group(msg.group_id);
                        std::string gameServer = std::get<1>(*group.getStatus());
                        if (checkAndFilterProgress(gameServer, lap, unit, thisHPList, nextHPList))
                        {
                            group.setStatus(lap, adaptHPList(thisHPList), adaptHPList(nextHPList));
                            return true;
                        }
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << '\n';
				}
                return false;
            }

            void resetProgess(const std::uint64_t group_id)
            {
                auto group = detail::Group(group_id);
                std::string gameServer = std::get<1>(*group.getStatus());
                auto& globalConfig = std::get<2>(getInstance());
                auto phase = getPhase(1, gameServer);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][1].get_ref<const ordered_json::array_t&>();
                auto HPList = adaptHPList(lapHPList);
                group.setStatus(1, HPList, HPList);
            }
        }

        inline RegexAction showProgress()
        {
            static const std::regex rgx("进度");
			static const Action act = [](const Message& msg) {
                return std::visit([](auto&& x) -> std::string {
                    if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return detail::showProgess(x.group_id);
                    }
                    return "";
                }, msg);
		    };
            return { rgx,act };
        }

        inline RegexAction setProgress()
        {
            static const std::regex rgx(R"(^(设置|调整|修改|变更|更新|改变)进度.*)");
            static const Action act = [](const Message& msg) -> std::string {
				return std::visit([](auto&& x) -> std::string {
					if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return !detail::Group(x.group_id).getStatus()
                            ? Group404ErrorResponse 
                            : detail::setProgress(x) 
                                ? "进度已修改：\n" + detail::showProgess(x.group_id) 
                                : "格式错误";
                    }
                    return "";
                }, msg);
            };
            return { rgx,act };
        }

        inline RegexAction resetProgress()
        {
            static const std::regex rgx("重置进度");
            static const Action act = [](const Message& msg) -> std::string {
                return std::visit([](auto&& x) -> std::string {
                    if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return !detail::Group(x.group_id).getStatus()
                            ? Group404ErrorResponse
                            : (detail::resetProgess(x.group_id), "进度已重置：\n" + detail::showProgess(x.group_id));
                    }
                    return "";
                }, msg);
            };
            return { rgx,act };
        }
    }

    static Instance construct()
    {
        auto&& [botConfig, dbConfig, globalConfig] = InitConfig();
        auto dbPool = InitDatabase(dbConfig);
        auto onebotIO = twobot::BotInstance::createInstance(botConfig);
        std::filesystem::create_directory(IconDir);
        RegexActionVector regexActionVec = {
            system::showVersion(),
            system::showStatistics(),
            system::updateData(),
            clanbattle::showProgress(),
            clanbattle::setProgress(),
            clanbattle::resetProgress()
        };
        return std::make_tuple(std::move(onebotIO), dbPool, globalConfig, std::move(regexActionVec));
    }

    static Instance& getInstance()
    {
        static auto g_instance = construct();
        return g_instance;
    }

    inline std::string parallelForEachAction(const Message& msg)
    {
        auto& regexActionVec = std::get<3>(yobot::getInstance());
        auto raw_message = std::visit([](auto&& x) { return x.raw_message; }, msg);
        std::string response;
        tbb::parallel_for(0ULL, regexActionVec.size(), [&](std::size_t it) {
            auto&& [regex, action] = regexActionVec[it];
            if (std::regex_match(raw_message, regex))
            {
                response = action(msg);
            }
        });
        return response;
    }

    coro::task<> processMessageAysnc(const Message& msg)
    {
        std::string response = yobot::parallelForEachAction(msg);
        if (!response.empty())
        {
            return std::visit([&](auto&& x) -> coro::task<> {
                auto& onebotIO = std::get<0>(yobot::getInstance());
                auto apiSet = onebotIO->getApiSet(x.self_id);
                if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                {
                    static const auto sendGroupMsg = [](twobot::ApiSet apiSet, std::uint64_t groud_id, std::string message) -> coro::task<> {
                        co_await apiSet.sendGroupMsg(groud_id, message);
                    };
                    return sendGroupMsg(apiSet, x.group_id, response);
                }
                if constexpr (std::is_convertible_v<decltype(x), PrivateMsg>)
                {
                    static const auto sendPrivateMsg = [](twobot::ApiSet apiSet, std::uint64_t user_id, std::string message) -> coro::task<> {
                        co_await apiSet.sendPrivateMsg(user_id, message);
                    };
                    return sendPrivateMsg(apiSet, x.user_id, response);
                }
            }, msg);
        }
        return []() -> coro::task<> { co_return; }();
    }

    void initialize()
    {
        auto& onebotIO = std::get<0>(yobot::getInstance());
        onebotIO->onEvent<GroupMsg>(processMessageAysnc);
        onebotIO->onEvent<PrivateMsg>(processMessageAysnc);
        onebotIO->onEvent<ConnectEvent>([](const ConnectEvent& msg) -> coro::task<> {
            std::cout << "websocket已连接！ID: " << msg.self_id << std::endl;
            co_return;
        });
    }

    void start()
    {
        auto& onebotIO = std::get<0>(getInstance());
        auto& globalConfig = std::get<2>(getInstance());
        std::cout << "Start listening:" << globalConfig["port"] << std::endl;
        onebotIO->start();
    }
}

void test()
{
    try {
        const std::regex re(R"(\[\s*(\d+),\s*([wWkK]|万|千),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\]),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\])\s*\]$)");
        std::smatch matches;
        std::string str = "[30,万,[2000,2000,3000,4000,50000],[1,5,3,4,5]]";
        if (std::regex_search(str, matches, re))
        {
            for (auto&& x : matches)
            {
                std::cout << x.str() << std::endl;
            }
        }
    }
    catch (std::exception e) {
        std::cout << e.what() << std::endl;
    }
}

int main(int argc, char** args)
{
	static volatile int MI_VERSION = mi_version();
    yobot::initialize();
    yobot::start();
    //test();
    return 0;
}
