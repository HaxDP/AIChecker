#pragma once

#include "core/Models.h"
#include "integration/TextUtils.h"

#include <string>

namespace integration {

inline bool MatchesStudentFilter(const core::Student& student, const std::string& group, const std::string& filter) {
    const std::string trimmed = Trim(filter);
    const std::string filterLower = ToLowerAscii(trimmed);
    const bool noFilter = trimmed.empty() || filterLower == "all";
    if (noFilter) {
        return true;
    }

    const bool groupMatch = group == trimmed;
    const bool exactNameMatch = student.fullName == trimmed;
    const bool emailMatch = ToLowerAscii(student.email) == filterLower;
    const bool githubMatch = ToLowerAscii(student.githubUsername) == filterLower;
    return groupMatch || exactNameMatch || emailMatch || githubMatch;
}

}