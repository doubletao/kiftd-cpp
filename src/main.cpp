#include <crow.h>
#include <iostream>
#include <filesystem>
#include <cstring>

#include "config.h"
#include "database.h"
#include "auth.h"
#include "filestore.h"
#include "transcode_manager.h"
#include "controllers/auth_controller.h"
#include "controllers/folder_controller.h"
#include "controllers/file_controller.h"
#include "controllers/share_controller.h"
#include "controllers/transcode_controller.h"

namespace fs = std::filesystem;
using namespace kiftd;

static void print_usage() {
    std::cout << "kiftd-cpp - lightweight file server\n"
              << "Usage: kiftd [options]\n"
              << "  -p <port>       Server port (default: 8080)\n"
              << "  -d <dir>        Data directory (default: data)\n"
              << "  -w <dir>        Web dist directory (default: web/dist)\n"
              << "  -h              Show this help\n";
}

int main(int argc, char* argv[]) {
    auto& cfg = Config::instance();

    // Parse command line
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            cfg.port = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            cfg.data_dir = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            cfg.web_dir = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    if (cfg.web_dir.empty()) {
        cfg.web_dir = "web/dist";
    }

    cfg.load(cfg.data_dir + "/config.json");

    // Initialize database
    Database db;
    if (!db.open(cfg.db_path)) {
        std::cerr << "Failed to open database: " << cfg.db_path << std::endl;
        return 1;
    }
    if (!db.init_schema()) {
        std::cerr << "Failed to initialize database schema" << std::endl;
        return 1;
    }

    // Ensure root folder exists
    if (db.get_folder("").id.empty()) {
        Folder root;
        root.id = "";
        root.name = "ROOT";
        root.parent_id = "";
        root.creator = "system";
        db.create_folder(root);
    }

    // Initialize accounts
    Auth auth(db);
    for (auto& acc : cfg.accounts) {
        if (!auth.init_admin(acc.username, acc.password)) {
            std::cerr << "Failed to create account: " << acc.username << std::endl;
            return 1;
        }
        std::cout << "Account ready: " << acc.username << std::endl;
    }

    // Initialize file store
    FileStore file_store(cfg.files_dir);

    // Initialize transcode manager
    TranscodeManager transcode_mgr(cfg, cfg.files_dir);
    if (!cfg.ffmpeg_path.empty()) {
        transcode_mgr.start();
        std::cout << "Transcode enabled: " << cfg.ffmpeg_path
                  << " (concurrency: " << cfg.transcode_concurrency << ")" << std::endl;
    }

    // Setup Crow app
    crow::SimpleApp app;

    // Register API routes
    register_auth_routes(app, db, auth, cfg);
    register_folder_routes(app, db);
    register_file_routes(app, db, file_store);
    register_share_routes(app, db, file_store);
    register_transcode_routes(app, db, file_store, transcode_mgr, cfg);

    // Static file serving + SPA fallback
    CROW_ROUTE(app, "/<path>")
        .methods("GET"_method)
    ([&cfg](const crow::request& req, const std::string& path) {
        // Don't serve static for API routes or share links
        if (path.find("api/") == 0 || path.find("s/") == 0) {
            return crow::response(404);
        }

        std::string file_path = cfg.web_dir + "/" + path;
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            crow::response res;
            res.set_static_file_info(file_path);
            return res;
        }

        // SPA fallback: serve index.html
        std::string index = cfg.web_dir + "/index.html";
        if (fs::exists(index)) {
            crow::response res;
            res.set_static_file_info(index);
            return res;
        }

        return crow::response(404, "Not Found");
    });

    // Root route
    CROW_ROUTE(app, "/")
        .methods("GET"_method)
    ([&cfg](const crow::request& req) {
        std::string index = cfg.web_dir + "/index.html";
        if (fs::exists(index)) {
            crow::response res;
            res.set_static_file_info(index);
            return res;
        }
        return crow::response(200, "kiftd-cpp is running. Place web files in " + cfg.web_dir);
    });

    std::cout << "Starting kiftd-cpp on port " << cfg.port << std::endl;
    std::cout << "Data directory: " << cfg.data_dir << std::endl;
    std::cout << "Web directory: " << cfg.web_dir << std::endl;

    app.port(cfg.port).multithreaded().run();

    return 0;
}
