#include <iostream>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <async_simple/coro/Lazy.h>
#include "yobotdata_new.h"
#include "default_config.h"

#define CONNECT_LITERAL(X,Y) X##Y

using namespace nlohmann::json_literals;
using namespace twobot::Event;


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
		std::string getStatus()
		{
            auto db = m_pool->get();
            std::string message;
            auto groupID = std::to_string(m_groupID);
            for (const auto &raw : db(sqlpp::select(sqlpp::all_of(m_clanGroup)).from(m_clanGroup).where(m_clanGroup.groupId == m_groupID)))
            {
                message += std::format("{} {} {} {}", raw.battleId.value(), raw.challengingMemberList.value(), raw.bossCycle.value(), raw.nowCycleBossHealth.value());
            }
            return message;
		}

    private:
        std::shared_ptr<DB_Pool> m_pool;
        std::uint64_t m_groupID;
        yobot::data::ClanGroup m_clanGroup;
    };

}

inline auto InitConfig()
{
#ifdef _WIN32
    system("chcp 65001 && cls");
#endif
    std::cout << "Initializing...";
    const char* targetLocaleName = "zh_CN.UTF-8";
    const char* cLocaleName = "C";
    std::setlocale(LC_ALL, targetLocaleName);
    std::setlocale(LC_NUMERIC, cLocaleName);
    std::locale::global(std::locale(targetLocaleName));
    std::locale::global(std::locale(cLocaleName, std::locale::numeric));
    auto dbConfig = std::make_shared<yobot::DB_Config>("yobotdata_new.db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    twobot::Config botConfig = {
        .host = "192.168.123.50",
        .api_port = 5700,
        .ws_port = 9444,
        .token = std::nullopt
    };
    return std::make_tuple(botConfig, dbConfig);
}

int main(int argc, char** args)
{
    twobot::Config botConfig;
    std::shared_ptr<yobot::DB_Config> dbConfig;
    std::tie(botConfig, dbConfig) = InitConfig();
    auto dbPool = std::make_shared<yobot::DB_Pool>(dbConfig, 2);
    auto instance = twobot::BotInstance::createInstance(botConfig);

    instance->onEvent<GroupMsg>([&instance, &dbPool](const GroupMsg& msg, void* session) {
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

    instance->onEvent<PrivateMsg>([&instance, &dbPool](const PrivateMsg& msg, void* session) {
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

    instance->onEvent<EnableEvent>([&instance](const EnableEvent& msg, void* session) {
        std::cout << "yobotpp已启动！ID：" << msg.self_id << std::endl;
    });

    instance->onEvent<DisableEvent>([&instance](const DisableEvent& msg, void* session) {
        std::cout << "yobotpp已停止！ID: " << msg.self_id << std::endl;
    });

    instance->onEvent<ConnectEvent>([&instance](const ConnectEvent& msg, void* session) {
        std::cout << "yobotpp已连接！ID: " << msg.self_id << std::endl;
    });

    std::cout << "Start listening:" << botConfig.ws_port << std::endl;
    instance->start();

    return 0;
}

