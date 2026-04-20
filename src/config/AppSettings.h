#pragma once

#include <string>

namespace config {

struct AppSettings {
    std::string ollamaBaseUrl = "http://localhost:11434";
    std::string ollamaModel = "aichecker-llama3.2-3b:latest";
    std::string plagiarismServiceUrl = "";
    std::string githubTokenPath = "settings/github/token.txt";
    std::string classroomTokenPath = "settings/google/access_token.txt";
    std::string exportDirectory = "result";
};

bool LoadAppSettings(const std::string& filePath, AppSettings& settings);
bool SaveAppSettings(const std::string& filePath, const AppSettings& settings);

}