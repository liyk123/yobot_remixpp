#include <iostream>
#include <twobot.hh>

using twobot::Config;
using twobot::BotInstance;
using twobot::ApiSet;
using namespace twobot::Event;

int main(int argc, char** args)
{
    // 解决UTF8编码，中文乱码问题，不需要可以不加
#ifdef _WIN32
    system("chcp 65001 && cls");
#endif
    const char* localeName = "zh_CN.UTF-8";
    std::setlocale(LC_ALL, localeName);
    std::locale::global(std::locale(localeName));
    Config config = {
        "192.168.123.50",
        5700,
        9444
    };
    auto instance = BotInstance::createInstance(config);
    if (!instance->getApiSet(false).testConnection())
    {
        std::cerr << "HTTP测试失败，请启动onebot服务器，并配置HTTP端口！" << std::endl;
    }
    else
    {
        std::cout << "HTTP测试通过!" << std::endl;
    }
    instance->onEvent<GroupMsg>([&instance](const GroupMsg& msg, void* session) {
        twobot::ApiSet::ApiResult r = {};
        if (msg.raw_message == "你好")
        {
            r = instance->getApiSet().sendGroupMsg(msg.group_id, "你好，我是twobot！");
        }
        else if (msg.raw_message.find("AT我") != std::string::npos)
        {
            std::string at = "[CQ:at,qq=" + std::to_string(msg.user_id) + "]";
            r = instance->getApiSet().sendGroupMsg(msg.group_id, at + "要我at你干啥？");
        }
        else if (msg.raw_message == "头像")
        {
            char buf[64];
            std::sprintf(buf, "[CQ:avatar,qq=%llu]", msg.user_id);
            r = instance->getApiSet().sendGroupMsg(msg.group_id, std::string(buf));
        }
        else if (msg.raw_message == "私聊")
        {
            r = instance->getApiSet().sendPrivateMsg(msg.user_id, "你好，我是twobot！");
        }

        std::cout << r.second.dump() << std::endl;
    });

    instance->onEvent<PrivateMsg>([&instance](const PrivateMsg& msg, void* session) {
        twobot::ApiSet::ApiResult r = {};
        if (msg.raw_message == "你好")
        {
            r = instance->getApiSet(session).sendPrivateMsg(msg.user_id, "你好，我是twobot！");
        }
        else if (msg.raw_message == "头像")
        {
            nlohmann::json param = {
                {"user_id", msg.user_id}
            };
            char buf[64];
            std::sprintf(buf, "[CQ:avatar,qq=%llu]", msg.user_id);
            r = instance->getApiSet(session).sendPrivateMsg(msg.user_id, std::string(buf));
        }
        std::cout << r.second.dump() << std::endl;
    });

    instance->onEvent<EnableEvent>([&instance](const EnableEvent& msg, void* session) {
        std::cout << "twobot已启动！机器人QQ：" << msg.self_id << std::endl;
    });

    instance->onEvent<DisableEvent>([&instance](const DisableEvent& msg, void* session) {
        std::cout << "twobot已停止！ID: " << msg.self_id << std::endl;
    });

    instance->onEvent<ConnectEvent>([&instance](const ConnectEvent& msg, void* session) {
        std::cout << "twobot已连接！ID: " << msg.self_id << std::endl;
    });


    // 启动机器人
    instance->start();

    return 0;
}

