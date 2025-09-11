#include "yobot.h"
#include "yobot_clanbatte_group.h"

namespace yobot {
	namespace clanbattle {
		namespace detail {
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChallengerDetail, is_continue, behalf, tree, msg);

			std::int8_t getPhase(const std::int64_t lap, const std::string& gameServer)
			{
				char ret = 0;
				auto& globalConfig = std::get<2>(getInstance());
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

			json adaptHPList(const std::ranges::range auto& list)
			{
				json ret = {};
				for (int i = 0; i < 5; i++)
				{
					auto strI = std::to_string(i + 1);
					ret[strI] = list[i];
				}
				return ret;
			}

			inline void recordChallenge(const Challenge& challenge, status& s)
			{
				static const json zeroHPList = { {"1", 0}, { "2",0 }, { "3",0 }, { "4",0 }, { "5",0 } };
				if (challenge.damage == 0)
				{
					return;
				}
				auto strI = std::to_string(challenge.bossNum);
				if (s.nextLapHPList[strI] != 0)
				{
					if (s.thisLapHPList[strI] != 0)
					{
						s.thisLapHPList[strI] = challenge.bossHP;
					}
					else
					{
						s.nextLapHPList[strI] = challenge.bossHP;
					}
				}
				if (challenge.bossHP == 0)
				{
					if (s.thisLapHPList == zeroHPList)
					{
						++s.lap;
						auto thisPhase = getPhase(s.lap, s.gameServer);
						auto nextPhase = getPhase(s.lap + 1, s.gameServer);
						auto& globalConfig = std::get<2>(getInstance());
						s.thisLapHPList = s.nextLapHPList;
						if (thisPhase != nextPhase)
						{
							s.nextLapHPList = zeroHPList;
						}
						else
						{
							auto lapHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][thisPhase].get_ref<const ordered_json::array_t&>());
							if (s.nextLapHPList == zeroHPList)
							{
								s.thisLapHPList = lapHPList;
							}
							s.nextLapHPList = lapHPList;
						}
					}
				}
			}

