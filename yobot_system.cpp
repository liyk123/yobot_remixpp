#include <fstream>
#include <httplib.h>
#include "yobot.h"
#include "yobotdata_new.h"

constexpr auto VersionInfo = "Branch: " GIT_BRANCH "\nCommit: " GIT_VERSION "\nDate: " GIT_DATE;
constexpr auto ConfigName = "yobot_config.json";
constexpr auto IconDir = "icon";
constexpr auto AuthorityErrorRespone = "权限错误";

namespace yobot {
    namespace area {
        constexpr std::string_view cn = "cn";
        constexpr std::string_view tw = "tw";
        constexpr std::string_view jp = "jp";
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

        RegexAction showVersion()
        {
            static const std::regex rgx("version");
            static const Action act = [](const Message& msg) {
                return VersionInfo;
            };
            return { rgx,act };
        }

        RegexAction showStatistics()
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

        RegexAction updateData()
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
}