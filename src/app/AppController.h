#pragma once

#include "core/Architecture.h"
#include "integration/Services.h"

#include <memory>
#include <string>
#include <vector>

namespace app {

class AppController final : public core::IObserver {
public:
    AppController();

    void OnEvent(const std::string& text) override;

    core::AppState& State();
    const core::AppState& State() const;

    void SetUseClassroomApiImport(bool enabled);
    bool UseClassroomApiImport() const;

    void LoadStudents();
    void BuildSubmissions();
    void RunAICheckOnly();
    void RunPlagiarismCheckOnly();
    void Analyze();
    void SyncGrades();
    void SendFeedbackEmails();
    bool ExportResultsCsv(const std::string& outputPath);
    void RunFullPipeline();

    void ClearLogs();

private:
    core::EventBus bus_;
    core::AppState state_;

    std::unique_ptr<core::IWorkflowCommand> loadStudentsCommand_;
    std::unique_ptr<core::IWorkflowCommand> buildSubmissionsCommand_;
    std::unique_ptr<core::IWorkflowCommand> analyzeCommand_;
    std::unique_ptr<core::IWorkflowCommand> syncCommand_;
    std::unique_ptr<core::IWorkflowCommand> emailCommand_;

    std::unique_ptr<integration::IClassroomGateway> classroomGateway_;
    std::unique_ptr<integration::IEmailGateway> emailGateway_;
    bool useClassroomApiImport_ = true;
};

} // namespace app
