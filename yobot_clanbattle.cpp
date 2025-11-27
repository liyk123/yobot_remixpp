#include "yobot.h"
#include "yobot_clanbatte_group.h"
#include "yobot_clanbattle_tools.h"

constexpr auto TargetLocaleName = "zh_CN.UTF-8";
constexpr auto Group404ErrorResponse = "未检测到数据，请先创建公会！";
constexpr auto FormatErrorResponse = "格式错误";

namespace yobot {
    namespace clanbattle {
        namespace detail {
            using tools::adaptHPList;
            using tools::getPhase;

            std::string toText(const status& status)
            {
                auto& globalConfig = std::get<2>(getInstance());
                auto& [lap, gameServer, subList, chalList, thisHPList, nextHPList] = status;
                auto phase = getPhase(lap, gameServer);
                std::string message = std::format("现在是{}阶段，第{}周目：", (char)(phase + 'A'), lap);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][phase].get_ref<const ordered_json::array_t&>();
                for (size_t i = 1; i <= 5; i++)
                {
                    auto strI = std::to_string(i);
                    bool chanllenging = !chalList.is_discarded() && !chalList[strI].empty();
                    auto& HPList = (thisHPList[strI] == 0 ? nextHPList : thisHPList);
                    auto HP = HPList[strI].get<std::int64_t>();
                    auto fullHP = lapHPList[i - 1].get<std::int64_t>();
                    auto percent = HP * 100 / fullHP;
                    percent = ((percent == 0) ? (HP != 0) : percent);
                    auto rate = percent / 10;
                    rate = ((rate == 0) ? (HP != 0) : rate);
                    auto chalStr = (chanllenging ? "有" : "无");
                    message += std::format("\n{}.【{:■<{}}{:□<{}}】{:02}% {}人", i, "", rate, "", 10 - rate, percent, chalStr);
                }
                return message;
            }

            inline std::string showProgess(const GroupMsg& msg)
            {
                return toText(Group(msg.group_id).getStatus());
            }

            inline bool isFilledWithZero(json::array_t& HPList)
            {
                int zeroCount = 0;
                for (int i = 0; i < 5; i++)
                {
                    if (HPList[i] == 0)
                    {
                        zeroCount++;
                    }
                }
                return zeroCount == 5;
            }

            inline void filterHP(json& hp, const int unit, const std::int64_t fullHP)
            {
                hp = hp * unit;
                if (hp > fullHP)
                {
                    hp = fullHP;
                }
            }

            void filterHPList(json::array_t& thisHPList, json::array_t& nextHPList, const ordered_json::array_t& lapHPList, const bool isOverlap, const int unit)
            {
                for (int i = 0; i < 5; i++)
                {
                    auto& thisHP = thisHPList[i];
                    auto& nextHP = nextHPList[i];
                    auto fullHP = lapHPList[i].get<std::int64_t>();
                    if (!isOverlap)
                    {
                        if (thisHP != 0)
                        {
                            nextHP = fullHP;
                            filterHP(thisHP, unit, fullHP);
                        }
                        else
                        {
                            filterHP(nextHP, unit, fullHP);
                        }
                    }
                    else
                    {
                        if (thisHP != 0)
                        {
                            filterHP(thisHP, unit, fullHP);
                        }
                        nextHP = 0;
                    }
                }
            }

            bool checkAndFilterProgress(const std::string& gameServer, const int lap, const int unit, json::array_t& thisHPList, json::array_t& nextHPList)
            {
                if (lap < 1 || lap > 999)
                {
                    return false;
                }

                auto& globalConfig = std::get<2>(getInstance());
                auto thisPhase = getPhase(lap, gameServer);
                auto nextPhase = getPhase(lap + 1LL, gameServer);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][thisPhase].get_ref<const ordered_json::array_t&>();
                bool isOverlap = (thisPhase != nextPhase);

                if (isFilledWithZero(thisHPList))
                {
                    return false;
                }

                filterHPList(thisHPList, nextHPList, lapHPList, isOverlap, unit);

                return true;
            }

