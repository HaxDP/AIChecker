#pragma once

#include <string>
#include <vector>

namespace core {

enum class SubmissionStatus {
    NotLoaded,
    Cloned,
    Analyzed,
    Synced
};

struct Student {
    std::string fullName;
    std::string email;
    std::string githubUsername;
};

struct Assignment {
    std::string classroomCourseId;
    std::string classroomCourseWorkId;
    std::string classroomStudentGroup;
    std::string githubClassroomUrl;
    double minGrade = 2.0;
    double maxPoints = 5.0;
};

struct AnalysisResult {
    double plagiarismScore = 0.0;
    double aiLikelihoodScore = 0.0;
    int finalGrade = 2;
    std::string summary;
    std::string aiThinking;
    std::string aiIndicators;
    std::string aiConclusion;
    std::string feedbackEmailBody;
};

struct Submission {
    Student student;
    std::string repositoryUrl;
    std::string localPath;
    SubmissionStatus status = SubmissionStatus::NotLoaded;
    AnalysisResult result;
};

struct AppState {
    std::vector<Student> students;
    std::vector<Submission> submissions;
    Assignment assignment;
    std::vector<std::string> logLines;
};

}