#pragma once
#include "yobot_typedef.h"
namespace yobot {
    namespace clanbattle {
        RegexAction createClan();
        RegexAction joinClan();
        RegexAction showProgress();
        RegexAction showStatus();
        RegexAction showPanel();
        RegexAction setProgress();
        RegexAction resetProgress();
        RegexAction applyForChallenge();
        RegexAction cancelApplyForChallenge();
        RegexAction reportChallenge();
        RegexAction cancelReportChallenge();
    }
}