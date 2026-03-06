#pragma once

#include <string>

namespace config {

struct AppSettings {
    bool darkTheme = true;
    float uiScale = 1.0f;
    int windowWidth = 1600;
    int windowHeight = 920;

    bool classroomUseApiImport = true;
    std::string classroomCourseId = "course-001";
    std::string classroomCourseWorkId = "cw-001";
    std::string classroomStudentGroup = "";

    std::string ollamaBaseUrl = "http://localhost:11434";
    std::string ollamaModel = "llama3.2:3b";
    std::string plagiarismServiceUrl = "";
    std::string githubTokenPath = "settings/github/token.txt";
    std::string classroomTokenPath = "settings/google/access_token.txt";
    std::string exportDirectory = "result";
};

bool LoadAppSettings(const std::string& filePath, AppSettings& settings);
bool SaveAppSettings(const std::string& filePath, const AppSettings& settings);

} // namespace config
