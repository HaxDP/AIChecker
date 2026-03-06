#include "ui/MainWindow.h"

#include "core/SubmissionStatusLabeler.h"
#include "ui/IntegrationStatusProbe.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstring>
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
    static const core::DefaultSubmissionStatusLabeler labeler;
    return labeler.Label(status);
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
    const IntegrationStatus status = ProbeIntegrationStatus(
        classroomTokenPathBuffer_,
        githubTokenPathBuffer_,
        ollamaUrlBuffer_,
        ollamaModelBuffer_,
        plagiarismServiceBuffer_);
    classroomStatus_ = status.classroom;
    githubStatus_ = status.github;
    ollamaStatus_ = status.ollama;
    plagiarismStatus_ = status.plagiarism;
}

void MainWindow::Render() {
    EnsureUiBuffersInitialized();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("Перевірка робіт", nullptr, flags);

    RenderTopBar();
    ImGui::Separator();

    if (ImGui::BeginTabBar("main_tabs")) {
        if (ImGui::BeginTabItem("Огляд")) {
            RenderDashboardTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Клас")) {
            RenderClassroomTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Перевірки")) {
            RenderChecksTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Інтеграції")) {
            RenderIntegrationsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Експорт")) {
            RenderExportTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Логи")) {
            RenderLogs();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    RenderSettingsWindow();

    ImGui::End();
}

void MainWindow::RenderTopBar() {
    if (ImGui::Button("Запустити повний цикл")) {
        controller_.RunFullPipeline();
    }
    ImGui::SameLine();
    if (ImGui::Button("Перевірити статус")) {
        CheckIntegrationStatus();
        controller_.State().logLines.push_back("[Статус] Стан інтеграцій оновлено.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Очистити логи")) {
        controller_.ClearLogs();
    }
    ImGui::SameLine();
    if (ImGui::Button("Налаштування")) {
        showSettings_ = true;
    }
}

void MainWindow::RenderSettingsWindow() {
    if (!showSettings_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Налаштування", &showSettings_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Тема");
    bool themeChanged = false;
    if (ImGui::RadioButton("Темна", darkTheme_)) {
        darkTheme_ = true;
        themeChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Світла", !darkTheme_)) {
        darkTheme_ = false;
        themeChanged = true;
    }

    ImGui::Spacing();
    ImGui::Text("Розмір інтерфейсу");
    float newScale = uiScale_;
    if (ImGui::SliderFloat("Масштаб UI", &newScale, 0.8f, 1.8f, "%.2f")) {
        uiScale_ = newScale;
        themeChanged = true;
    }

    if (ImGui::Button("Менше вікно")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1200, 700);
            SaveSettingsIfChanged();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Більше вікно")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1920, 1080);
            SaveSettingsIfChanged();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Розмір за замовчуванням")) {
        if (window_ != nullptr) {
            glfwSetWindowSize(window_, 1600, 920);
            SaveSettingsIfChanged();
        }
    }

    ImGui::Separator();
    ImGui::Text("Шкала оцінювання");
    ImGui::Text("Фіксована шкала для цього проєкту: мінімум 2, максимум 5");

    ImGui::Separator();
    ImGui::Text("Інші налаштування");
    bool useApiImport = controller_.UseClassroomApiImport();
    if (ImGui::Checkbox("Завантажувати студентів через Classroom API", &useApiImport)) {
        controller_.SetUseClassroomApiImport(useApiImport);
        SaveSettingsIfChanged();
    }
    ImGui::TextWrapped("Рекомендований порядок: Клас -> Перевірки -> Експорт.");
    ImGui::TextWrapped("Файл налаштувань: %s", settingsPath_.c_str());

    if (themeChanged) {
        ApplyTheme();
        SaveSettingsIfChanged();
    }

    ImGui::End();
}

void MainWindow::RenderDashboardTab() {
    const auto& state = controller_.State();
    ImGui::Text("Огляд робочого процесу викладача");
    ImGui::Separator();

    ImGui::Text("Студенти: %d", static_cast<int>(state.students.size()));
    ImGui::SameLine();
    ImGui::Text("Роботи: %d", static_cast<int>(state.submissions.size()));
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
    ImGui::Text("Перевірено: %d", analyzedCount);
    ImGui::SameLine();
    ImGui::Text("Синхронізовано: %d", syncedCount);

    ImGui::Spacing();
    if (ImGui::Button("Завантажити студентів")) {
        controller_.LoadStudents();
    }
    ImGui::SameLine();
    if (ImGui::Button("Побудувати роботи")) {
        controller_.BuildSubmissions();
    }
    ImGui::SameLine();
    if (ImGui::Button("Перевірити все")) {
        controller_.Analyze();
    }
    ImGui::SameLine();
    if (ImGui::Button("Синхронізувати оцінки")) {
        controller_.SyncGrades();
    }
    ImGui::SameLine();
    if (ImGui::Button("Надіслати email")) {
        controller_.SendFeedbackEmails();
    }

    ImGui::Spacing();
    ImGui::Text("Стан інтеграцій");
    ImGui::BulletText("Classroom API: %s", classroomStatus_.c_str());
    ImGui::BulletText("Ollama (Llama 3.2): %s", ollamaStatus_.c_str());
    ImGui::BulletText("Сервіс плагіату: %s", plagiarismStatus_.c_str());
    ImGui::BulletText("GitHub API: %s", githubStatus_.c_str());
}

void MainWindow::RenderClassroomTab() {
    auto& assignment = controller_.State().assignment;

    const std::string previousCourseId = assignment.classroomCourseId;
    const std::string previousCourseWorkId = assignment.classroomCourseWorkId;
    const std::string previousStudentGroup = assignment.classroomStudentGroup;

    ImGui::Text("Мінімальне введення даних");
    ImGui::InputText("ID курсу Google", courseIdBuffer_, sizeof(courseIdBuffer_));
    ImGui::InputText("ID завдання Google CourseWork", courseWorkIdBuffer_, sizeof(courseWorkIdBuffer_));
    ImGui::InputText("Фільтр студентів (ПІБ/група/email/github)", studentFilterBuffer_, sizeof(studentFilterBuffer_));

    bool useApiImport = controller_.UseClassroomApiImport();
    const bool apiImportChanged = ImGui::Checkbox("Використовувати імпорт через Classroom API", &useApiImport);
    if (apiImportChanged) {
        controller_.SetUseClassroomApiImport(useApiImport);
        SaveSettingsIfChanged();
    }

    assignment.minGrade = 2.0;
    assignment.maxPoints = 5.0;
    ImGui::Text("Діапазон оцінок: %.1f .. %.1f", assignment.minGrade, assignment.maxPoints);

    assignment.classroomCourseId = courseIdBuffer_;
    assignment.classroomCourseWorkId = courseWorkIdBuffer_;
    assignment.classroomStudentGroup = studentFilterBuffer_;

    if (previousCourseId != assignment.classroomCourseId ||
        previousCourseWorkId != assignment.classroomCourseWorkId ||
        previousStudentGroup != assignment.classroomStudentGroup) {
        SaveSettingsIfChanged();
    }

    ImGui::Separator();
    ImGui::Text("Імпортовані студенти");
    const auto& students = controller_.State().students;
    ImGui::Text("Студенти: %d", static_cast<int>(students.size()));

    if (ImGui::BeginTable("students_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("ПІБ");
        ImGui::TableSetupColumn("Email");
        ImGui::TableSetupColumn("GitHub");
        ImGui::TableHeadersRow();

        for (const auto& student : students) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(student.fullName.c_str());
            ImGui::TableSetColumnIndex(1);
            const std::string visibleEmail = student.email.empty()
                ? "Немає (потрібен scope classroom.profile.emails)"
                : student.email;
            ImGui::TextUnformatted(visibleEmail.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(student.githubUsername.c_str());
        }

        ImGui::EndTable();
    }
}

void MainWindow::RenderChecksTab() {
    ImGui::Text("Перевірки");
    if (ImGui::Button("AI перевірка")) {
        controller_.RunAICheckOnly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Перевірка плагіату")) {
        controller_.RunPlagiarismCheckOnly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Повний аналіз (оцінки 2..5)")) {
        controller_.Analyze();
    }

    ImGui::Spacing();
    const auto& submissions = controller_.State().submissions;
    ImGui::Text("Роботи: %d", static_cast<int>(submissions.size()));

    if (ImGui::BeginTable("submissions_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 230.0f))) {
        ImGui::TableSetupColumn("Студент");
        ImGui::TableSetupColumn("Репозиторій");
        ImGui::TableSetupColumn("Плагіат %");
        ImGui::TableSetupColumn("AI %");
        ImGui::TableSetupColumn("Оцінка");
        ImGui::TableSetupColumn("Статус");
        ImGui::TableSetupColumn("Підсумок");
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

    ImGui::Text("Ollama (локальна Llama 3.2)");
    changed |= ImGui::InputText("URL Ollama", ollamaUrlBuffer_, sizeof(ollamaUrlBuffer_));
    changed |= ImGui::InputText("Модель Ollama", ollamaModelBuffer_, sizeof(ollamaModelBuffer_));

    ImGui::Separator();
    ImGui::Text("Сервіс перевірки плагіату");
    changed |= ImGui::InputText("URL сервісу", plagiarismServiceBuffer_, sizeof(plagiarismServiceBuffer_));

    ImGui::Separator();
    ImGui::Text("API токени / шляхи");
    changed |= ImGui::InputText("Шлях токена Classroom", classroomTokenPathBuffer_, sizeof(classroomTokenPathBuffer_));
    changed |= ImGui::InputText("Шлях токена GitHub", githubTokenPathBuffer_, sizeof(githubTokenPathBuffer_));

    if (ImGui::Button("Оновити статус")) {
        CheckIntegrationStatus();
    }

    ImGui::Text("Classroom API: %s", classroomStatus_.c_str());
    ImGui::Text("Ollama: %s", ollamaStatus_.c_str());
    ImGui::Text("Плагіат: %s", plagiarismStatus_.c_str());
    ImGui::Text("GitHub API: %s", githubStatus_.c_str());

    if (changed) {
        CheckIntegrationStatus();
        SaveSettingsIfChanged();
    }
}

void MainWindow::RenderExportTab() {
    ImGui::Text("Експорт результатів");
    bool changed = false;
    changed |= ImGui::InputText("Каталог експорту", exportDirBuffer_, sizeof(exportDirBuffer_));
    changed |= ImGui::InputText("CSV файл", exportFileBuffer_, sizeof(exportFileBuffer_));

    if (ImGui::Button("Експорт CSV (Excel)")) {
        controller_.ExportResultsCsv(exportFileBuffer_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Експорт TXT по студентах")) {
        controller_.ExportStudentReports("result");
    }
    ImGui::TextWrapped("Формат TXT: result/Ім'я-Студента/lab23_result.txt");
    ImGui::TextWrapped("Порада: CSV можна відкрити в Excel і за потреби зберегти як .xlsx.");

    if (changed) {
        SaveSettingsIfChanged();
    }
}

void MainWindow::RenderLogs() {
    const auto& logs = controller_.State().logLines;
    ImGui::Text("Журнал подій");

    const bool hasNewLogs = logs.size() > previousLogCount_;

    if (ImGui::BeginChild("log_panel", ImVec2(0, 170), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : logs) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        if (hasNewLogs && !logs.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    previousLogCount_ = logs.size();
}
}