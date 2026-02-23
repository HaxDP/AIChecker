#include "app/AppController.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace app {

AppController::AppController()
    : loadStudentsCommand_(std::make_unique<core::LoadStudentsCommand>()),
      buildSubmissionsCommand_(std::make_unique<core::BuildSubmissionsCommand>()),
      analyzeCommand_(std::make_unique<core::AnalyzeSubmissionsCommand>(core::StrategyFactory::BuildDefault())),
      syncCommand_(std::make_unique<core::SyncGradesCommand>()),
      emailCommand_(std::make_unique<core::SendEmailFeedbackCommand>()),
      classroomGateway_(std::make_unique<integration::ClassroomGatewayStub>()),
      emailGateway_(std::make_unique<integration::EmailGatewayStub>()) {
    bus_.Subscribe(this);

    state_.assignment.classroomCourseId = "course-001";
    state_.assignment.classroomCourseWorkId = "cw-001";
    state_.assignment.classroomStudentGroup = "";
    state_.assignment.githubClassroomUrl = "https://classroom.github.com/a/demo";
    state_.assignment.minGrade = 2.0;
    state_.assignment.maxPoints = 5.0;
}

void AppController::OnEvent(const std::string& text) {
    state_.logLines.push_back(text);
}

core::AppState& AppController::State() {
    return state_;
}

const core::AppState& AppController::State() const {
    return state_;
}

void AppController::SetUseClassroomApiImport(bool enabled) {
    useClassroomApiImport_ = enabled;
}

bool AppController::UseClassroomApiImport() const {
    return useClassroomApiImport_;
}

void AppController::LoadStudents() {
    if (useClassroomApiImport_) {
        std::vector<core::Student> students;
        std::string message;
        const bool ok = classroomGateway_->FetchStudents(
            state_.assignment.classroomCourseId,
            state_.assignment.classroomCourseWorkId,
            state_.assignment.classroomStudentGroup,
            students,
            message);

        if (ok) {
            state_.students = std::move(students);
            state_.logLines.push_back(message);
            return;
        }

        state_.students.clear();
        if (message.empty()) {
            message = "[Classroom API] failed to export students.";
        }
        state_.logLines.push_back(message + " No local fallback in API mode.");
        return;
    }

    loadStudentsCommand_->Execute(state_, bus_);
}

void AppController::BuildSubmissions() {
    buildSubmissionsCommand_->Execute(state_, bus_);
}

void AppController::RunAICheckOnly() {
    if (state_.submissions.empty()) {
        state_.logLines.push_back("[AI Check] No submissions to analyze.");
        return;
    }

    core::StylometryAIStrategy stylometry;
    core::PromptLeakHeuristicStrategy promptLeak;

    for (auto& submission : state_.submissions) {
        stylometry.Analyze(submission);
        promptLeak.Analyze(submission);
        submission.result.summary = "AI-only score=" + std::to_string(submission.result.aiLikelihoodScore);
    }

    state_.logLines.push_back("[AI Check] Completed local AI-likelihood analysis.");
}

void AppController::RunPlagiarismCheckOnly() {
    if (state_.submissions.empty()) {
        state_.logLines.push_back("[Plagiarism Check] No submissions to analyze.");
        return;
    }

    core::NgramSimilarityStrategy ngram;

    for (auto& submission : state_.submissions) {
        ngram.Analyze(submission);
        submission.result.summary = "Plagiarism-only score=" + std::to_string(submission.result.plagiarismScore);
    }

    state_.logLines.push_back("[Plagiarism Check] Completed similarity analysis.");
}

void AppController::Analyze() {
    analyzeCommand_->Execute(state_, bus_);
}

void AppController::SyncGrades() {
    syncCommand_->Execute(state_, bus_);

    for (const auto& submission : state_.submissions) {
        std::string message;
        classroomGateway_->PushGrade(submission, state_.assignment, message);
        state_.logLines.push_back(message);
    }
}

void AppController::SendFeedbackEmails() {
    emailCommand_->Execute(state_, bus_);

    for (const auto& submission : state_.submissions) {
        std::string message;
        emailGateway_->SendFeedback(submission, message);
        state_.logLines.push_back(message);
    }
}

bool AppController::ExportResultsCsv(const std::string& outputPath) {
    auto statusLabel = [](core::SubmissionStatus status) {
        switch (status) {
            case core::SubmissionStatus::NotLoaded: return "NotLoaded";
            case core::SubmissionStatus::Cloned: return "Fetched";
            case core::SubmissionStatus::Analyzed: return "Checked";
            case core::SubmissionStatus::Synced: return "Synced";
        }
        return "Unknown";
    };

    std::filesystem::path path(outputPath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream file(outputPath, std::ios::trunc);
    if (!file.is_open()) {
        state_.logLines.push_back("[Export] Failed to open file: " + outputPath);
        return false;
    }

    file << "Full Name,Email,GitHub,Plagiarism,AI Risk,Grade,Status,Summary\n";
    for (const auto& submission : state_.submissions) {
        file << '"' << submission.student.fullName << "\",";
        file << '"' << submission.student.email << "\",";
        file << '"' << submission.student.githubUsername << "\",";
        file << submission.result.plagiarismScore << ',';
        file << submission.result.aiLikelihoodScore << ',';
        file << submission.result.finalGrade << ',';
        file << '"' << statusLabel(submission.status) << "\",";
        file << '"' << submission.result.summary << "\"\n";
    }

    state_.logLines.push_back("[Export] CSV exported: " + outputPath + " (Excel compatible)");
    return true;
}

void AppController::RunFullPipeline() {
    LoadStudents();
    BuildSubmissions();
    Analyze();
    SyncGrades();
    SendFeedbackEmails();
}

void AppController::ClearLogs() {
    state_.logLines.clear();
}

} // namespace app
