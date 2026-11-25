cmake_minimum_required(VERSION 3.20)
project(kiftd-cpp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Single-config generators (Makefiles/Ninja) need an explicit build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/bigobj)
endif()

find_package(Threads REQUIRED)

# Asio (standalone, header-only)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE third_party)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601)
endif()

# nlohmann/json (header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# SQLite3 (local amalgamation)
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1)
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

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
    src/live_segmenter.cpp
    src/controllers/auth_controller.cpp
    src/controllers/admin_controller.cpp
    src/controllers/user_controller.cpp
    src/controllers/folder_controller.cpp
    src/controllers/file_controller.cpp
    src/controllers/share_controller.cpp
    src/controllers/transcode_controller.cpp
    src/controllers/play_history_controller.cpp
    src/utils/sha256.cpp
    src/utils/uuid.cpp
    src/utils/common.cpp
)

target_include_directories(kiftd PRIVATE src)

target_link_libraries(kiftd PRIVATE
    crow
    nlohmann_json
    sqlite3
    Threads::Threads
)

if(WIN32)
    target_link_libraries(kiftd PRIVATE ws2_32)
endif()

# HTTPS support (LibreSSL, built from local source as static libs)
option(KIFTD_ENABLE_SSL "Enable HTTPS support via bundled LibreSSL" ON)
if(KIFTD_ENABLE_SSL)
    set(LIBRESSL_APPS OFF CACHE BOOL "" FORCE)
    set(LIBRESSL_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBRESSL_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/libressl EXCLUDE_FROM_ALL)
    # MSVC (verified on VS2019 14.29) miscompiles bn_mont.c with optimization on,
    # breaking RSA/EC math and every TLS handshake; LibreSSL's own tests catch it.
    if(MSVC)
        set_source_files_properties(third_party/libressl/crypto/bn/bn_mont.c
            DIRECTORY third_party/libressl/crypto
            PROPERTIES COMPILE_OPTIONS /Od)
    endif()
    target_compile_definitions(kiftd PRIVATE CROW_ENABLE_SSL)
    target_link_libraries(kiftd PRIVATE ssl crypto)
endif()
