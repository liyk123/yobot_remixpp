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
}
