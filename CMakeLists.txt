cmake_minimum_required(VERSION 3.20)
project(kiftd-cpp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/bigobj)
endif()

# Asio (standalone, header-only)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE third_party)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE _WIN32_WINNT=0x0601)

# nlohmann/json (header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# SQLite3 (local amalgamation)
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1)

# Crow (header-only, local)
add_library(crow INTERFACE)
target_include_directories(crow INTERFACE third_party third_party/crow)
target_link_libraries(crow INTERFACE asio nlohmann_json)

# Main executable
add_executable(kiftd
    src/main.cpp
    src/config.cpp
    src/database.cpp
    src/auth.cpp
    src/filestore.cpp
    src/transcode_manager.cpp
    src/controllers/auth_controller.cpp
    src/controllers/folder_controller.cpp
    src/controllers/file_controller.cpp
    src/controllers/share_controller.cpp
    src/controllers/transcode_controller.cpp
    src/utils/sha256.cpp
    src/utils/uuid.cpp
)

target_include_directories(kiftd PRIVATE src)

target_link_libraries(kiftd PRIVATE
    crow
    nlohmann_json
    sqlite3
)

if(WIN32)
    target_link_libraries(kiftd PRIVATE ws2_32)
endif()
