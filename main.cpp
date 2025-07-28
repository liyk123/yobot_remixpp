#include <mimalloc-new-delete.h>
#include <mimalloc-override.h>
#include <iostream>
#include <fstream>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <httplib.h>
#include <tbb/tbb.h>
#include "yobotdata_new.h"
#include "default_config.h"
#include "yobotdata_new.sql.h"

#define CONNECT_LITERAL(X,Y) X##Y

using namespace nlohmann::json_literals;
using namespace twobot::Event;
using json = nlohmann::basic_json<std::map, std::vector, std::string, bool, int64_t, uint64_t, double, mi_stl_allocator>;
using ordered_json = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, int64_t, uint64_t, double, mi_stl_allocator>;

constexpr auto versionInfo = "Branch: " GIT_BRANCH "\nCommit: " GIT_VERSION "\nDate: " GIT_DATE;
constexpr auto targetLocaleName = "zh_CN.UTF-8";
constexpr auto cLocaleName = "C";
constexpr auto dbName = "yobotdata_new.db";
constexpr auto configName = "yobot_config.json";

template <typename T, class... Args>
std::shared_ptr<T> make_mi_shared(Args&&... args)
{
    return std::allocate_shared<T>(mi_stl_allocator<T>(), std::forward<Args>(args)...);
}

