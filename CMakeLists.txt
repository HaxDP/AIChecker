cmake_minimum_required(VERSION 3.20)
project(AIChecker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

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

set(AICHECKER_SERVER_SOURCES
  src/server/Api.cpp
  src/server/Controllers.cpp
  src/server/Utils.cpp
  src/server/Stores.cpp
  src/server/Services.cpp
)

add_executable(AIChecker
  src/main.cpp
  ${AICHECKER_SERVER_SOURCES}
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

if(BUILD_TESTING)
  add_executable(AICheckerTests
    tests/unitTests.cpp
    ${AICHECKER_SERVER_SOURCES}
  )

  target_include_directories(AICheckerTests PRIVATE src)
  target_include_directories(AICheckerTests PRIVATE ${cpp_httplib_SOURCE_DIR})
  target_link_libraries(AICheckerTests PRIVATE nlohmann_json::nlohmann_json)

  if(WIN32)
    target_link_libraries(AICheckerTests PRIVATE winhttp)
    target_link_libraries(AICheckerTests PRIVATE ws2_32)
  endif()

  if(MSVC)
    target_compile_options(AICheckerTests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(AICheckerTests PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  add_test(NAME Unit.RuntimeDirectories COMMAND AICheckerTests --run RuntimeDirectories)
  add_test(NAME Unit.ReadWriteFileHelpers COMMAND AICheckerTests --run ReadWriteFileHelpers)
  add_test(NAME Unit.SessionStoreRoundtrip COMMAND AICheckerTests --run SessionStoreRoundtrip)
  add_test(NAME Unit.CacheStoreWritesJson COMMAND AICheckerTests --run CacheStoreWritesJson)
  add_test(NAME Unit.DataStoreReplaceAndQueries COMMAND AICheckerTests --run DataStoreReplaceAndQueries)
  add_test(NAME Unit.DataStoreKeepsReviewFieldsOnRefresh COMMAND AICheckerTests --run DataStoreKeepsReviewFieldsOnRefresh)
  add_test(NAME Unit.PlagiarismAnalyze COMMAND AICheckerTests --run PlagiarismAnalyze)
  add_test(NAME Unit.FinalizationApproveAndSend COMMAND AICheckerTests --run FinalizationApproveAndSend)
  add_test(NAME Unit.FinalizationSendRejectedWhenNotApproved COMMAND AICheckerTests --run FinalizationSendRejectedWhenNotApproved)
  add_test(NAME Unit.AiAnalyzeFallback COMMAND AICheckerTests --run AiAnalyzeFallback)
endif()