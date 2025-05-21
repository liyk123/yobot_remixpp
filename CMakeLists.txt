cmake_minimum_required (VERSION 3.10)

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
)

find_package(spdlog CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${FILE_LIST})

target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog_header_only)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
  message("Set C++20")
endif()

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  target_compile_definitions(${PROJECT_NAME} PUBLIC DEBUG)
endif()