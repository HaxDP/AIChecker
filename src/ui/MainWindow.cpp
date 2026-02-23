#include "ui/MainWindow.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace ui {

MainWindow::MainWindow(app::AppController& controller, GLFWwindow* window, config::AppSettings& settings, std::string settingsPath)
    : controller_(controller),
      window_(window),
      darkTheme_(settings.darkTheme),
      uiScale_(settings.uiScale),
      settings_(settings),
      settingsPath_(std::move(settingsPath)) {
    ApplyTheme();
}

const char* MainWindow::SubmissionStatusLabel(core::SubmissionStatus status) {
    switch (status) {
        case core::SubmissionStatus::NotLoaded: return "NotLoaded";
        case core::SubmissionStatus::Cloned: return "Fetched";
        case core::SubmissionStatus::Analyzed: return "Checked";
        case core::SubmissionStatus::Synced: return "Synced";
    }
    return "Unknown";
}

void MainWindow::EnsureUiBuffersInitialized() {
    if (uiBuffersInitialized_) {
        return;
    }

    auto& assignment = controller_.State().assignment;

    strncpy_s(courseIdBuffer_, assignment.classroomCourseId.c_str(), _TRUNCATE);
    strncpy_s(courseWorkIdBuffer_, assignment.classroomCourseWorkId.c_str(), _TRUNCATE);
    strncpy_s(studentFilterBuffer_, assignment.classroomStudentGroup.c_str(), _TRUNCATE);

    strncpy_s(ollamaUrlBuffer_, settings_.ollamaBaseUrl.c_str(), _TRUNCATE);
    strncpy_s(ollamaModelBuffer_, settings_.ollamaModel.c_str(), _TRUNCATE);
    strncpy_s(plagiarismServiceBuffer_, settings_.plagiarismServiceUrl.c_str(), _TRUNCATE);
    strncpy_s(githubTokenPathBuffer_, settings_.githubTokenPath.c_str(), _TRUNCATE);
    strncpy_s(classroomTokenPathBuffer_, settings_.classroomTokenPath.c_str(), _TRUNCATE);
    strncpy_s(exportDirBuffer_, settings_.exportDirectory.c_str(), _TRUNCATE);

    uiBuffersInitialized_ = true;
    CheckIntegrationStatus();
}

void MainWindow::SaveSettings() {
    EnsureUiBuffersInitialized();

    settings_.darkTheme = darkTheme_;
    settings_.uiScale = uiScale_;
    settings_.classroomUseApiImport = controller_.UseClassroomApiImport();
    settings_.ollamaBaseUrl = ollamaUrlBuffer_;
    settings_.ollamaModel = ollamaModelBuffer_;
    settings_.plagiarismServiceUrl = plagiarismServiceBuffer_;
    settings_.githubTokenPath = githubTokenPathBuffer_;
    settings_.classroomTokenPath = classroomTokenPathBuffer_;
    settings_.exportDirectory = exportDirBuffer_;

    if (window_ != nullptr) {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(window_, &width, &height);
        settings_.windowWidth = width;
        settings_.windowHeight = height;
    }

    settings_.classroomCourseId = controller_.State().assignment.classroomCourseId;
    settings_.classroomCourseWorkId = controller_.State().assignment.classroomCourseWorkId;
    settings_.classroomStudentGroup = controller_.State().assignment.classroomStudentGroup;

    config::SaveAppSettings(settingsPath_, settings_);
}

void MainWindow::SaveSettingsIfChanged() {
    SaveSettings();
}

void MainWindow::ApplyTheme() {
    if (darkTheme_) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
    ImGui::GetIO().FontGlobalScale = uiScale_;
}

void MainWindow::CheckIntegrationStatus() {
    const bool hasClassroomToken = std::filesystem::exists(classroomTokenPathBuffer_);
    const bool hasGithubToken = std::filesystem::exists(githubTokenPathBuffer_);
    const bool ollamaConfigured = std::strlen(ollamaUrlBuffer_) > 0 && std::strlen(ollamaModelBuffer_) > 0;
    const bool plagiarismConfigured = std::strlen(plagiarismServiceBuffer_) > 0;

    classroomStatus_ = hasClassroomToken ? "Ready" : "Missing token";
    githubStatus_ = hasGithubToken ? "Ready" : "Missing token";
    ollamaStatus_ = ollamaConfigured ? "Configured" : "Not configured";
    plagiarismStatus_ = plagiarismConfigured ? "Configured" : "Not configured";
}

