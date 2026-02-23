cmake_minimum_required(VERSION 3.20)
project(AIChecker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

find_package(OpenGL REQUIRED)

target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

add_executable(AIChecker
  src/main.cpp
  src/app/AppController.cpp
  src/config/AppSettings.cpp
  src/ui/MainWindow.cpp
)

target_include_directories(AIChecker PRIVATE src)
target_link_libraries(AIChecker PRIVATE imgui_lib nlohmann_json::nlohmann_json)

if(WIN32)
  target_link_libraries(AIChecker PRIVATE winhttp)
endif()

if(MSVC)
  target_compile_options(AIChecker PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(AIChecker PRIVATE -Wall -Wextra -Wpedantic)
endif()
