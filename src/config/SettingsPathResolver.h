#pragma once

#include <filesystem>
#include <string>

namespace config {

inline std::string ResolveSettingsPath() {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path currentDir = fs::current_path(ec);
    if (!ec) {
        fs::path searchDir = currentDir;
        for (int depth = 0; depth < 8; ++depth) {
            const fs::path settingsCandidate = searchDir / "settings" / "app_settings.cfg";
            const fs::path cmakeCandidate = searchDir / "CMakeLists.txt";

            std::error_code fileEc;
            const bool hasSettings = fs::exists(settingsCandidate, fileEc) && !fileEc;
            fileEc.clear();
            const bool hasCmake = fs::exists(cmakeCandidate, fileEc) && !fileEc;
            if (hasSettings && hasCmake) {
                return settingsCandidate.lexically_normal().string();
            }

            if (!searchDir.has_parent_path()) {
                break;
            }
            const fs::path parent = searchDir.parent_path();
            if (parent == searchDir) {
                break;
            }
            searchDir = parent;
        }
    }

    return (fs::path("settings") / "app_settings.cfg").string();
}

}