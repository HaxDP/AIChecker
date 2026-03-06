#include "app/AppController.h"
#include "config/AppSettings.h"
#include "config/SettingsPathResolver.h"
#include "core/AnalysisStrategies.h"
#include "core/WorkflowCommands.h"
#include "integration/ClassroomStudentExporter.h"
#include "integration/Services.h"
#include "integration/TokenProvider.h"
#include "ui/MainWindow.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string ResolvePathFromSettings(const std::string& settingsPath, const std::string& configuredPath) {
    namespace fs = std::filesystem;

    auto collapseDuplicateSettings = [](const fs::path& input) {
        fs::path output;
        std::string previousPart;
        for (const auto& part : input) {
            const std::string currentPart = part.string();
            if (previousPart == "settings" && currentPart == "settings") {
                continue;
            }
            output /= part;
            previousPart = currentPart;
        }
        return output;
    };

    fs::path path = collapseDuplicateSettings(fs::path(configuredPath));
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    const fs::path settingsDir = fs::path(settingsPath).parent_path();
    const fs::path projectRoot = settingsDir.filename() == "settings" ? settingsDir.parent_path() : settingsDir;
    return collapseDuplicateSettings(projectRoot / path).lexically_normal().string();
}

} // namespace

int main() {
    const std::string settingsPath = config::ResolveSettingsPath();
    config::AppSettings settings;
    config::LoadAppSettings(settingsPath, settings);

    settings.classroomTokenPath = ResolvePathFromSettings(settingsPath, settings.classroomTokenPath);
    settings.githubTokenPath = ResolvePathFromSettings(settingsPath, settings.githubTokenPath);

    if (!glfwInit()) {
        std::fprintf(stderr, "Не вдалося ініціалізувати GLFW\n");
        return 1;
    }

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(settings.windowWidth, settings.windowHeight, "Перевірка робіт", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Не вдалося створити вікно\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    app::AppControllerDependencies dependencies;
    dependencies.analyzeCommand = std::make_unique<core::AnalyzeSubmissionsCommand>(
        core::StrategyFactory::BuildDefault(settings.ollamaBaseUrl, settings.ollamaModel, settings.plagiarismServiceUrl));
    dependencies.aiCheckCommand = std::make_unique<core::AnalyzeAiOnlyCommand>(
        std::make_unique<core::OllamaAIStrategy>(
            settings.ollamaBaseUrl,
            settings.ollamaModel,
            std::make_unique<core::StylometryAIStrategy>()),
        std::make_unique<core::PromptLeakHeuristicStrategy>());
    dependencies.plagiarismCheckCommand = std::make_unique<core::AnalyzePlagiarismOnlyCommand>(
        std::make_unique<core::PlagiarismServiceStrategy>(
            settings.plagiarismServiceUrl,
            std::make_unique<core::NgramSimilarityStrategy>()));
    dependencies.classroomGateway = std::make_unique<integration::ClassroomGatewayStub>(
        std::make_unique<integration::GoogleClassroomStudentExporter>(
            std::make_unique<integration::FileTokenProvider>(settings.classroomTokenPath)));

    app::AppController controller(std::move(dependencies));
    controller.SetUseClassroomApiImport(settings.classroomUseApiImport);
    controller.State().assignment.classroomCourseId = settings.classroomCourseId;
    controller.State().assignment.classroomCourseWorkId = settings.classroomCourseWorkId;
    controller.State().assignment.classroomStudentGroup = settings.classroomStudentGroup;
    ui::MainWindow mainWindow(controller, window, settings, settingsPath);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        mainWindow.Render();

        ImGui::Render();

        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        glClearColor(background.x, background.y, background.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    mainWindow.SaveSettings();

    glfwDestroyWindow(window);
    glfwTerminate();
}