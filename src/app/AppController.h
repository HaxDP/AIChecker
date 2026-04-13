#pragma once

#include "core/Architecture.h"
#include "core/SubmissionStatusLabeler.h"
#include "integration/Services.h"

#include <memory>
#include <string>
#include <vector>

namespace app {

struct AppControllerDependencies {
    std::unique_ptr<core::IWorkflowCommand> loadStudentsCommand;
    std::unique_ptr<core::IWorkflowCommand> buildSubmissionsCommand;
    std::unique_ptr<core::IWorkflowCommand> analyzeCommand;
    std::unique_ptr<core::IWorkflowCommand> aiCheckCommand;
    std::unique_ptr<core::IWorkflowCommand> plagiarismCheckCommand;
    std::unique_ptr<core::IWorkflowCommand> syncCommand;
    std::unique_ptr<core::IWorkflowCommand> emailCommand;
    std::unique_ptr<core::ISubmissionStatusLabeler> statusLabeler;
    std::unique_ptr<integration::IClassroomGateway> classroomGateway;
    std::unique_ptr<integration::IEmailGateway> emailGateway;
};

class AppController final : public core::IObserver {
public:
    AppController();
    explicit AppController(AppControllerDependencies dependencies);

    void OnEvent(const std::string& text) override;

    core::AppState& State();
    const core::AppState& State() const;

    void SetUseClassroomApiImport(bool enabled);
    bool UseClassroomApiImport() const;

    void LoadStudents();
    void BuildSubmissions();
    bool EnsureSubmissionsReady();
    void RunAICheckOnly();
    void RunPlagiarismCheckOnly();
    void Analyze();
    void SyncGrades();
    void SendFeedbackEmails();
    bool ExportResultsCsv(const std::string& outputPath);
    bool ExportStudentReports(const std::string& outputRootDir);
    void RunFullPipeline();

    void ClearLogs();

private:
    void LoadStudentsFromApi();
    void LoadStudentsFromLocal();

    core::EventBus bus_;
    core::AppState state_;

    std::unique_ptr<core::IWorkflowCommand> loadStudentsCommand_;
    std::unique_ptr<core::IWorkflowCommand> buildSubmissionsCommand_;
    std::unique_ptr<core::IWorkflowCommand> analyzeCommand_;
    std::unique_ptr<core::IWorkflowCommand> aiCheckCommand_;
    std::unique_ptr<core::IWorkflowCommand> plagiarismCheckCommand_;
    std::unique_ptr<core::IWorkflowCommand> syncCommand_;
    std::unique_ptr<core::IWorkflowCommand> emailCommand_;
    std::unique_ptr<core::ISubmissionStatusLabeler> statusLabeler_;

    std::unique_ptr<integration::IClassroomGateway> classroomGateway_;
    std::unique_ptr<integration::IEmailGateway> emailGateway_;
    bool useClassroomApiImport_ = true;
};

}