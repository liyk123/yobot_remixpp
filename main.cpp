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

namespace yobot {
    using twobot::Event::GroupMsg;
    using twobot::Event::PrivateMsg;
    using twobot::Event::ConnectEvent;
    using DB_Pool = sqlpp::sqlite3::connection_pool;
    using DB_Config = sqlpp::sqlite3::connection_config;
    using Message = std::variant<GroupMsg, PrivateMsg>;
    using Action = std::function<std::string(const Message&)>;
    using RegexAction = std::pair<std::regex, Action>;
    using RegexActionVector = std::vector<RegexAction>;
    using Instance = std::tuple<std::unique_ptr<twobot::BotInstance>, std::shared_ptr<DB_Pool>, ordered_json, RegexActionVector>;
    using BoosData = std::tuple<std::string_view, json::array_t, json::array_t, json::array_t, json::array_t>;

    static Instance& getInstance();
    std::string statistics(const std::shared_ptr<DB_Pool>& dbPool);
    void updateBossData(ordered_json& globalConfig);

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

        inline auto showVersion()
        {
            return RegexAction{
                "version",
                [](const Message& msg) {
                    return VersionInfo;
                }
            };
        }

        inline auto showStatistics()
        {
            return RegexAction{
                "统计",
                [](const Message& msg) -> std::string {
                    if (isSuperAdmin(msg))
                    {
                        auto& dbPool = std::get<1>(getInstance());
                        return statistics(dbPool);
                    }
                    return AuthorityErrorRespone;
                }
            };
        }

        inline auto updateData()
        {
            return RegexAction{
                "更新数据",
                [](const Message& msg) {
                    static std::mutex mtUpdate;
					if (isSuperAdmin(msg))
                    {
                        std::lock_guard lock(mtUpdate);
                        auto& globalConfig = std::get<2>(getInstance());
                        updateBossData(globalConfig);
                        return "更新成功";
                    }
                    return AuthorityErrorRespone;
                }
            };
        }
    }

    namespace clanbattle {
        class Group
        {
        public:
            Group(const std::shared_ptr<DB_Pool>& pool, std::uint64_t groupID) noexcept
                : m_pool(pool)
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

            bool setStatus(const std::int64_t& lap, const json& thisLapBossHealth, const json& nextLapBossHealth)
            {
                bool ret = false;
                auto db = m_pool->get();
                if (isStatusLegal(lap, thisLapBossHealth, nextLapBossHealth))
                {
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
                    ret = true;
                }
                return ret;
            }

        private:
            void updateStatusInternal()
            {

            }

            bool isStatusLegal(const std::int64_t& bossCycle, const json& nowCycleBossHealth, const json& nextCycleBossHealth)
            {

                return false;
            }

        private:
            std::shared_ptr<DB_Pool> m_pool;
            std::uint64_t m_groupID;
            data::ClanGroup m_clanGroup;
        };

        inline std::int8_t getPhase(const std::int64_t lap, const std::string& gameServer, const ordered_json& globalConfig)
        {
            char ret = 0;
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

        std::string renderStatusText(Group::status& status, const ordered_json& globalConfig)
        {
            std::cout << status << std::endl;
            const auto&& [lap, gameServer, subList, chalList, thisHPList, nextHPList] = std::move(status);
            auto phase = getPhase(lap, gameServer, globalConfig);
            std::string message = std::format("现在是{}阶段，第{}周目：", (char)(phase + 'A'), lap);
            auto& lapHPList = globalConfig["boss_hp"][gameServer][phase].get_ref<const ordered_json::array_t&>();
            for (size_t i = 1; i <= 5; i++)
            {
                auto strI = std::to_string(i);
                bool chanllenging = !chalList.is_discarded() && !chalList[strI].is_null();
                auto& HPList = (thisHPList[strI] == 0 ? nextHPList : thisHPList);
                auto HP = HPList[strI].get<std::int64_t>();
                auto fullHP = lapHPList[i - 1].get<std::int64_t>();
                std::int64_t rate = HP * 10 / fullHP + (HP == 0);
                auto chalStr = (chanllenging ? "有" : "无");
                message += std::format("\n{}.【{:■<{}}{:□<{}}】{}人", i, "", rate, "", 10 - rate, chalStr);
            }
            return message;
        }

        inline auto showProgress()
        {
            return RegexAction{
                "进度",
                [](const Message& msg) {
					return std::visit([](auto&& x) -> std::string {
                        if constexpr (std::is_convertible_v<decltype(x),GroupMsg>)
                        {
                            auto groupId = x.group_id;
                            auto& dbPool = std::get<1>(getInstance());
                            auto& globalConfig = std::get<2>(getInstance());
                            auto group = Group(dbPool, groupId);
                            auto status = group.getStatus();
                            if (status)
                            {
                            	return renderStatusText(*status, globalConfig);
                            }
                            return "未检测到数据，请先创建公会！";
                        }
                        return "";
                    }, msg);
                }
            };
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
            clanbattle::showProgress()
        };
        return std::make_tuple(std::move(onebotIO), dbPool, globalConfig, std::move(regexActionVec));
    }

    static Instance& getInstance()
    {
        static auto g_instance = construct();
        return g_instance;
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

    void updateBossData(ordered_json& globalConfig)
    {
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
            jbossData["boss_HP"][a] = b;
            jbossData["lap_range"][a] = c;
            jbossData["boss_id"][a] = d;
            jbossData["boss_name"][a] = e;
        }
        globalConfig.merge_patch(jbossData);
        std::ofstream(ConfigName) << globalConfig.dump(4) << std::endl;
    }

    std::string statistics(const std::shared_ptr<DB_Pool> &dbPool)
    {
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
                    return [](twobot::ApiSet apiSet, std::uint64_t groud_id, std::string message) -> coro::task<> {
                        co_await apiSet.sendGroupMsg(groud_id, message);
                        co_return;
                    }(apiSet, x.group_id, response);
                }
                if constexpr (std::is_convertible_v<decltype(x), PrivateMsg>)
                {
                    return [](twobot::ApiSet apiSet, std::uint64_t user_id, std::string message) -> coro::task<> {
                        co_await apiSet.sendPrivateMsg(user_id, message);
                        co_return;
                    }(apiSet, x.user_id, response);
                }
            }, msg);
        }
        return []() -> coro::task<> { co_return; }();
    }

    void initialize()
    {
        auto& onebotIO = std::get<0>(yobot::getInstance());
        onebotIO->onEvent<GroupMsg>([](const GroupMsg& msg) -> coro::task<> {
            co_return co_await processMessageAysnc(msg);
        });

        onebotIO->onEvent<PrivateMsg>([](const PrivateMsg& msg) -> coro::task<> {
            co_return co_await processMessageAysnc(msg);
        });

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

int main(int argc, char** args)
{
	static volatile int MI_VERSION = mi_version();
    yobot::initialize();
    yobot::start();
    return 0;
}
