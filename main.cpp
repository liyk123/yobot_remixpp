#include <mimalloc-new-delete.h>
#include <mimalloc-override.h>
#include <iostream>
#include <fstream>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <async_simple/coro/Lazy.h>
#include <httplib.h>
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

		bool setStatus(const std::int64_t& bossCycle, const json& nowCycleBossHealth, const json& nextCycleBossHealth)
        {
            bool ret = false;
            auto db = m_pool->get();
            if (isStatusLegal(bossCycle, nowCycleBossHealth, nextCycleBossHealth))
            {
                db(
                    sqlpp::update(m_clanGroup)
                    .set(
                        m_clanGroup.challengingMemberList = sqlpp::null,
                        m_clanGroup.subscribeList = sqlpp::null,
                        m_clanGroup.bossCycle = bossCycle,
                        m_clanGroup.nowCycleBossHealth = nowCycleBossHealth.dump(),
                        m_clanGroup.nextCycleBossHealth = nextCycleBossHealth.dump()
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

    inline std::int64_t getLevel(const std::int64_t bossCycle, const std::string& gameServer, const ordered_json& globalConfig)
    {
        char ret = 0;
        auto& levelList = globalConfig["level_by_cycle"][gameServer].get_ref<const ordered_json::array_t&>();
        for (auto&& lv : levelList)
        {
            if (bossCycle >= lv[0] && bossCycle <= lv[1])
            {
                break;
            }
            ret++;
        }
        return ret;
    }

    std::string renderStatusText(std::optional<Group::status>& status, const ordered_json& globalConfig)
    {
        std::cout << status << std::endl;
        const auto&& [bossCycle, gameServer, subList, chalList, thisHPList, nextHPList] = std::move(*status);
        auto level = getLevel(bossCycle, gameServer, globalConfig);
        std::string message = std::format("现在是{}阶段，第{}周目：", (char)(level + 'A'), bossCycle);
        auto& levelHPList = globalConfig["boss"][gameServer][level].get_ref<const ordered_json::array_t&>();
        for (size_t i = 1; i <= 5; i++)
        {
            auto strI = std::to_string(i);
            bool chanllenging = !chalList.is_discarded() && !chalList[strI].is_null();
            auto& HPList = (thisHPList[strI] == 0 ? nextHPList : thisHPList);
            auto HP = HPList[strI].get<std::int64_t>();
            auto fullHP = levelHPList[i - 1].get<std::int64_t>();
			std::int64_t rate = HP * 10 / fullHP + (HP == 0);
            auto chalStr = (chanllenging ? "有" : "无");
            message += std::format("\n{}.【{:■<{}}{:□<{}}】{}人", i, "", rate, "", 10 - rate, chalStr);
        }
        return message;
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

int main(int argc, char** args)
{
    auto [botConfig, dbConfig, globalConfig] = InitConfig();
    auto dbPool = InitDatabase(dbConfig);
    auto instance = twobot::BotInstance::createInstance(botConfig);

    instance->onEvent<GroupMsg>([&instance, &dbPool, &globalConfig](const GroupMsg& msg, const std::any& session) {
        auto sessionSet = instance->getApiSet(session);

        if (msg.raw_message == "version")
        {
            sessionSet.sendGroupMsg(msg.group_id, versionInfo);
        }
        if (msg.raw_message == "进度")
        {
            auto group = yobot::Group(dbPool, msg.group_id);
            auto status = group.getStatus();
            std::string message = "未检测到数据，请先创建公会！";
            if (status)
            {
                message = yobot::renderStatusText(status, globalConfig);
            }
            sessionSet.sendGroupMsg(msg.group_id, message);
        }
        if (msg.raw_message == "更新会战数据")
        {
            auto group = yobot::Group(dbPool, msg.group_id);
            auto status = group.getStatus();
            if (status)
            {
                httplib::Client client("https://pcr.satroki.tech");
                auto result = client.Get("/api/Quest/GetClanBattleInfos?s=" + std::get<1>(*status));
                if (result && result->status == 200)
                {
                    auto clanBattleInfo = ordered_json::parse(result->body);
                    auto &lastInfo = *(clanBattleInfo.rbegin());
                    std::cout << lastInfo << std::endl;
                }
                sessionSet.sendGroupMsg(msg.group_id, "更新成功");
            }
            
        }
    });

    instance->onEvent<PrivateMsg>([&instance, &dbPool](const PrivateMsg& msg, const std::any& session) {
        auto sessionSet = instance->getApiSet(session);
        auto db = dbPool->get();
        if (msg.raw_message == "你好")
        {
            sessionSet.sendPrivateMsg(msg.user_id, "你好，我是yobotpp！");
        }
        if (msg.raw_message == "进度")
        {
            std::string message = "现在是阶段B，第1周目：";
            for (int i = 0; i < 5; i++)
            {
                message += std::format("\n{}.【{:■<{}}{:□<{}}】{}", i + 1, "", 5 - i, "", 5 + i, (i % 2 ? "有人" : ""));
            }
            sessionSet.sendPrivateMsg(msg.user_id, message);
        }
        if (msg.raw_message == "用户列表")
        {
            yobot::data::User user = {};
            for (const auto& raw : db(sqlpp::select(user.qqid).from(user).unconditionally()))
            {
                std::cout << raw.qqid << std::endl;
            }
            sessionSet.sendPrivateMsg(msg.user_id, "操作完成");
        }
    });

    instance->onEvent<EnableEvent>([&instance](const EnableEvent& msg, const std::any& session) {
        std::cout << "yobotpp已启动！ID：" << msg.self_id << std::endl;
    });

    instance->onEvent<DisableEvent>([&instance](const DisableEvent& msg, const std::any& session) {
        std::cout << "yobotpp已停止！ID: " << msg.self_id << std::endl;
    });

    instance->onEvent<ConnectEvent>([&instance](const ConnectEvent& msg, const std::any& session) {
        std::cout << "yobotpp已连接！ID: " << msg.self_id << std::endl;
    });

    std::cout << "Start listening:" << botConfig.ws_port << std::endl;
    instance->start();

    return 0;
}
