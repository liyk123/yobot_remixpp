#include <iostream>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <async_simple/coro/Lazy.h>
#include "yobotdata_new.h"
#include "default_config.h"
#include "yobotdata_new.sql.h"

#define CONNECT_LITERAL(X,Y) X##Y

using namespace nlohmann::json_literals;
using namespace twobot::Event;
using nlohmann::json;


namespace yobot {
    namespace cqcode {
        inline constexpr auto cq(const char* op, auto id)
        {
			return std::format("[CQ:{},qq={}]", op, id);
        }

        inline constexpr auto at(auto id)
        {
            return cq("at", id);
        }

        inline constexpr auto avatar(auto id)
        {
            return cq("avatar", id);
        }
    }

    using DB_Pool = sqlpp::sqlite3::connection_pool;
    using DB_Config = sqlpp::sqlite3::connection_config;

    class Group
    {
    public:
        Group(std::shared_ptr<DB_Pool> pool, std::uint64_t groupID)
            : m_pool(pool)
            , m_groupID(groupID)
            , m_clanGroup()
        {

        }
        ~Group()
        {

        }
		Group(const Group&) = delete;
		Group(Group&) = delete;
        Group(Group&&) = default;

    public:
        std::optional<std::tuple<std::int64_t, json, json, json>> getStatus()
        {            
            auto db = m_pool->get();
            auto raws = db(
                sqlpp::select(sqlpp::all_of(m_clanGroup))
                .from(m_clanGroup)
                .where(m_clanGroup.groupId == m_groupID)
            );
            for (const auto &raw : raws)
            {
                return std::make_optional(std::make_tuple(
                    raw.bossCycle.value(),
                    json::parse(raw.challengingMemberList.value(), nullptr, false),
                    json::parse(raw.nowCycleBossHealth.value()),
                    json::parse(raw.nextCycleBossHealth.value())
                ));
            }
            return std::nullopt;
		}

        void setStatus(const std::int64_t& bossCycle, const json& nowCycleBossHealth, const json& nextCycleBossHealth)
        {
            auto db = m_pool->get();
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
        }

    private:
        std::shared_ptr<DB_Pool> m_pool;
        std::uint64_t m_groupID;
        data::ClanGroup m_clanGroup;
    };

}

inline auto InitConfig()
{
#ifdef _WIN32
    system("chcp 65001 && cls");
#endif
    std::cout << "Initializing...\n";
    constexpr auto targetLocaleName = "zh_CN.UTF-8";
    constexpr auto cLocaleName = "C";
    constexpr auto dbName = "yobotdata_new.db";
    std::setlocale(LC_ALL, targetLocaleName);
    std::setlocale(LC_NUMERIC, cLocaleName);
    std::locale::global(std::locale(targetLocaleName));
    std::locale::global(std::locale(cLocaleName, std::locale::numeric));
    auto dbConfig = std::make_shared<yobot::DB_Config>(dbName, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    dbConfig->debug = true;
    twobot::Config botConfig = {
        .host = "127.0.0.1",
        .api_port = 5700,
        .ws_port = 9444,
        .token = std::nullopt
    };
    return std::make_tuple(botConfig, dbConfig);
}

std::shared_ptr<yobot::DB_Pool> InitDatabase(const std::shared_ptr<yobot::DB_Config> &dbConfig)
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
    twobot::Config botConfig;
    std::shared_ptr<yobot::DB_Config> dbConfig;
    std::tie(botConfig, dbConfig) = InitConfig();
    auto dbPool = InitDatabase(dbConfig);
    auto instance = twobot::BotInstance::createInstance(botConfig);

    instance->onEvent<GroupMsg>([&instance, &dbPool](const GroupMsg& msg, const std::any& session) {
        auto sessionSet = instance->getApiSet(session);
        if (msg.raw_message == "你好")
        {
            sessionSet.sendGroupMsg(msg.group_id, "你好，我是yobotpp！");
        }
        if (msg.raw_message == "进度")
        {
            auto group = yobot::Group(dbPool, msg.group_id);
            std::cout << group.getStatus() << std::endl;
			sessionSet.sendGroupMsg(msg.group_id, "操作完成");
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

