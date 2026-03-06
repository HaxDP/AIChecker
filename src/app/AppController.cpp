#include "app/AppController.h"
#include "app/AppControllerDependencyPolicy.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace app {

namespace {

bool TryDecodeUtf8(const std::string& text, std::size_t& index, char32_t& codePoint) {
    if (index >= text.size()) {
        return false;
    }

    const unsigned char c0 = static_cast<unsigned char>(text[index]);
    if ((c0 & 0x80) == 0) {
        codePoint = c0;
        ++index;
        return true;
    }

    if ((c0 & 0xE0) == 0xC0) {
        if (index + 1 >= text.size()) {
            ++index;
            return false;
        }
        const unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
        if ((c1 & 0xC0) != 0x80) {
            ++index;
            return false;
        }
        codePoint = static_cast<char32_t>(((c0 & 0x1F) << 6) | (c1 & 0x3F));
        index += 2;
        return true;
    }

    if ((c0 & 0xF0) == 0xE0) {
        if (index + 2 >= text.size()) {
            ++index;
            return false;
        }
        const unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
        const unsigned char c2 = static_cast<unsigned char>(text[index + 2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            ++index;
            return false;
        }
        codePoint = static_cast<char32_t>(((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F));
        index += 3;
        return true;
    }

    ++index;
    return false;
}

const std::unordered_map<char32_t, std::string>& TransliterationTable() {
    static const std::unordered_map<char32_t, std::string> table = {
        {U'А', "A"}, {U'а', "a"}, {U'Б', "B"}, {U'б', "b"}, {U'В', "V"}, {U'в', "v"},
        {U'Г', "H"}, {U'г', "h"}, {U'Ґ', "G"}, {U'ґ', "g"}, {U'Д', "D"}, {U'д', "d"},
        {U'Е', "E"}, {U'е', "e"}, {U'Є', "Ye"}, {U'є', "ie"}, {U'Ж', "Zh"}, {U'ж', "zh"},
        {U'З', "Z"}, {U'з', "z"}, {U'И', "Y"}, {U'и', "y"}, {U'І', "I"}, {U'і', "i"},
        {U'Ї', "Yi"}, {U'ї', "yi"}, {U'Й', "Y"}, {U'й', "y"}, {U'К', "K"}, {U'к', "k"},
        {U'Л', "L"}, {U'л', "l"}, {U'М', "M"}, {U'м', "m"}, {U'Н', "N"}, {U'н', "n"},
        {U'О', "O"}, {U'о', "o"}, {U'П', "P"}, {U'п', "p"}, {U'Р', "R"}, {U'р', "r"},
        {U'С', "S"}, {U'с', "s"}, {U'Т', "T"}, {U'т', "t"}, {U'У', "U"}, {U'у', "u"},
        {U'Ф', "F"}, {U'ф', "f"}, {U'Х', "Kh"}, {U'х', "kh"}, {U'Ц', "Ts"}, {U'ц', "ts"},
        {U'Ч', "Ch"}, {U'ч', "ch"}, {U'Ш', "Sh"}, {U'ш', "sh"}, {U'Щ', "Shch"}, {U'щ', "shch"},
        {U'Ь', ""}, {U'ь', ""}, {U'Ю', "Yu"}, {U'ю', "yu"}, {U'Я', "Ya"}, {U'я', "ya"},
        {U'Ъ', ""}, {U'ъ', ""}
    };
    return table;
}

std::string SanitizeDirectoryName(const std::string& input) {
    std::string transliterated;
    transliterated.reserve(input.size() * 2);

    const auto& table = TransliterationTable();
    std::size_t index = 0;
    while (index < input.size()) {
        char32_t codePoint = 0;
        if (!TryDecodeUtf8(input, index, codePoint)) {
            continue;
        }

        if (codePoint < 128) {
            const char symbol = static_cast<char>(codePoint);
            const unsigned char usymbol = static_cast<unsigned char>(symbol);
            if (std::isalnum(usymbol) != 0 || symbol == '-' || symbol == '_') {
                transliterated.push_back(symbol);
            } else if (std::isspace(usymbol) != 0 || symbol == '/' || symbol == '\\' || symbol == ':' || symbol == '.') {
                transliterated.push_back('-');
            }
            continue;
        }

        const auto it = table.find(codePoint);
        if (it != table.end()) {
            transliterated += it->second;
        }
    }

    std::string value;
    value.reserve(transliterated.size());
    bool previousDash = false;
    for (char symbol : transliterated) {
        if (symbol == '-') {
            if (!previousDash) {
                value.push_back('-');
                previousDash = true;
            }
            continue;
        }

        value.push_back(symbol);
        previousDash = false;
    }

    while (!value.empty() && value.back() == '-') {
        value.pop_back();
    }
    while (!value.empty() && value.front() == '-') {
        value.erase(value.begin());
    }

    return value.empty() ? std::string("student") : value;
}

std::string RiskLevelLabel(double score) {
    if (score >= 70.0) {
        return "Високий";
    }
    if (score >= 40.0) {
        return "Середній";
    }
    return "Низький";
}

std::string VisibleEmail(const std::string& email) {
    if (!email.empty()) {
        return email;
    }
    return "Немає email (додайте scope classroom.profile.emails)";
}

std::string ExtractRepositorySlug(const std::string& repositoryUrl) {
    if (repositoryUrl.empty()) {
        return {};
    }

    std::string slug = repositoryUrl;
    const auto slashPos = slug.find_last_of('/');
    if (slashPos != std::string::npos && slashPos + 1 < slug.size()) {
        slug = slug.substr(slashPos + 1);
    }
    if (slug.size() > 4 && slug.substr(slug.size() - 4) == ".git") {
        slug = slug.substr(0, slug.size() - 4);
    }
    return slug;
}

} // namespace

AppController::AppController()
    : AppController(BuildDefaultDependencies()) {}

AppController::AppController(AppControllerDependencies dependencies)
    : loadStudentsCommand_(),
      buildSubmissionsCommand_(),
      analyzeCommand_(),
      aiCheckCommand_(),
      plagiarismCheckCommand_(),
      syncCommand_(),
      emailCommand_(),
      statusLabeler_(),
      classroomGateway_(),
      emailGateway_() {
    dependencies = EnsureDefaults(std::move(dependencies));
    loadStudentsCommand_ = std::move(dependencies.loadStudentsCommand);
    buildSubmissionsCommand_ = std::move(dependencies.buildSubmissionsCommand);
    analyzeCommand_ = std::move(dependencies.analyzeCommand);
    aiCheckCommand_ = std::move(dependencies.aiCheckCommand);
    plagiarismCheckCommand_ = std::move(dependencies.plagiarismCheckCommand);
    syncCommand_ = std::move(dependencies.syncCommand);
    emailCommand_ = std::move(dependencies.emailCommand);
    statusLabeler_ = std::move(dependencies.statusLabeler);
    classroomGateway_ = std::move(dependencies.classroomGateway);
    emailGateway_ = std::move(dependencies.emailGateway);

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
        LoadStudentsFromApi();
        return;
    }

    LoadStudentsFromLocal();
}

void AppController::LoadStudentsFromApi() {
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
        message = "[Classroom API] Не вдалося імпортувати студентів.";
    }
    state_.logLines.push_back(message + " Локальний fallback у режимі API вимкнений.");
}

