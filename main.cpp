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
constexpr auto FormatErrorResponse = "格式错误";

namespace yobot {
    using twobot::Event::GroupMsg;
    using twobot::Event::PrivateMsg;
    using twobot::Event::ConnectEvent;
    using DB_Pool = sqlpp::sqlite3::connection_pool;
    using DB_Config = sqlpp::sqlite3::connection_config;
    using DB_Connection = sqlpp::sqlite3::pooled_connection;
    using Message = std::variant<GroupMsg, PrivateMsg>;
    using Action = std::function<std::string(const Message&)>;
    using GroupAction = std::function<std::string(const GroupMsg&)>;
    using RegexAction = std::pair<const std::regex&, const Action&>;
    using RegexActionVector = std::vector<RegexAction>;
    using GroupMutexMap = tbb::concurrent_unordered_map<std::uint64_t, coro::mutex>;
    using Instance = std::tuple<std::unique_ptr<twobot::BotInstance>, std::shared_ptr<DB_Pool>, ordered_json, RegexActionVector, GroupMutexMap>;
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

    namespace tools {
        inline std::smatch regexSearch(const std::regex& parten, const std::string& rawStr)
        {
            std::smatch matches;
            std::regex_search(rawStr, matches, parten);
            return matches;
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

            struct status
            {
                std::int64_t lap;
                std::string gameServer;
                json challengerList;
                json subscribeList;
                json thisLapHPList;
                json nextLapHPList;
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

            inline void recordChallenge(const Challenge& challenge, status& s)
            {
                static const json zeroHPList = { {"1", 0}, { "2",0 }, { "3",0 }, { "4",0 }, { "5",0 } };
                if (challenge.damage == 0)
                {
                    return;
                }
                auto strI = std::to_string(challenge.bossNum);
                if (s.nextLapHPList[strI] != 0)
                {
                    if (s.thisLapHPList[strI] != 0)
                    {
                        s.thisLapHPList[strI] = challenge.bossHP;
                    }
                    else
                    {
                        s.nextLapHPList[strI] = challenge.bossHP;
                    }
                }
                if (challenge.bossHP == 0)
                {
                    if (s.thisLapHPList == zeroHPList)
                    {
                        ++s.lap;
                        auto thisPhase = getPhase(s.lap, s.gameServer);
                        auto nextPhase = getPhase(s.lap + 1, s.gameServer);
                        auto& globalConfig = std::get<2>(getInstance());
                        s.thisLapHPList = s.nextLapHPList;
                        if (thisPhase != nextPhase)
                        {
                            s.nextLapHPList = zeroHPList;
                        }
                        else
                        {
                            auto lapHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][thisPhase].get_ref<const ordered_json::array_t&>());
                            if (s.nextLapHPList == zeroHPList)
                            {
                                s.thisLapHPList = lapHPList;
                            }
                            s.nextLapHPList = lapHPList;
                        }
                    }
                }
            }