namespace yobot {
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
        template <typename... Args>
        inline auto make_optional_tuple(Args&&... args)
        {
            return std::make_optional(std::make_tuple(std::forward<Args>(args)...));
        }
    }

    namespace area {
        constexpr std::string_view cn = "cn";
        constexpr std::string_view tw = "tw";
        constexpr std::string_view jp = "jp";
    }

    using BoosData = std::tuple<std::string_view, json::array_t, json::array_t, json::array_t>;

    using DB_Pool = sqlpp::sqlite3::connection_pool;
    using DB_Config = sqlpp::sqlite3::connection_config;

    class Group
    {
    public:
        Group(std::shared_ptr<DB_Pool> pool, std::uint64_t groupID) noexcept
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
                sqlpp::select(sqlpp::all_of(m_clanGroup))
                .from(m_clanGroup)
                .where(m_clanGroup.groupId == m_groupID)
            );
            for (const auto &raw : raws)
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
                    sqlpp::update(m_clanGroup)
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

    inline void fetchBossData(BoosData& bossData)
    {
        auto&& [itArea, itBossHP, itLapRange, itBossId] = bossData;
        httplib::Client client("https://pcr.satroki.tech");
        //"https://pcr.satroki.tech/icon/unit/";
        auto result = client.Get("/api/Quest/GetClanBattleInfos?s=" + std::string(itArea));
        if (result && result->status == 200)
        {
            auto clanBattleInfo = json::parse(std::string_view(result->body));
            auto& lastInfo = *(clanBattleInfo.rbegin());
            auto& phases = lastInfo["phases"];
            if (phases.is_array())
            {
                auto ait = phases.begin();
                for (auto&& boss : (*ait)["bosses"])
                {
                    itBossId.push_back(boss["unitId"]);
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

    void updateBossData(ordered_json& globalConfig)
    {
		std::vector<yobot::BoosData> vBossData = {
	        {yobot::area::cn, {}, {}, {}},
	        {yobot::area::tw, {}, {}, {}},
	        {yobot::area::jp, {}, {}, {}}
		};
        tbb::parallel_for(0ULL, vBossData.size(), [&](std::size_t it) {
            yobot::fetchBossData(vBossData[it]);
            });
        ordered_json jbossData;
        for (auto&& x : vBossData)
        {
            auto& [a, b, c, d] = x;
            jbossData["boss_HP"][a] = b;
            jbossData["lap_range"][a] = c;
            jbossData["boss_id"][a] = d;
        }
        globalConfig.merge_patch(jbossData);
        std::ofstream(configName) << globalConfig.dump(4) << std::endl;
    }
}

inline auto InitConfig() noexcept
{   
#ifdef _WIN32
    system("chcp 65001 && cls");
#endif
    std::cout << "Initializing...\n";
    std::setlocale(LC_ALL, targetLocaleName);
    std::setlocale(LC_NUMERIC, cLocaleName);
    std::locale::global(std::locale(targetLocaleName));
    std::locale::global(std::locale(cLocaleName, std::locale::numeric));
    auto dbConfig = make_mi_shared<yobot::DB_Config>(dbName, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (!std::filesystem::exists(configName) || std::filesystem::file_size(configName) == 0)
    {
        std::ofstream(configName) << DEFAULT_CONFIG;
    }
    auto globalConfig = ordered_json::parse(std::ifstream(configName));
    auto accessToken = globalConfig["access_token"].get<std::string>();
    twobot::Config botConfig = {
        .host = "127.0.0.1",
        .api_port = 5700,
        .ws_port = globalConfig["port"],
        .token = (accessToken.empty() ? std::nullopt : std::make_optional(accessToken))
    };
    return std::make_tuple(botConfig, dbConfig, globalConfig);
}

std::shared_ptr<yobot::DB_Pool> InitDatabase(const std::shared_ptr<yobot::DB_Config> &dbConfig) noexcept
{
    auto dbPool = make_mi_shared<yobot::DB_Pool>(dbConfig, 2);
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

int main(int argc, char** args)
{
    auto [botConfig, dbConfig, globalConfig] = InitConfig();
    auto dbPool = InitDatabase(dbConfig);
    auto instance = twobot::BotInstance::createInstance(botConfig);

    instance->onEvent<GroupMsg>([&instance, &dbPool, &globalConfig](const GroupMsg& msg) -> coro::task<> {
        auto sessionSet = instance->getApiSet(msg.self_id);

        if (msg.raw_message == "version")
        {
            co_await sessionSet.sendGroupMsg(msg.group_id, versionInfo);
        }
        if (msg.raw_message == "进度")
        {
            auto group = yobot::Group(dbPool, msg.group_id);
            auto status = group.getStatus();
            std::string message = "未检测到数据，请先创建公会！";
            if (status)
            {
                message = yobot::renderStatusText(*status, globalConfig);
            }
            co_await sessionSet.sendGroupMsg(msg.group_id, message);
        }
        co_return;
    });

    instance->onEvent<PrivateMsg>([&instance, &dbPool, &globalConfig](const PrivateMsg& msg) -> coro::task<> {
        auto sessionSet = instance->getApiSet(msg.self_id);
        auto db = dbPool->get();
        if (msg.raw_message == "你好")
        {
            co_await sessionSet.sendPrivateMsg(msg.user_id, "你好，我是yobotpp！");
        }
        if (msg.raw_message == "用户列表")
        {
            yobot::data::User user = {};
            for (const auto& raw : db(sqlpp::select(user.qqid).from(user).unconditionally()))
            {
                std::cout << raw.qqid << std::endl;
            }
            co_await sessionSet.sendPrivateMsg(msg.user_id, "操作完成");
        }
        if (msg.raw_message == "更新会战数据")
        {
            yobot::updateBossData(globalConfig);
            co_await sessionSet.sendPrivateMsg(msg.user_id, "更新成功");
        }
        co_return;
    });

    instance->onEvent<EnableEvent>([&instance](const EnableEvent& msg) -> coro::task<> {
        std::cout << "yobotpp已启动！ID：" << msg.self_id << std::endl;
        co_return;
    });

    instance->onEvent<DisableEvent>([&instance](const DisableEvent& msg) -> coro::task<> {
        std::cout << "yobotpp已停止！ID: " << msg.self_id << std::endl;
        co_return;
    });

    instance->onEvent<ConnectEvent>([&instance](const ConnectEvent& msg) -> coro::task<> {
        std::cout << "yobotpp已连接！ID: " << msg.self_id << std::endl;
        co_return;
    });

    std::cout << "Start listening:" << botConfig.ws_port << std::endl;
    instance->start();

    return 0;
}
