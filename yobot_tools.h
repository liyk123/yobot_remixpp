#pragma once
#include "yobot_typedef.h"
namespace yobot {
	namespace clanbattle {
		namespace tools {
			std::int8_t getPhase(const std::int64_t lap, const std::string& gameServer);
			json adaptHPList(const std::ranges::range auto& list);
			std::smatch regexSearch(const std::regex& parten, const std::string& rawStr);
		}
	}

	namespace tools {
		std::string createQQBotCMDInput(const std::string& cmdStr, const std::string_view showStr, const bool refer = false);
		json createQQBotButton(std::string_view id, std::string_view label, std::string_view data, std::uint8_t permission = 2);
		std::string packMarkdown(std::string_view content, const json::array_t& buttons = {});
	}
}