            inline void revokeChallenge(const Challenge& challenge, status& s)
            {
                static const json zeroHPList = { {"1", 0}, { "2",0 }, { "3",0 }, { "4",0 }, { "5",0 } };
                auto strI = std::to_string(challenge.bossNum);
                if (challenge.bossHP == 0) 
                {
                    auto thisPhase = getPhase(s.lap, s.gameServer);
                    auto lastPhase = getPhase(s.lap - 1, s.gameServer);
                    auto& globalConfig = std::get<2>(getInstance());
                    auto thisLapFullHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][thisPhase].get_ref<const ordered_json::array_t&>());
                    if (s.thisLapHPList == thisLapFullHPList)
                    {
                        --s.lap;
                        if (thisPhase != lastPhase)
                        {
                            if (thisPhase > 1)
                            {
                                auto lastLapFullHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][lastPhase].get_ref<const ordered_json::array_t&>());
                                s.nextLapHPList = zeroHPList;
                            }
                        }
                        else
                        {
                            s.nextLapHPList = thisLapFullHPList;
                        }
                        s.thisLapHPList = zeroHPList;
                    }
                }
                s.thisLapHPList[strI] = challenge.bossHP + challenge.damage;
            }

            inline std::time_t toDateOnly(const std::time_t time)
            {
                auto timePoint = std::chrono::system_clock::from_time_t(time);
                auto timeDays = std::chrono::floor<std::chrono::days>(timePoint);
                return std::chrono::system_clock::to_time_t(timeDays);
            }

            inline std::time_t toTimeOnly(const std::time_t time)
            {
                auto timePoint = std::chrono::system_clock::from_time_t(time);
                auto timeOnly = timePoint - std::chrono::floor<std::chrono::days>(timePoint);
                return std::chrono::duration_cast<std::chrono::seconds>(timeOnly).count();
            }

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
                explicit operator bool() const
                {
                    auto r = m_pool->get()(
                        select(m_clanGroup.deleted)
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                    return r.empty() ? false : !r.begin()->deleted.value();
                }

            public:
                status getStatus()
                {
                    auto db = m_pool->get();
                    auto raws = db(
                        select(all_of(m_clanGroup))
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID)
                    );
                    auto &raw = *raws.begin();
                    return {
                        raw.bossCycle.value(),
                        raw.gameServer.value(),
                        json::parse(raw.challengingMemberList.value(), nullptr, false),
                        json::parse(raw.subscribeList.value(), nullptr, false),
                        json::parse(raw.nowCycleBossHealth.value()),
                        json::parse(raw.nextCycleBossHealth.value())
                    };
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

                std::string getGameServer()
                {
                    return m_pool->get()(
                        select(m_clanGroup.gameServer)
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID)
                    ).begin()->gameServer.value();
                }

                void setChallenger(std::string_view bossNum, std::uint64_t userId, const ChallengerDetail& detail)
                {
                    updateChanllengerList([=](json& list) {
                        list[bossNum][std::to_string(userId)] = detail;
                    });
                }


                void removeChallenger(std::string_view bossNum, std::uint64_t userId)
                {
                    updateChanllengerList([=](json& list) {
                        if (!list[bossNum].is_null() && !list[bossNum][std::to_string(userId)].is_null())
                        {
                            list[bossNum].erase(std::to_string(userId));
                        }
                    });
                }

                void pushChallenge(const Challenge& challenge)
                {
                    auto db = m_pool->get();
                    auto bid = 
                        select(m_clanGroup.battleId)
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID);
                    db(
                        insert_into(m_clanChallenge)
                        .set(
                            m_clanChallenge.behalf = challenge.behafId,
                            m_clanChallenge.bid = bid,
                            m_clanChallenge.bossCycle = challenge.lap,
                            m_clanChallenge.bossHealthRemain = challenge.bossHP,
                            m_clanChallenge.bossNum = challenge.bossNum,
                            m_clanChallenge.challengeDamage = challenge.damage,
                            m_clanChallenge.challengePcrdate = toDateOnly(challenge.time),
                            m_clanChallenge.challengePcrtime = toTimeOnly(challenge.time),
                            m_clanChallenge.gid = m_groupID,
                            m_clanChallenge.isContinue = (int)challenge.isContinue,
                            m_clanChallenge.message = challenge.message,
                            m_clanChallenge.qqid = challenge.userId
                        )
                    );
                    
                    updateStatusInternal([&](status &s) {
                        recordChallenge(challenge, s);
                    });
                }

                void popChallenge()
                {
                    auto db = m_pool->get();
                    auto challengeJoinGroup = 
                        m_clanChallenge
                        .left_outer_join(m_clanGroup)
                        .on(m_clanChallenge.bid == m_clanGroup.battleId);
                    auto maxCid = 
                        select(max(m_clanChallenge.cid))
                        .from(m_clanChallenge)
                        .where(m_clanChallenge.gid == m_groupID);
                    auto raws = db(
                        select(all_of(m_clanChallenge))
                        .from(challengeJoinGroup)
                        .where(m_clanChallenge.cid == maxCid)
                    );
                    Challenge chal;
                    for (auto&& raw : raws)
                    {
                        chal.behafId = raw.behalf;
                        chal.bossHP = raw.bossHealthRemain;
                        chal.bossNum = (int)raw.bossNum;
                        chal.damage = raw.challengeDamage;
                        chal.time = raw.challengePcrdate + raw.challengePcrtime;
                        chal.isContinue = raw.isContinue;
                        chal.lap = (int)raw.bossCycle;
                        chal.message = raw.message;
                        chal.userId = raw.qqid;
                    }

                    updateStatusInternal([&](status& s) {
                        revokeChallenge(chal, s);
                    });
                    db(remove_from(m_clanChallenge).where(m_clanChallenge.cid == maxCid));
                }

                void clearChallenge()
                {
                    auto db = m_pool->get();
                    auto bid = 
                        select(m_clanGroup.battleId)
                        .from(m_clanGroup)
                        .where(m_clanGroup.groupId == m_groupID);
                    db(
                        remove_from(m_clanChallenge)
                        .where(m_clanChallenge.gid == m_groupID and m_clanChallenge.bid == bid)
                    );
                }

            private:
                void updateStatusInternal(const std::function<void(status&)>& func)
                {
                    auto s = getStatus();
                    func(s);
                    auto db = m_pool->get();
                    db(
                        update(m_clanGroup)
                        .set(
                            m_clanGroup.bossCycle = s.lap,
                            m_clanGroup.challengingMemberList = s.challengerList.dump(),
                            m_clanGroup.subscribeList = s.subscribeList.dump(),
                            m_clanGroup.gameServer = s.gameServer,
                            m_clanGroup.nowCycleBossHealth = s.thisLapHPList.dump(),
                            m_clanGroup.nextCycleBossHealth = s.nextLapHPList.dump()
                        )
                        .where(m_clanGroup.groupId == m_groupID)
                    );
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

            std::string toText(const status& status)
            {
                auto& globalConfig = std::get<2>(getInstance());
                auto& [lap, gameServer, subList, chalList, thisHPList, nextHPList] = status;
                auto phase = getPhase(lap, gameServer);
                std::string message = std::format("现在是{}阶段，第{}周目：", (char)(phase + 'A'), lap);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][phase].get_ref<const ordered_json::array_t&>();
                for (size_t i = 1; i <= 5; i++)
                {
                    auto strI = std::to_string(i);
                    bool chanllenging = !chalList.is_discarded() && !chalList[strI].empty();
                    auto& HPList = (thisHPList[strI] == 0 ? nextHPList : thisHPList);
                    auto HP = HPList[strI].get<std::int64_t>();
                    auto fullHP = lapHPList[i - 1].get<std::int64_t>();
                    auto percent = HP * 100 / fullHP;
                    percent = ((percent == 0) ? (HP != 0) : percent);
                    auto rate = percent / 10;
					rate = ((rate == 0) ? (HP != 0) : rate);
                    auto chalStr = (chanllenging ? "有" : "无");
					message += std::format("\n{}.【{:■<{}}{:□<{}}】{:02}% {}人", i, "", rate, "", 10 - rate, percent, chalStr);
                }
                return message;
            }

            inline std::string showProgess(const GroupMsg& msg)
            {
                return toText(Group(msg.group_id).getStatus());
            }

            inline bool isFilledWithZero(json::array_t& HPList)
            {
                int zeroCount = 0;
                for (int i = 0; i < 5; i++)
                {
                    if (HPList[i] == 0)
                    {
                        zeroCount++;
                    }
                }
                return zeroCount == 5;
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

                if (isFilledWithZero(thisHPList))
                {
                    return false;
                }
                    
                filterHPList(thisHPList, nextHPList, lapHPList, isOverlap, unit);

                return true;
            }

            bool setProgress(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(\[\s*(\d+),\s*([wWkK]|万|千),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\]),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\])\s*\]$)";
                static const std::regex parten(partenStr);
                try
                {
                    std::smatch matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
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
                        auto gameServer = group.getGameServer();
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

            void resetProgess(const GroupMsg& msg)
            {
                auto group = detail::Group(msg.group_id);
                auto gameServer = group.getGameServer();
                auto& globalConfig = std::get<2>(getInstance());
                auto phase = getPhase(1, gameServer);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][1].get_ref<const ordered_json::array_t&>();
                auto HPList = adaptHPList(lapHPList);
                group.setStatus(1, HPList, HPList);
                group.clearChallenge();
            }

			inline bool isBossAlive(const status& s, const std::string& bossNum)
            {
                return s.thisLapHPList[bossNum] != 0 || s.nextLapHPList[bossNum] != 0;
            }

            inline std::string getApplicationBossNum(const status& s, const std::uint64_t user_id)
            {
                for (auto it = s.challengerList.begin(); it != s.challengerList.end(); it++)
                {
                    if (it.value().contains(std::to_string(user_id)))
                    {
                        return it.key();
                    }
                }
                return {};
            }

            inline bool isApplied(const status& s, const std::uint64_t user_id)
            {
                return !getApplicationBossNum(s, user_id).empty();
            }

            bool applyForChallenge(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(([1-5])\s*(:|：|\s)?\s*(\S*)$)";
                static const std::regex parten(partenStr);
                try
                {
                    std::smatch matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
                    {
                        auto bossNum = matches[1].str();
                        auto group = Group(msg.group_id);
                        auto s = group.getStatus();
						bool isMatched = ((matches[2].length() != 0) || (matches[3].length() == 0));
                        if (isMatched && isBossAlive(s, bossNum) && !isApplied(s, msg.user_id))
                        {
                            bool isContinue = (msg.raw_message.find("补") != std::string::npos);
                            auto message = matches[3].str();
                            group.setChallenger(bossNum, msg.user_id, {
                                .is_continue = isContinue,
                                .msg = message
                            });
                            return true;
                        }
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << std::endl;
                }
                return false;
            }

            bool cancelApplyForChallenge(const GroupMsg& msg)
            {
                auto group = Group(msg.group_id);
                auto s = group.getStatus();
                auto bossNum = getApplicationBossNum(s, msg.user_id);
                if (!bossNum.empty())
                {
                    group.removeChallenger(bossNum, msg.user_id);
                    return true;
                }
                return false;
            }

            inline int toUnit(const std::string& unitStr)
            {
                static const std::regex partenUnitK("[kK]|千");
                static const std::regex partenUnitW("[wW]|万");
                int unit = 1;
                if (std::regex_match(unitStr, partenUnitK))
                {
                    unit = 1000;
                }
                else if (std::regex_match(unitStr, partenUnitW))
                {
                    unit = 10000;
                }
                return unit;
            }

            inline std::string checkChallengeInput(const bool isKilled, const std::string& applyNum, const std::smatch& matches)
            {
                if (isKilled)
                {
                    if (applyNum.empty() && matches[1].length() == 0 && matches[3].length() == 0)
                    {
                        return "未申请且没有指定boss";
                    }
                    auto firstChar = matches[3].str().c_str()[0];
                    if (firstChar == '-' || firstChar > '5' || matches[4].length() != 0)
                    {
                        return FormatErrorResponse;
                    }
                }
                else
                {
                    if (applyNum.empty() && (matches[1].length() == 0 || matches[2].length() == 0))
                    {
                        return "未申请且没有指定boss";
                    }
                }
                return {};
            }

            inline std::string getChallegeBossNum(const bool isKilled,const std::string& applyNum, const std::smatch& matches)
            {
                auto bossNum = applyNum;
                if (isKilled)
                {
                    if (matches[1].length() != 0 && matches[2].length() == 0)
                    {
                        bossNum = matches[1];
                    }
                    else
                    {
                        if (matches[3].length() != 0)
                        {
                            bossNum = matches[3];
                        }
                    }
                }
                else
                {
                    if (matches[1].length() != 0 && matches[2].length() != 0)
                    {
                        bossNum = matches[1];
                    }
                }
                return bossNum;
            }

            inline std::int64_t getChallengeDamage(const bool isKilled, const std::int64_t bossHP, const std::smatch& matches)
            {
                if (isKilled)
                {
                    return bossHP;
                }
                else
                {
                    int unit = toUnit(matches[4]);
                    auto damageStr = matches[3].str();
                    if (matches[2].length() == 0)
                    {
                        damageStr = matches[1].str() + damageStr;
                    }
                    return std::atoll(damageStr.c_str()) * unit;
                }
            }

            inline std::string processReportChanllenge(const GroupMsg& msg, const std::smatch& matches)
            {
                auto group = Group(msg.group_id);
                auto s = group.getStatus();
                auto applyNum = getApplicationBossNum(s, msg.user_id);
                bool isKilled = (msg.raw_message.find("尾") != std::string::npos);
                auto checkResult = checkChallengeInput(isKilled, applyNum, matches);
                if (!checkResult.empty())
                {
                    return checkResult;
                }

                auto bossNum = getChallegeBossNum(isKilled, applyNum, matches);
                bool isNext = (s.thisLapHPList[bossNum] == 0);
                std::int64_t bossHP = (isNext ? s.nextLapHPList[bossNum] : s.thisLapHPList[bossNum]);
                std::int64_t damage = getChallengeDamage(isKilled, bossHP, matches);

                if (!isKilled && bossHP <= damage)
                {
                    return "伤害超出剩余血量，请使用“尾刀”指令";
                }
                bossHP = bossHP - damage;

                bool isContinue = (msg.raw_message.find("补") != std::string::npos);
                group.pushChallenge({
                    .userId = msg.user_id,
                    .time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                    .lap = (int)s.lap + (int)isNext,
                    .bossNum = std::atoi(bossNum.c_str()),
                    .bossHP = bossHP,
                    .damage = damage,
                    .isContinue = isContinue
                });
                if (!applyNum.empty())
                {
                    group.removeChallenger(applyNum, msg.user_id);
                }                
                std::string result = isKilled
                    ? std::format(std::locale(TargetLocaleName), "对{}王造成伤害{:L}，并击败", bossNum, damage)
                    : std::format(std::locale(TargetLocaleName), "对{}王造成伤害{:L}，剩余血量{:L}", bossNum, damage, bossHP);
                return result + "\n" + toText(group.getStatus());
            }

            std::string reportChallenge(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(([1-5])?\s*(\s)?\s*(\d+)?([wWkK]|万|千)?$)";
                static const std::regex parten(partenStr);
                try
                {
                    auto matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
                    {
                        return processReportChanllenge(msg, matches);
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << std::endl;
                }
                return FormatErrorResponse;
            }

            std::string cancelReportChallenge(const GroupMsg& msg)
            {
                auto group = Group(msg.group_id);
                group.popChallenge();
                return "撤销完成\n" + toText(group.getStatus());
            }
        }

        inline Action groupAction(GroupAction act)
        {
            return [=](const Message& msg) {
                return std::visit([=](auto&& x) -> std::string {
                    if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return !detail::Group(x.group_id)
                            ? Group404ErrorResponse
                            : act(x);
                    }
                    return {};
                }, msg);
            };
        }

        inline RegexAction showProgress()
        {
            static const std::regex rgx("进度");
            static const Action act = groupAction(detail::showProgess);
            return { rgx,act };
        }

        inline RegexAction setProgress()
        {
            static const std::regex rgx(R"(^(设置|调整|修改|变更|更新|改变)进度.*)");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::setProgress(msg)
                    ? "进度已修改：\n" + detail::showProgess(msg)
                    : FormatErrorResponse;
            });
            return { rgx,act };
        }

        inline RegexAction resetProgress()
        {
            static const std::regex rgx("重置进度");
            static const Action act = groupAction([](const GroupMsg& msg) {
                detail::resetProgess(msg);
                return "进度已重置：\n" + detail::showProgess(msg);
            });
            return { rgx,act };
        }

        inline RegexAction applyForChallenge()
        {
            static const std::regex rgx("^申请(出|补|出补)刀.*");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::applyForChallenge(msg)
                    ? "申请成功：\n" + detail::showProgess(msg)
                    : "申请失败，格式或状态错误";
            });
            return { rgx, act };
        }

        inline RegexAction cancelApplyForChallenge()
        {
            static const std::regex rgx("^(取消|撤销)申请");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::cancelApplyForChallenge(msg)
                    ? "取消申请成功：\n" + detail::showProgess(msg)
                    : "没有申请记录";
            });
            return { rgx, act };
        }

        inline RegexAction reportChallenge()
        {
            static const std::regex rgx("^(报|尾|报尾|补|补尾)刀.*");
            static const Action act = groupAction(detail::reportChallenge);
            return { rgx, act };
        }

        inline RegexAction cancelReportChallenge()
        {
            static const std::regex rgx("撤销报刀");
            static const Action act = groupAction(detail::cancelReportChallenge);
            return { rgx, act };
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
            clanbattle::resetProgress(),
            clanbattle::applyForChallenge(),
            clanbattle::cancelApplyForChallenge(),
            clanbattle::reportChallenge(),
            clanbattle::cancelReportChallenge()
        };
        return std::make_tuple(std::move(onebotIO), dbPool, globalConfig, std::move(regexActionVec), GroupMutexMap{});
    }

    static Instance& getInstance()
    {
        static auto g_instance = construct();
        return g_instance;
    }

    inline coro::task<> processGroupMsgAsync(twobot::ApiSet apiSet, GroupMsg msg, Action action)
    {
        auto& groupMutexMap = std::get<4>(yobot::getInstance());
        auto response = std::string{};
        {
            auto lock = co_await groupMutexMap[msg.group_id].scoped_lock();
            response = action(msg);
        }
        if (!response.empty())
        {
            co_await apiSet.sendGroupMsg(msg.group_id, response);
        }
    }

    inline coro::task<> processPrivateMsgAsync(twobot::ApiSet apiSet, PrivateMsg msg, Action action)
    {
        auto response = action(msg);
        if (!response.empty())
        {
            co_await apiSet.sendPrivateMsg(msg.user_id, response);
        }
    }

    inline coro::task<> processMessageAysnc(const Message& msg)
    {
        auto& regexActionVec = std::get<3>(yobot::getInstance());
        auto raw_message = std::visit([](auto&& x) { return x.raw_message; }, msg);
        auto ret = []() -> coro::task<> { co_return; }();
        tbb::parallel_for(0ULL, regexActionVec.size(), [&](std::size_t it) {
            auto&& [regex, action] = regexActionVec[it];
            if (std::regex_match(raw_message, regex))
            {
                ret = std::visit([&](auto&& x) -> coro::task<> {
                    auto apiSet = std::get<0>(yobot::getInstance())->getApiSet(x.self_id);
                    if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return processGroupMsgAsync(apiSet, x, action);
                    }
                    if constexpr (std::is_convertible_v<decltype(x), PrivateMsg>)
                    {
                        return processPrivateMsgAsync(apiSet, x, action);
                    }
                }, msg);
            }
        });
        return ret;
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

//void test()
//{
//    long long num = 123456789LL;
//    std::cout << std::format(std::locale(TargetLocaleName), "{:L}\n", num) << std::endl;;
//}

int main(int argc, char** args)
{
	static volatile int MI_VERSION = mi_version();
    yobot::initialize();
    yobot::start();
    //test();
    return 0;
}
