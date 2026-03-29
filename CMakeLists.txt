cmake_minimum_required(VERSION 3.14)
project(opencodecpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# FetchContent for FTXUI and nlohmann/json
include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# System dependencies
find_package(CURL REQUIRED)
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  # Fallback for systems where FindSQLite3 is not available
  find_library(SQLite3_LIBRARIES NAMES sqlite3)
  find_path(SQLite3_INCLUDE_DIRS NAMES sqlite3.h)
  if(SQLite3_LIBRARIES AND SQLite3_INCLUDE_DIRS)
    set(SQLite3_FOUND TRUE)
  else()
    message(FATAL_ERROR "SQLite3 not found")
  endif()
endif()

# Version definition
add_compile_definitions(OPENCODECPP_VERSION="${PROJECT_VERSION}")

# Collect source files
set(LIB_SOURCES
  src/config/config.cpp
  src/utils/http.cpp
  src/llm/streaming.cpp
  src/llm/anthropic.cpp
  src/llm/openai.cpp
  src/tools/bash.cpp
  src/tools/file_read.cpp
  src/tools/file_write.cpp
  src/tools/file_edit.cpp
  src/tools/glob.cpp
  src/tools/grep.cpp
  src/tools/ls.cpp
  src/session/database.cpp
  src/session/session.cpp
  src/llm/agent_loop.cpp
  src/tui/render.cpp
  src/tui/input.cpp
  src/tui/tui.cpp
  src/tui/markdown.cpp
  src/skills/skill.cpp
  src/skills/skill_tool.cpp
  src/tools/web_search.cpp
  src/tools/web_fetch.cpp
  src/tools/subagent.cpp
  src/auth/codex_auth.cpp
)

# Static library for shared code
add_library(opencodecpp_lib STATIC ${LIB_SOURCES})
target_include_directories(opencodecpp_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(opencodecpp_lib PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  nlohmann_json::nlohmann_json
  CURL::libcurl
  ${SQLite3_LIBRARIES}
)

# Link Apple Security/CommonCrypto framework on macOS (for SHA256 in codex_auth)
if(APPLE)
  find_library(SECURITY_FRAMEWORK Security)
  if(SECURITY_FRAMEWORK)
    target_link_libraries(opencodecpp_lib PUBLIC ${SECURITY_FRAMEWORK})
  endif()
endif()

# Main executable
add_executable(opencode src/main.cpp)
target_link_libraries(opencode PRIVATE opencodecpp_lib)

# Tests
enable_testing()

add_executable(test_config tests/test_config.cpp)
target_link_libraries(test_config PRIVATE opencodecpp_lib)
add_test(NAME test_config COMMAND test_config)

add_executable(test_tools tests/test_tools.cpp)
target_link_libraries(test_tools PRIVATE opencodecpp_lib)
add_test(NAME test_tools COMMAND test_tools)

add_executable(test_llm tests/test_llm.cpp)
target_link_libraries(test_llm PRIVATE opencodecpp_lib)
add_test(NAME test_llm COMMAND test_llm)

add_executable(test_session tests/test_session.cpp)
target_link_libraries(test_session PRIVATE opencodecpp_lib)
add_test(NAME test_session COMMAND test_session)

add_executable(test_tui_scroll tests/test_tui_scroll.cpp)
target_link_libraries(test_tui_scroll PRIVATE opencodecpp_lib)
add_test(NAME test_tui_scroll COMMAND test_tui_scroll)

add_executable(test_openai_streaming tests/test_openai_streaming.cpp)
target_link_libraries(test_openai_streaming PRIVATE opencodecpp_lib)
add_test(NAME test_openai_streaming COMMAND test_openai_streaming)

add_executable(test_markdown tests/test_markdown.cpp)
target_link_libraries(test_markdown PRIVATE opencodecpp_lib)
add_test(NAME test_markdown COMMAND test_markdown)

add_executable(test_skills tests/test_skills.cpp)
target_link_libraries(test_skills PRIVATE opencodecpp_lib)
add_test(NAME test_skills COMMAND test_skills)

add_executable(test_compact_status tests/test_compact_status.cpp)
target_link_libraries(test_compact_status PRIVATE opencodecpp_lib)
add_test(NAME test_compact_status COMMAND test_compact_status)

add_executable(test_sprint_f1_config tests/test_sprint_f1_config.cpp)
target_link_libraries(test_sprint_f1_config PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f1_config COMMAND test_sprint_f1_config)

add_executable(test_sprint_f2_session tests/test_sprint_f2_session.cpp)
target_link_libraries(test_sprint_f2_session PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f2_session COMMAND test_sprint_f2_session)

add_executable(test_sprint_f3_compaction tests/test_sprint_f3_compaction.cpp)
target_link_libraries(test_sprint_f3_compaction PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f3_compaction COMMAND test_sprint_f3_compaction)

add_executable(test_sprint_f4_suggestions tests/test_sprint_f4_suggestions.cpp)
target_link_libraries(test_sprint_f4_suggestions PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f4_suggestions COMMAND test_sprint_f4_suggestions)

add_executable(test_sprint_f5_parallel tests/test_sprint_f5_parallel.cpp)
target_link_libraries(test_sprint_f5_parallel PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f5_parallel COMMAND test_sprint_f5_parallel)

add_executable(test_scroll_container tests/test_scroll_container.cpp)
target_link_libraries(test_scroll_container PRIVATE opencodecpp_lib)
add_test(NAME test_scroll_container COMMAND test_scroll_container)

add_executable(test_plan_mode tests/test_plan_mode.cpp)
target_link_libraries(test_plan_mode PRIVATE opencodecpp_lib)
add_test(NAME test_plan_mode COMMAND test_plan_mode)

add_executable(test_sprint_f6_features tests/test_sprint_f6_features.cpp)
target_link_libraries(test_sprint_f6_features PRIVATE opencodecpp_lib)
add_test(NAME test_sprint_f6_features COMMAND test_sprint_f6_features)

add_executable(test_codex_auth_flow tests/test_codex_auth_flow.cpp)
target_link_libraries(test_codex_auth_flow PRIVATE opencodecpp_lib)
add_test(NAME test_codex_auth_flow COMMAND test_codex_auth_flow)

add_executable(test_ac16_codex_auth_cli tests/test_ac16_codex_auth_cli.cpp)
target_link_libraries(test_ac16_codex_auth_cli PRIVATE opencodecpp_lib)
add_test(NAME test_ac16_codex_auth_cli COMMAND test_ac16_codex_auth_cli)

add_executable(test_codex_responses_api tests/test_codex_responses_api.cpp)
target_link_libraries(test_codex_responses_api PRIVATE opencodecpp_lib)
add_test(NAME test_codex_responses_api COMMAND test_codex_responses_api)

add_executable(test_session_resume tests/test_session_resume.cpp)
target_link_libraries(test_session_resume PRIVATE opencodecpp_lib)
add_test(NAME test_session_resume COMMAND test_session_resume)
