#include "yobot.h"

namespace yobot {
	namespace clanbattle {
		namespace tools {
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

			std::smatch regexSearch(const std::regex& parten, const std::string& rawStr)
			{
				std::smatch matches;
				std::regex_search(rawStr, matches, parten);
				return matches;
			}
		}

		// 模板特化导出以避免链接问题
		namespace _ {
			void _()
			{
				tools::adaptHPList(ordered_json::array_t{});
				tools::adaptHPList(json::array_t{});
			}
		}
	}
}