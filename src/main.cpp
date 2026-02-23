#include "app/AppController.h"
#include "config/AppSettings.h"
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

std::string ResolveSettingsPath() {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path currentDir = fs::current_path(ec);
    if (!ec) {
        fs::path searchDir = currentDir;
        for (int depth = 0; depth < 8; ++depth) {
            const fs::path settingsCandidate = searchDir / "settings" / "app_settings.cfg";
            const fs::path cmakeCandidate = searchDir / "CMakeLists.txt";

            std::error_code fileEc;
            const bool hasSettings = fs::exists(settingsCandidate, fileEc) && !fileEc;
            fileEc.clear();
            const bool hasCmake = fs::exists(cmakeCandidate, fileEc) && !fileEc;
            if (hasSettings && hasCmake) {
                return settingsCandidate.lexically_normal().string();
            }

            if (!searchDir.has_parent_path()) {
                break;
            }
            const fs::path parent = searchDir.parent_path();
            if (parent == searchDir) {
                break;
            }
            searchDir = parent;
        }
    }

    return (fs::path("settings") / "app_settings.cfg").string();
}

} // namespace

int main() {
    const std::string settingsPath = ResolveSettingsPath();
    config::AppSettings settings;
    config::LoadAppSettings(settingsPath, settings);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(settings.windowWidth, settings.windowHeight, "AIChecker", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create window\n");
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

    app::AppController controller;
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

    return 0;
}
