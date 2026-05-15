#include <httplib.h>
#include <format>
#include "yobot.h"

constexpr std::string_view StrIArray[] = { "1","2","3","4","5" };

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
					ret[StrIArray[i]] = list[i];
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

	namespace tools {
		std::string createQQBotCMDInput(const std::string& cmdStr, const std::string_view showStr, const bool refer = false)
		{
			return std::format(R"(<qqbot-cmd-input text="{}" show="{}" reference="{}" />)", httplib::encode_uri(cmdStr), showStr, refer);
		}

		json createQQBotButton(std::string_view id, std::string_view label, std::string_view data, std::uint8_t permission = 2)
		{
			return json{
				{"id", id},
				{"render_data", {{"label",label}, {"visited_label", label}, {"style", 1}}},
				{
					"action", {
						{"type",2},
						{"permission", {{"type", permission}}},
						{"unsupport_tips", "操作不支持"},
						{"data", data}
					}
				}
			};
		}

		std::string packMarkdown(std::string_view content, const json::array_t& buttons = {})
		{
			json data{};
			data.emplace("markdown", json::object()).first->emplace("content", content);
			data.emplace("keyboard", json::object()).first->emplace("content", json::object()).first->emplace("rows", buttons);
			return std::format("[CQ:markdown,data=base64://{}]", httplib::detail::base64_encode(data.dump()));
		}
	}
}
