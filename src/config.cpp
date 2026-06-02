#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kiftd {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

void Config::apply_defaults() {
    db_path = data_dir + "/kiftd.db";
    files_dir = data_dir + "/files";

    // Ensure directories exist
    fs::create_directories(data_dir);
    fs::create_directories(files_dir);
}

void Config::load(const std::string& config_path) {
    apply_defaults();

    if (!fs::exists(config_path)) {
        return; // Use defaults
    }

    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (j.contains("port")) port = j["port"].get<int>();
        if (j.contains("data_dir")) {
            data_dir = j["data_dir"].get<std::string>();
            apply_defaults();
        }
        if (j.contains("accounts") && j["accounts"].is_array()) {
            accounts.clear();
            for (auto& a : j["accounts"]) {
                if (a.contains("username") && a.contains("password")) {
                    accounts.push_back({a["username"].get<std::string>(), a["password"].get<std::string>()});
                }
            }
        }
        if (j.contains("max_attempts")) max_attempts = j["max_attempts"].get<int>();
        if (j.contains("lockout_seconds")) lockout_seconds = j["lockout_seconds"].get<int>();
    } catch (...) {
        // Config parse error, use defaults
    }
}

} // namespace kiftd