void MainWindow::Render() {
    EnsureUiBuffersInitialized();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("AI Plagiarism Checker", nullptr, flags);

    RenderTopBar();
    ImGui::Separator();

    if (ImGui::BeginTabBar("main_tabs")) {
        if (ImGui::BeginTabItem("Dashboard")) {
            RenderDashboardTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Classroom")) {
            RenderClassroomTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Checks")) {
            RenderChecksTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Integrations")) {
            RenderIntegrationsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Export")) {
            RenderExportTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Logs")) {
            RenderLogs();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    RenderSettingsWindow();

    ImGui::End();
}

void MainWindow::RenderTopBar() {
    if (ImGui::Button("Run Full Pipeline")) {
        controller_.RunFullPipeline();
    }
    ImGui::SameLine();
    if (ImGui::Button("Status Check")) {
        CheckIntegrationStatus();
        controller_.State().logLines.push_back("[Status] Integration status refreshed.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Logs")) {
        controller_.ClearLogs();
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) {
        showSettings_ = true;
    }
}

void MainWindow::RenderSettingsWindow() {
    if (!showSettings_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &showSettings_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Theme");
    bool themeChanged = false;
    if (ImGui::RadioButton("Black theme", darkTheme_)) {
        darkTheme_ = true;
        themeChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("White theme", !darkTheme_)) {
        darkTheme_ = false;
        themeChanged = true;
    }

    ImGui::Spacing();
    ImGui::Text("UI Size");
    float newScale = uiScale_;
    if (ImGui::SliderFloat("UI scale", &newScale, 0.8f, 1.8f, "%.2f")) {
        uiScale_ = newScale;
        themeChanged = true;
    }

    if (ImGui::Button("Smaller App Window")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1200, 700);
            SaveSettingsIfChanged();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Bigger App Window")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1920, 1080);
            SaveSettingsIfChanged();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Default Size")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1600, 920);
            SaveSettingsIfChanged();
        }
    }

    ImGui::Separator();
    ImGui::Text("Grade Scale");
    ImGui::Text("Fixed scale for this project: minimum 2, maximum 5");

    ImGui::Separator();
    ImGui::Text("Other settings");
    bool useApiImport = controller_.UseClassroomApiImport();
    if (ImGui::Checkbox("Load students via Classroom API", &useApiImport)) {
        controller_.SetUseClassroomApiImport(useApiImport);
        SaveSettingsIfChanged();
    }
    ImGui::TextWrapped("For daily work, use tabs: Classroom -> Checks -> Export.");
    ImGui::TextWrapped("Settings file: %s", settingsPath_.c_str());

    if (themeChanged) {
        ApplyTheme();
        SaveSettingsIfChanged();
    }

    ImGui::End();
}

void MainWindow::RenderDashboardTab() {
    const auto& state = controller_.State();
    ImGui::Text("Teacher Workflow Overview");
    ImGui::Separator();

    ImGui::Text("Students: %d", static_cast<int>(state.students.size()));
    ImGui::SameLine();
    ImGui::Text("Submissions: %d", static_cast<int>(state.submissions.size()));
    ImGui::SameLine();

    int analyzedCount = 0;
    int syncedCount = 0;
    for (const auto& submission : state.submissions) {
        if (submission.status == core::SubmissionStatus::Analyzed || submission.status == core::SubmissionStatus::Synced) {
            ++analyzedCount;
        }
        if (submission.status == core::SubmissionStatus::Synced) {
            ++syncedCount;
        }
    }
    ImGui::Text("Analyzed: %d", analyzedCount);
    ImGui::SameLine();
    ImGui::Text("Synced: %d", syncedCount);

    ImGui::Spacing();
    if (ImGui::Button("Load Students")) {
        controller_.LoadStudents();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build Submissions")) {
        controller_.BuildSubmissions();
    }
    ImGui::SameLine();
    if (ImGui::Button("Analyze All")) {
        controller_.Analyze();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sync Grades")) {
        controller_.SyncGrades();
    }
    ImGui::SameLine();
    if (ImGui::Button("Send Email")) {
        controller_.SendFeedbackEmails();
    }

    ImGui::Spacing();
    ImGui::Text("Integration status");
    ImGui::BulletText("Classroom API: %s", classroomStatus_.c_str());
    ImGui::BulletText("Ollama (Llama 3.2): %s", ollamaStatus_.c_str());
    ImGui::BulletText("Plagiarism service: %s", plagiarismStatus_.c_str());
    ImGui::BulletText("GitHub API: %s", githubStatus_.c_str());
}

void MainWindow::RenderClassroomTab() {
    auto& assignment = controller_.State().assignment;

    const std::string previousCourseId = assignment.classroomCourseId;
    const std::string previousCourseWorkId = assignment.classroomCourseWorkId;
    const std::string previousStudentGroup = assignment.classroomStudentGroup;

    ImGui::Text("Minimal teacher input");
    ImGui::InputText("Google Course ID", courseIdBuffer_, sizeof(courseIdBuffer_));
    ImGui::InputText("Google CourseWork ID", courseWorkIdBuffer_, sizeof(courseWorkIdBuffer_));
    ImGui::InputText("Student filter (name/group/email/github)", studentFilterBuffer_, sizeof(studentFilterBuffer_));

    bool useApiImport = controller_.UseClassroomApiImport();
    const bool apiImportChanged = ImGui::Checkbox("Use Classroom API import", &useApiImport);
    if (apiImportChanged) {
        controller_.SetUseClassroomApiImport(useApiImport);
        SaveSettingsIfChanged();
    }

    assignment.minGrade = 2.0;
    assignment.maxPoints = 5.0;
    ImGui::Text("Grade range: %.1f .. %.1f", assignment.minGrade, assignment.maxPoints);

    assignment.classroomCourseId = courseIdBuffer_;
    assignment.classroomCourseWorkId = courseWorkIdBuffer_;
    assignment.classroomStudentGroup = studentFilterBuffer_;

    if (previousCourseId != assignment.classroomCourseId ||
        previousCourseWorkId != assignment.classroomCourseWorkId ||
        previousStudentGroup != assignment.classroomStudentGroup) {
        SaveSettingsIfChanged();
    }

    ImGui::Separator();
    ImGui::Text("Imported students");
    const auto& students = controller_.State().students;
    ImGui::Text("Students: %d", static_cast<int>(students.size()));

    if (ImGui::BeginTable("students_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Full Name");
        ImGui::TableSetupColumn("Email");
        ImGui::TableSetupColumn("GitHub");
        ImGui::TableHeadersRow();

        for (const auto& student : students) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(student.fullName.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(student.email.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(student.githubUsername.c_str());
        }

        ImGui::EndTable();
    }
}

void MainWindow::RenderChecksTab() {
    ImGui::Text("Checks");
    if (ImGui::Button("AI Check")) {
        controller_.RunAICheckOnly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Plagiarism Check")) {
        controller_.RunPlagiarismCheckOnly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Full Analyze (grade 2..5)")) {
        controller_.Analyze();
    }

    ImGui::Spacing();
    const auto& submissions = controller_.State().submissions;
    ImGui::Text("Submissions: %d", static_cast<int>(submissions.size()));

    if (ImGui::BeginTable("submissions_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 230.0f))) {
        ImGui::TableSetupColumn("Student");
        ImGui::TableSetupColumn("Repository");
        ImGui::TableSetupColumn("Plagiarism %");
        ImGui::TableSetupColumn("AI %");
        ImGui::TableSetupColumn("Grade");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Summary");
        ImGui::TableHeadersRow();

        for (const auto& submission : submissions) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(submission.student.fullName.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(submission.repositoryUrl.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", submission.result.plagiarismScore);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", submission.result.aiLikelihoodScore);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", submission.result.finalGrade);

            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(SubmissionStatusLabel(submission.status));

            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(submission.result.summary.c_str());
        }

        ImGui::EndTable();
    }
}

void MainWindow::RenderIntegrationsTab() {
    bool changed = false;

    ImGui::Text("Ollama (Local Llama 3.2)");
    changed |= ImGui::InputText("Ollama URL", ollamaUrlBuffer_, sizeof(ollamaUrlBuffer_));
    changed |= ImGui::InputText("Ollama model", ollamaModelBuffer_, sizeof(ollamaModelBuffer_));

    ImGui::Separator();
    ImGui::Text("Plagiarism service");
    changed |= ImGui::InputText("Service URL", plagiarismServiceBuffer_, sizeof(plagiarismServiceBuffer_));

    ImGui::Separator();
    ImGui::Text("API tokens / paths");
    changed |= ImGui::InputText("Classroom token path", classroomTokenPathBuffer_, sizeof(classroomTokenPathBuffer_));
    changed |= ImGui::InputText("GitHub token path", githubTokenPathBuffer_, sizeof(githubTokenPathBuffer_));

    if (ImGui::Button("Refresh status")) {
        CheckIntegrationStatus();
    }

    ImGui::Text("Classroom API: %s", classroomStatus_.c_str());
    ImGui::Text("Ollama: %s", ollamaStatus_.c_str());
    ImGui::Text("Plagiarism: %s", plagiarismStatus_.c_str());
    ImGui::Text("GitHub API: %s", githubStatus_.c_str());

    if (changed) {
        CheckIntegrationStatus();
        SaveSettingsIfChanged();
    }
}

void MainWindow::RenderExportTab() {
    ImGui::Text("Export results");
    bool changed = false;
    changed |= ImGui::InputText("Export directory", exportDirBuffer_, sizeof(exportDirBuffer_));
    changed |= ImGui::InputText("Output CSV file", exportFileBuffer_, sizeof(exportFileBuffer_));

    if (ImGui::Button("Export Excel-compatible CSV")) {
        controller_.ExportResultsCsv(exportFileBuffer_);
    }
    ImGui::TextWrapped("Tip: open CSV in Excel and save as .xlsx if needed.");

    if (changed) {
        SaveSettingsIfChanged();
    }
}

void MainWindow::RenderLogs() {
    const auto& logs = controller_.State().logLines;
    ImGui::Text("Event Log");

    if (ImGui::BeginChild("log_panel", ImVec2(0, 170), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : logs) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        if (!logs.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}
}