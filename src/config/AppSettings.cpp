#include "config/AppSettings.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ParseBool(const std::string& value, bool defaultValue) {
    const std::string normalized = Trim(value);
    if (normalized == "1" || normalized == "true" || normalized == "True") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "False") {
        return false;
    }
    return defaultValue;
}

int ParseInt(const std::string& value, int defaultValue) {
    try {
        return std::stoi(Trim(value));
    } catch (...) {
        return defaultValue;
    }
}

float ParseFloat(const std::string& value, float defaultValue) {
    try {
        return std::stof(Trim(value));
    } catch (...) {
        return defaultValue;
    }
}

} // namespace

namespace config {

bool LoadAppSettings(const std::string& filePath, AppSettings& settings) {
    std::ifstream input(filePath);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto equalsPos = trimmed.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, equalsPos));
        const std::string value = Trim(trimmed.substr(equalsPos + 1));

        if (key == "theme.dark") {
            settings.darkTheme = ParseBool(value, settings.darkTheme);
        } else if (key == "ui.scale") {
            settings.uiScale = ParseFloat(value, settings.uiScale);
        } else if (key == "window.width") {
            settings.windowWidth = ParseInt(value, settings.windowWidth);
        } else if (key == "window.height") {
            settings.windowHeight = ParseInt(value, settings.windowHeight);
        } else if (key == "classroom.useApiImport") {
            settings.classroomUseApiImport = ParseBool(value, settings.classroomUseApiImport);
        } else if (key == "classroom.courseId") {
            settings.classroomCourseId = value;
        } else if (key == "classroom.courseWorkId") {
            settings.classroomCourseWorkId = value;
        } else if (key == "classroom.studentGroup") {
            settings.classroomStudentGroup = value;
        } else if (key == "ollama.baseUrl") {
            settings.ollamaBaseUrl = value;
        } else if (key == "ollama.model") {
            settings.ollamaModel = value;
        } else if (key == "plagiarism.serviceUrl") {
            settings.plagiarismServiceUrl = value;
        } else if (key == "github.tokenPath") {
            settings.githubTokenPath = value;
        } else if (key == "classroom.tokenPath") {
            settings.classroomTokenPath = value;
        } else if (key == "export.directory") {
            settings.exportDirectory = value;
        }
    }

    return true;
}

bool SaveAppSettings(const std::string& filePath, const AppSettings& settings) {
    std::filesystem::path path(filePath);
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        (void)ec;
    }

    std::ofstream output(filePath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "# AIChecker settings\n";
    output << "theme.dark=" << (settings.darkTheme ? "true" : "false") << "\n";
    output << "ui.scale=" << settings.uiScale << "\n";
    output << "window.width=" << settings.windowWidth << "\n";
    output << "window.height=" << settings.windowHeight << "\n";
    output << "classroom.useApiImport=" << (settings.classroomUseApiImport ? "true" : "false") << "\n";
    output << "classroom.courseId=" << settings.classroomCourseId << "\n";
    output << "classroom.courseWorkId=" << settings.classroomCourseWorkId << "\n";
    output << "classroom.studentGroup=" << settings.classroomStudentGroup << "\n";
    output << "ollama.baseUrl=" << settings.ollamaBaseUrl << "\n";
    output << "ollama.model=" << settings.ollamaModel << "\n";
    output << "plagiarism.serviceUrl=" << settings.plagiarismServiceUrl << "\n";
    output << "github.tokenPath=" << settings.githubTokenPath << "\n";
    output << "classroom.tokenPath=" << settings.classroomTokenPath << "\n";
    output << "export.directory=" << settings.exportDirectory << "\n";

    return true;
}

} // namespace config