void AppController::LoadStudentsFromLocal() {
    loadStudentsCommand_->Execute(state_, bus_);
}

void AppController::BuildSubmissions() {
    buildSubmissionsCommand_->Execute(state_, bus_);
}

bool AppController::EnsureSubmissionsReady() {
    if (!state_.submissions.empty()) {
        return true;
    }

    if (state_.students.empty()) {
        LoadStudents();
    }

    if (state_.students.empty()) {
        bus_.Emit("Немає студентів. Спочатку завантажте список.");
        return false;
    }

    BuildSubmissions();
    if (state_.submissions.empty()) {
        bus_.Emit("Після побудови не знайдено жодної роботи.");
        return false;
    }

    return true;
}

void AppController::RunAICheckOnly() {
    if (!EnsureSubmissionsReady()) {
        return;
    }

    aiCheckCommand_->Execute(state_, bus_);
}

void AppController::RunPlagiarismCheckOnly() {
    if (!EnsureSubmissionsReady()) {
        return;
    }

    plagiarismCheckCommand_->Execute(state_, bus_);
}

void AppController::Analyze() {
    if (!EnsureSubmissionsReady()) {
        return;
    }

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
    std::filesystem::path path(outputPath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream file(outputPath, std::ios::trunc);
    if (!file.is_open()) {
        state_.logLines.push_back("[Експорт] Не вдалося відкрити файл: " + outputPath);
        return false;
    }

    file << "ПІБ,Email,GitHub,Плагіат,AI Ризик,Оцінка,Статус,Підсумок\n";
    for (const auto& submission : state_.submissions) {
        file << '"' << submission.student.fullName << "\",";
        file << '"' << VisibleEmail(submission.student.email) << "\",";
        file << '"' << submission.student.githubUsername << "\",";
        file << submission.result.plagiarismScore << ',';
        file << submission.result.aiLikelihoodScore << ',';
        file << submission.result.finalGrade << ',';
        file << '"' << statusLabeler_->Label(submission.status) << "\",";
        file << '"' << submission.result.summary << "\"\n";
    }

    state_.logLines.push_back("[Експорт] CSV збережено: " + outputPath + " (сумісно з Excel)");
    return true;
}

bool AppController::ExportStudentReports(const std::string& outputRootDir) {
    const std::filesystem::path root = outputRootDir.empty() ? std::filesystem::path("result") : std::filesystem::path(outputRootDir);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);

    if (ec) {
        state_.logLines.push_back("[Експорт] Не вдалося створити каталог: " + root.string());
        return false;
    }

    int exportedCount = 0;
    for (const auto& submission : state_.submissions) {
        const std::string repositorySlug = ExtractRepositorySlug(submission.repositoryUrl);
        const std::string folderSource = repositorySlug.empty() ? submission.student.fullName : repositorySlug;
        const std::string studentDirName = SanitizeDirectoryName(folderSource);
        const std::filesystem::path studentDir = root / studentDirName;
        std::filesystem::create_directories(studentDir, ec);
        if (ec) {
            state_.logLines.push_back("[Експорт] Помилка створення каталогу: " + studentDir.string());
            continue;
        }

        const std::filesystem::path reportPath = studentDir / "lab23_result.txt";
        std::ofstream report(reportPath.string(), std::ios::trunc);
        if (!report.is_open()) {
            state_.logLines.push_back("[Експорт] Не вдалося записати звіт: " + reportPath.string());
            continue;
        }

        report << "Звіт перевірки лабораторної роботи\n";
        report << "=================================\n\n";
        report << "Студент: " << submission.student.fullName << "\n";
        report << "Email: " << VisibleEmail(submission.student.email) << "\n";
        report << "GitHub: " << submission.student.githubUsername << "\n";
        report << "Репозиторій: " << submission.repositoryUrl << "\n\n";

        report << "1) Як AI перевіряв роботу:\n";
        report << submission.result.aiThinking << "\n\n";

        report << "2) Що вказує на AI / не-AI:\n";
        report << submission.result.aiIndicators << "\n";
        report << "Рішення: " << submission.result.aiConclusion << "\n\n";

        report << std::fixed << std::setprecision(1);
        report << "3) Оцінка:\n";
        report << "Підсумкова оцінка: " << submission.result.finalGrade << "\n\n";

        report << "4) Коментар для email:\n";
        report << submission.result.feedbackEmailBody << "\n\n";

        report << "Додаткові результати:\n";
        report << "- AI ризик: " << submission.result.aiLikelihoodScore << "% (" << RiskLevelLabel(submission.result.aiLikelihoodScore) << ")\n";
        report << "- Ризик плагіату: " << submission.result.plagiarismScore << "% (" << RiskLevelLabel(submission.result.plagiarismScore) << ")\n";
        report << "- Статус: " << statusLabeler_->Label(submission.status) << "\n";
        report << "- Підсумок: " << submission.result.summary << "\n";

        ++exportedCount;
    }

    state_.logLines.push_back("[Експорт] TXT-звіти сформовано: " + std::to_string(exportedCount) + " (каталог: " + root.string() + ")");
    return exportedCount > 0;
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
