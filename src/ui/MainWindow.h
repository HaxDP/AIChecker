#pragma once

#include "app/AppController.h"
#include "config/AppSettings.h"

struct GLFWwindow;

namespace ui {

class MainWindow {
public:
    MainWindow(app::AppController& controller, GLFWwindow* window, config::AppSettings& settings, std::string settingsPath);

    void Render();
    void SaveSettings();

private:
    void EnsureUiBuffersInitialized();
    void ApplyTheme();
    void SaveSettingsIfChanged();
    void CheckIntegrationStatus();

    void RenderTopBar();
    void RenderSettingsWindow();
    void RenderDashboardTab();
    void RenderClassroomTab();
    void RenderChecksTab();
    void RenderIntegrationsTab();
    void RenderExportTab();
    void RenderLogs();

    static const char* SubmissionStatusLabel(core::SubmissionStatus status);

    app::AppController& controller_;
    GLFWwindow* window_ = nullptr;
    bool showSettings_ = false;
    bool darkTheme_ = true;
    float uiScale_ = 1.0f;
    config::AppSettings& settings_;
    std::string settingsPath_;

    bool uiBuffersInitialized_ = false;

    char courseIdBuffer_[128] = {};
    char courseWorkIdBuffer_[128] = {};
    char studentFilterBuffer_[128] = {};

    char ollamaUrlBuffer_[256] = {};
    char ollamaModelBuffer_[128] = {};
    char plagiarismServiceBuffer_[256] = {};
    char githubTokenPathBuffer_[260] = {};
    char classroomTokenPathBuffer_[260] = {};
    char exportDirBuffer_[260] = {};
    char exportFileBuffer_[260] = "exports/results.csv";

    std::string classroomStatus_ = "Unknown";
    std::string ollamaStatus_ = "Unknown";
    std::string plagiarismStatus_ = "Unknown";
    std::string githubStatus_ = "Unknown";
};

} // namespace ui
