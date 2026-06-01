#pragma once
#include <string>

namespace kiftd {

struct Config {
    int port = 8080;
    std::string data_dir = "data";
    std::string db_path;        // data_dir + "/kiftd.db"
    std::string files_dir;      // data_dir + "/files"
    std::string web_dir;        // "web/dist"
    std::string admin_user = "admin";
    std::string admin_pass = "admin";

    static Config& instance();
    void load(const std::string& config_path);
    void apply_defaults();
};

} // namespace kiftd