			inline void revokeChallenge(const Challenge& challenge, status& s)
			{
				static const json zeroHPList = { {"1", 0}, { "2",0 }, { "3",0 }, { "4",0 }, { "5",0 } };
				auto strI = std::to_string(challenge.bossNum);
				if (challenge.bossHP == 0)
				{
					auto thisPhase = getPhase(s.lap, s.gameServer);
					auto lastPhase = getPhase(s.lap - 1, s.gameServer);
					auto& globalConfig = std::get<2>(getInstance());
					auto thisLapFullHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][thisPhase].get_ref<const ordered_json::array_t&>());
					if (s.thisLapHPList == thisLapFullHPList)
					{
						--s.lap;
						if (thisPhase != lastPhase)
						{
							if (thisPhase > 1)
							{
								auto lastLapFullHPList = adaptHPList(globalConfig["boss_hp"][s.gameServer][lastPhase].get_ref<const ordered_json::array_t&>());
								s.nextLapHPList = zeroHPList;
							}
						}
						else
						{
							s.nextLapHPList = thisLapFullHPList;
						}
						s.thisLapHPList = zeroHPList;
					}
				}
				s.thisLapHPList[strI] = challenge.bossHP + challenge.damage;
			}

			inline std::time_t toDateOnly(const std::time_t time)
			{
				auto timePoint = std::chrono::system_clock::from_time_t(time);
				auto timeDays = std::chrono::floor<std::chrono::days>(timePoint);
				return std::chrono::system_clock::to_time_t(timeDays);
			}

			inline std::time_t toTimeOnly(const std::time_t time)
			{
				auto timePoint = std::chrono::system_clock::from_time_t(time);
				auto timeOnly = timePoint - std::chrono::floor<std::chrono::days>(timePoint);
				return std::chrono::duration_cast<std::chrono::seconds>(timeOnly).count();
			}

			Group::Group(std::uint64_t groupID) noexcept
				: m_pool(std::get<1>(getInstance()))
				, m_groupID(groupID)
				, m_clanGroup()
				, m_clanChallenge()
			{

			}

			Group::operator bool() const
			{
				auto r = m_pool->get()(
					select(m_clanGroup.deleted)
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID)
				);
				return r.empty() ? false : !r.begin()->deleted.value();
			}

			status Group::getStatus()
			{
				auto db = m_pool->get();
				auto raws = db(
					select(all_of(m_clanGroup))
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID)
				);
				auto& raw = *raws.begin();
				return {
					raw.bossCycle.value(),
					raw.gameServer.value(),
					json::parse(raw.challengingMemberList.value(), nullptr, false),
					json::parse(raw.subscribeList.value(), nullptr, false),
					json::parse(raw.nowCycleBossHealth.value()),
					json::parse(raw.nextCycleBossHealth.value())
				};
			}

			void Group::setStatus(const std::int64_t lap, const json& thisLapBossHealth, const json& nextLapBossHealth)
			{
				auto db = m_pool->get();
				db(
					update(m_clanGroup)
					.set(
						m_clanGroup.challengingMemberList = sqlpp::null,
						m_clanGroup.subscribeList = sqlpp::null,
						m_clanGroup.bossCycle = lap,
						m_clanGroup.nowCycleBossHealth = thisLapBossHealth.dump(),
						m_clanGroup.nextCycleBossHealth = nextLapBossHealth.dump()
					)
					.where(m_clanGroup.groupId == m_groupID)
				);
			}

			std::string Group::getGameServer()
			{
				return m_pool->get()(
					select(m_clanGroup.gameServer)
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID)
				).begin()->gameServer.value();
			}

			void Group::setChallenger(std::string_view bossNum, std::uint64_t userId, const ChallengerDetail& detail)
			{
				updateChanllengerList([=](json& list) {
					list[bossNum][std::to_string(userId)] = detail;
				});
			}

			void Group::removeChallenger(std::string_view bossNum, std::uint64_t userId)
			{
				updateChanllengerList([=](json& list) {
					if (!list[bossNum].is_null() && !list[bossNum][std::to_string(userId)].is_null())
					{
						list[bossNum].erase(std::to_string(userId));
					}
				});
			}

			void Group::pushChallenge(const Challenge& challenge)
			{
				auto db = m_pool->get();
				auto bid =
					select(m_clanGroup.battleId)
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID);
				db(
					insert_into(m_clanChallenge)
					.set(
						m_clanChallenge.behalf = challenge.behafId,
						m_clanChallenge.bid = bid,
						m_clanChallenge.bossCycle = challenge.lap,
						m_clanChallenge.bossHealthRemain = challenge.bossHP,
						m_clanChallenge.bossNum = challenge.bossNum,
						m_clanChallenge.challengeDamage = challenge.damage,
						m_clanChallenge.challengePcrdate = toDateOnly(challenge.time),
						m_clanChallenge.challengePcrtime = toTimeOnly(challenge.time),
						m_clanChallenge.gid = m_groupID,
						m_clanChallenge.isContinue = (int)challenge.isContinue,
						m_clanChallenge.message = challenge.message,
						m_clanChallenge.qqid = challenge.userId
					)
				);

				updateStatusInternal([&](status& s) {
					recordChallenge(challenge, s);
				});
			}

			void Group::popChallenge()
			{
				auto db = m_pool->get();
				auto challengeJoinGroup =
					m_clanChallenge
					.left_outer_join(m_clanGroup)
					.on(m_clanChallenge.bid == m_clanGroup.battleId);
				auto maxCid =
					select(max(m_clanChallenge.cid))
					.from(m_clanChallenge)
					.where(m_clanChallenge.gid == m_groupID);
				auto raws = db(
					select(all_of(m_clanChallenge))
					.from(challengeJoinGroup)
					.where(m_clanChallenge.cid == maxCid)
				);
				Challenge chal;
				for (auto&& raw : raws)
				{
					chal.behafId = raw.behalf;
					chal.bossHP = raw.bossHealthRemain;
					chal.bossNum = (int)raw.bossNum;
					chal.damage = raw.challengeDamage;
					chal.time = raw.challengePcrdate + raw.challengePcrtime;
					chal.isContinue = raw.isContinue;
					chal.lap = (int)raw.bossCycle;
					chal.message = raw.message;
					chal.userId = raw.qqid;
				}

				updateStatusInternal([&](status& s) {
					revokeChallenge(chal, s);
		        });
				db(remove_from(m_clanChallenge).where(m_clanChallenge.cid == maxCid));
			}

			void Group::clearChallenge()
			{
				auto db = m_pool->get();
				auto bid =
					select(m_clanGroup.battleId)
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID);
				db(
					remove_from(m_clanChallenge)
					.where(m_clanChallenge.gid == m_groupID and m_clanChallenge.bid == bid)
				);
			}

			void Group::updateStatusInternal(const std::function<void(status&)>& func)
			{
				auto s = getStatus();
				func(s);
				auto db = m_pool->get();
				db(
					update(m_clanGroup)
					.set(
						m_clanGroup.bossCycle = s.lap,
						m_clanGroup.challengingMemberList = s.challengerList.dump(),
						m_clanGroup.subscribeList = s.subscribeList.dump(),
						m_clanGroup.gameServer = s.gameServer,
						m_clanGroup.nowCycleBossHealth = s.thisLapHPList.dump(),
						m_clanGroup.nextCycleBossHealth = s.nextLapHPList.dump()
					)
					.where(m_clanGroup.groupId == m_groupID)
				);
			}

			inline void Group::updateChanllengerList(const std::function<void(json&)>& func)
			{
				auto db = m_pool->get();
				auto raw = db(
					select(m_clanGroup.challengingMemberList)
					.from(m_clanGroup)
					.where(m_clanGroup.groupId == m_groupID)
				);
				auto list = raw.empty() ? json{} : json::parse(raw.begin()->challengingMemberList.value());
				func(list);
				db(
					update(m_clanGroup)
					.set(m_clanGroup.challengingMemberList = list.dump())
					.where(m_clanGroup.groupId == m_groupID)
				);
			}
		}
	}
}