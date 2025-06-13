#include <iostream>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <async_simple/coro/Lazy.h>
#include "yobotdata.h"

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
                message += std::format("{} {} {} {}", raw.battleId.value(), raw.bossNum.value(), raw.bossCycle.value(), raw.bossHealth.value());
            }
            return message;
		}

    private:
        std::shared_ptr<DB_Pool> m_pool;
        std::uint64_t m_groupID;
        yobot::data::ClanGroup m_clanGroup;
    };

}

int main(int argc, char** args)
{
#ifdef _WIN32
    system("chcp 65001 && cls");
#endif
    const char* localeName = "zh_CN.UTF-8";
    std::setlocale(LC_ALL, localeName);
    std::locale::global(std::locale(localeName));
    auto sqlconfig = std::make_shared<sqlpp::sqlite3::connection_config>("yobotdata.db", SQLITE_OPEN_READWRITE);
    auto db_pool = std::make_shared<sqlpp::sqlite3::connection_pool>(sqlconfig,2);
    twobot::Config config = {
        "192.168.123.50",
        5700,
        9444
    };
    auto instance = twobot::BotInstance::createInstance(config);
    instance->onEvent<GroupMsg>([&instance, &db_pool](const GroupMsg& msg, void* session) {
        auto sessionSet = instance->getApiSet(session);
        if (msg.raw_message == "你好")
        {
            sessionSet.sendGroupMsg(msg.group_id, "你好，我是yobotpp！");
        }
        if (msg.raw_message == "进度")
        {
            auto group = yobot::Group(db_pool, msg.group_id);
            std::cout << group.getStatus() << std::endl;
			sessionSet.sendGroupMsg(msg.group_id, "操作完成");
        }
    });

    instance->onEvent<PrivateMsg>([&instance, &db_pool](const PrivateMsg& msg, void* session) {
        auto sessionSet = instance->getApiSet(session);
        auto db = db_pool->get();
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

    instance->start();

    return 0;
}

