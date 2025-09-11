#pragma once
#include "yobot_typedef.h"
namespace yobot {
    namespace clanbattle {
        RegexAction showProgress();
        RegexAction setProgress();
        RegexAction resetProgress();
        RegexAction applyForChallenge();
        RegexAction cancelApplyForChallenge();
        RegexAction reportChallenge();
        RegexAction cancelReportChallenge();
    }
}