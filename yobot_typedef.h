#pragma once
#include <regex>
#include <twobot.hh>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/sqlite3.h>
#include <tbb/tbb.h>

using nlohmann::json;
using nlohmann::ordered_json;

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
}