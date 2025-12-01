#include <iostream>
#include <fstream>
#include "yobot.h"
#include "default_config.h"
#include "yobotdata_new.sql.h"
#include "yobot_system.h"
#include "yobot_clanbattle.h"

constexpr auto TargetLocaleName = "zh_CN.UTF-8";
constexpr auto C_LocaleName = "C";
constexpr auto DB_Name = "yobotdata_new.db";
constexpr auto ConfigName = "yobot_config.json";
constexpr auto IconDir = "icon";

namespace yobot {
    inline auto InitConfig() noexcept
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
        //dbConfig->debug = true;
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

    inline std::shared_ptr<yobot::DB_Pool> InitDatabase(const std::shared_ptr<yobot::DB_Config>& dbConfig) noexcept
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

    inline Instance construct()
    {
        auto&& [botConfig, dbConfig, globalConfig] = InitConfig();
        auto dbPool = InitDatabase(dbConfig);
        auto onebotIO = twobot::BotInstance::createInstance(botConfig);
        std::filesystem::create_directory(IconDir);
        RegexActionVector regexActionVec = {
            system::showVersion(),
            system::showStatistics(),
            system::updateData(),
            clanbattle::createClan(),
            clanbattle::joinClan(),
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

    Instance& getInstance()
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
            std::cout << "websocket已连接! ID: " << msg.self_id << std::endl;
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

