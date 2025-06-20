cmake_minimum_required (VERSION 3.12)

message("CMAKE_VERSION: ${CMAKE_VERSION}") 
message("CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(PROJECT_NAME "yobot_remixpp")

project (${PROJECT_NAME})

set(FILE_LIST 
	"main.cpp"
	"yobotdata_new.h"
	"default_config.h"
	"yobotdata_new.sql.h"
)

find_package(spdlog CONFIG REQUIRED)
find_package(Sqlpp11 CONFIG REQUIRED COMPONENTS SQLite3)
find_package(TwoBot CONFIG REQUIRED)
find_package(async_simple CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${FILE_LIST})

target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog_header_only TwoBot::TwoBot sqlpp11::sqlite3 async_simple::async_simple)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

