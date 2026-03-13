cmake_minimum_required(VERSION 3.20)
project(AIChecker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.6
)
FetchContent_GetProperties(cpp_httplib)
if(NOT cpp_httplib_POPULATED)
  FetchContent_Populate(cpp_httplib)
endif()

add_executable(AIChecker
  src/main.cpp
  src/server/Api.cpp
  src/server/Controllers.cpp
  src/server/Utils.cpp
  src/server/Stores.cpp
  src/server/Services.cpp
)

target_include_directories(AIChecker PRIVATE src)
target_include_directories(AIChecker PRIVATE ${cpp_httplib_SOURCE_DIR})
target_link_libraries(AIChecker PRIVATE nlohmann_json::nlohmann_json)

if(WIN32)
  target_link_libraries(AIChecker PRIVATE winhttp)
  target_link_libraries(AIChecker PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(AIChecker PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(AIChecker PRIVATE -Wall -Wextra -Wpedantic)
endif()