            bool setProgress(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(\[\s*(\d+),\s*([wWkK]|万|千),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\]),\s*(\[\s*\d+(?:,\s*\d+){4}\s*\])\s*\]$)";
                static const std::regex parten(partenStr);
                try
                {
                    std::smatch matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
                    {
                        int lap = std::atoi(matches[1].str().c_str());
                        int unit = 10000;
                        static const std::regex partenUnitK("[kK]|千");
                        if (std::regex_match(matches[2].str(), partenUnitK))
                        {
                            unit = 1000;
                        }
                        json::array_t thisHPList = json::parse(matches[3].str());
                        json::array_t nextHPList = json::parse(matches[4].str());
                        auto group = detail::Group(msg.group_id);
                        auto gameServer = group.getGameServer();
                        if (checkAndFilterProgress(gameServer, lap, unit, thisHPList, nextHPList))
                        {
                            group.setStatus(lap, adaptHPList(thisHPList), adaptHPList(nextHPList));
                            return true;
                        }
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << '\n';
                }
                return false;
            }

            void resetProgess(const GroupMsg& msg)
            {
                auto group = detail::Group(msg.group_id);
                auto gameServer = group.getGameServer();
                auto& globalConfig = std::get<2>(getInstance());
                auto phase = getPhase(1, gameServer);
                auto& lapHPList = globalConfig["boss_hp"][gameServer][1].get_ref<const ordered_json::array_t&>();
                auto HPList = adaptHPList(lapHPList);
                group.setStatus(1, HPList, HPList);
                group.clearChallenge();
            }

            inline bool isBossAlive(const status& s, const std::string& bossNum)
            {
                return s.thisLapHPList[bossNum] != 0 || s.nextLapHPList[bossNum] != 0;
            }

            inline std::string getApplicationBossNum(const status& s, const std::uint64_t user_id)
            {
                for (auto it = s.challengerList.begin(); it != s.challengerList.end(); it++)
                {
                    if (it.value().contains(std::to_string(user_id)))
                    {
                        return it.key();
                    }
                }
                return {};
            }

            inline bool isApplied(const status& s, const std::uint64_t user_id)
            {
                return !getApplicationBossNum(s, user_id).empty();
            }

            bool applyForChallenge(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(([1-5])\s*(:|：|\s)?\s*(\S*)$)";
                static const std::regex parten(partenStr);
                try
                {
                    std::smatch matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
                    {
                        auto bossNum = matches[1].str();
                        auto group = Group(msg.group_id);
                        auto s = group.getStatus();
                        bool isMatched = ((matches[2].length() != 0) || (matches[3].length() == 0));
                        if (isMatched && isBossAlive(s, bossNum) && !isApplied(s, msg.user_id))
                        {
                            bool isContinue = (msg.raw_message.find("补") != std::string::npos);
                            auto message = matches[3].str();
                            group.setChallenger(bossNum, msg.user_id, {
                                .is_continue = isContinue,
                                .msg = message
                                });
                            return true;
                        }
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << std::endl;
                }
                return false;
            }

            bool cancelApplyForChallenge(const GroupMsg& msg)
            {
                auto group = Group(msg.group_id);
                auto s = group.getStatus();
                auto bossNum = getApplicationBossNum(s, msg.user_id);
                if (!bossNum.empty())
                {
                    group.removeChallenger(bossNum, msg.user_id);
                    return true;
                }
                return false;
            }

            inline int toUnit(const std::string& unitStr)
            {
                static const std::regex partenUnitK("[kK]|千");
                static const std::regex partenUnitW("[wW]|万");
                int unit = 1;
                if (std::regex_match(unitStr, partenUnitK))
                {
                    unit = 1000;
                }
                else if (std::regex_match(unitStr, partenUnitW))
                {
                    unit = 10000;
                }
                return unit;
            }

            inline std::string checkChallengeInput(const bool isKilled, const std::string& applyNum, const std::smatch& matches)
            {
                if (isKilled)
                {
                    if (applyNum.empty() && matches[1].length() == 0 && matches[3].length() == 0)
                    {
                        return "未申请且没有指定boss";
                    }
                    auto firstChar = matches[3].str().c_str()[0];
                    if (firstChar == '-' || firstChar > '5' || matches[4].length() != 0)
                    {
                        return FormatErrorResponse;
                    }
                }
                else
                {
                    if (applyNum.empty() && (matches[1].length() == 0 || matches[2].length() == 0))
                    {
                        return "未申请且没有指定boss";
                    }
                }
                return {};
            }

            inline std::string getChallegeBossNum(const bool isKilled, const std::string& applyNum, const std::smatch& matches)
            {
                auto bossNum = applyNum;
                if (isKilled)
                {
                    if (matches[1].length() != 0 && matches[2].length() == 0)
                    {
                        bossNum = matches[1];
                    }
                    else
                    {
                        if (matches[3].length() != 0)
                        {
                            bossNum = matches[3];
                        }
                    }
                }
                else
                {
                    if (matches[1].length() != 0 && matches[2].length() != 0)
                    {
                        bossNum = matches[1];
                    }
                }
                return bossNum;
            }

            inline std::int64_t getChallengeDamage(const bool isKilled, const std::int64_t bossHP, const std::smatch& matches)
            {
                if (isKilled)
                {
                    return bossHP;
                }
                else
                {
                    int unit = toUnit(matches[4]);
                    auto damageStr = matches[3].str();
                    if (matches[2].length() == 0)
                    {
                        damageStr = matches[1].str() + damageStr;
                    }
                    return std::atoll(damageStr.c_str()) * unit;
                }
            }

            inline std::string processReportChanllenge(const GroupMsg& msg, const std::smatch& matches)
            {
                auto group = Group(msg.group_id);
                auto s = group.getStatus();
                auto applyNum = getApplicationBossNum(s, msg.user_id);
                bool isKilled = (msg.raw_message.find("尾") != std::string::npos);
                auto checkResult = checkChallengeInput(isKilled, applyNum, matches);
                if (!checkResult.empty())
                {
                    return checkResult;
                }

                auto bossNum = getChallegeBossNum(isKilled, applyNum, matches);
                bool isNext = (s.thisLapHPList[bossNum] == 0);
                std::int64_t bossHP = (isNext ? s.nextLapHPList[bossNum] : s.thisLapHPList[bossNum]);
                std::int64_t damage = getChallengeDamage(isKilled, bossHP, matches);

                if (!isKilled && bossHP <= damage)
                {
                    return "伤害超出剩余血量，请使用“尾刀”指令";
                }
                bossHP = bossHP - damage;

                bool isContinue = (msg.raw_message.find("补") != std::string::npos);
                group.pushChallenge({
                    .userId = msg.user_id,
                    .time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                    .lap = (int)s.lap + (int)isNext,
                    .bossNum = std::atoi(bossNum.c_str()),
                    .bossHP = bossHP,
                    .damage = damage,
                    .isContinue = isContinue
                    });
                if (!applyNum.empty())
                {
                    group.removeChallenger(applyNum, msg.user_id);
                }
                std::string result = isKilled
                    ? std::format(std::locale(TargetLocaleName), "对{}王造成伤害{:L}，并击败", bossNum, damage)
                    : std::format(std::locale(TargetLocaleName), "对{}王造成伤害{:L}，剩余血量{:L}", bossNum, damage, bossHP);
                return result + "\n" + toText(group.getStatus());
            }

            std::string reportChallenge(const GroupMsg& msg)
            {
                constexpr auto partenStr = R"(([1-5])?(\s*)?(\d+)?([wWkK]|万|千)?$)";
                static const std::regex parten(partenStr);
                try
                {
                    auto matches = tools::regexSearch(parten, msg.raw_message);
                    if (!matches.empty())
                    {
                        return processReportChanllenge(msg, matches);
                    }
                }
                catch (std::exception e)
                {
                    std::cerr << e.what() << std::endl;
                }
                return FormatErrorResponse;
            }

            std::string cancelReportChallenge(const GroupMsg& msg)
            {
                auto group = Group(msg.group_id);
                group.popChallenge();
                return "撤销完成\n" + toText(group.getStatus());
            }
        }

        Action groupAction(GroupAction act)
        {
            return [=](const Message& msg) {
                return std::visit([=](auto&& x) -> std::string {
                    if constexpr (std::is_convertible_v<decltype(x), GroupMsg>)
                    {
                        return !detail::Group(x.group_id)
                            ? Group404ErrorResponse
                            : act(x);
                    }
                    return {};
                }, msg);
            };
        }

        RegexAction showProgress()
        {
            static const std::regex rgx("进度");
            static const Action act = groupAction(detail::showProgess);
            return { rgx,act };
        }

        RegexAction setProgress()
        {
            static const std::regex rgx(R"(^(设置|调整|修改|变更|更新|改变)进度.*)");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::setProgress(msg)
                    ? "进度已修改：\n" + detail::showProgess(msg)
                    : FormatErrorResponse;
            });
            return { rgx,act };
        }

        RegexAction resetProgress()
        {
            static const std::regex rgx("重置进度");
            static const Action act = groupAction([](const GroupMsg& msg) {
                detail::resetProgess(msg);
                return "进度已重置：\n" + detail::showProgess(msg);
            });
            return { rgx,act };
        }

        RegexAction applyForChallenge()
        {
            static const std::regex rgx("^申请(出|补|出补)刀.*");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::applyForChallenge(msg)
                    ? "申请成功：\n" + detail::showProgess(msg)
                    : "申请失败，格式或状态错误";
            });
            return { rgx, act };
        }

        RegexAction cancelApplyForChallenge()
        {
            static const std::regex rgx("^(取消|撤销)申请");
            static const Action act = groupAction([](const GroupMsg& msg) {
                return detail::cancelApplyForChallenge(msg)
                    ? "取消申请成功：\n" + detail::showProgess(msg)
                    : "没有申请记录";
            });
            return { rgx, act };
        }

        RegexAction reportChallenge()
        {
            static const std::regex rgx("^(报|尾|报尾|补|补尾)刀.*");
            static const Action act = groupAction(detail::reportChallenge);
            return { rgx, act };
        }

        RegexAction cancelReportChallenge()
        {
            static const std::regex rgx("撤销报刀");
            static const Action act = groupAction(detail::cancelReportChallenge);
            return { rgx, act };
        }
    }
}