#pragma once
#include <string>
#include <vector>

namespace kiftd {

struct Account {
    std::string username;
    std::string password;
};

struct Config {
    int port = 8081;
    std::string data_dir = "data";
    std::string db_path;        // data_dir + "/kiftd.db"
    std::string files_dir;      // data_dir + "/files"
    std::string web_dir;        // "web/dist"
    std::vector<Account> accounts = {{"admin", "admin"}};
    int max_attempts = 5;
    int lockout_seconds = 60;

    static Config& instance();
    void load(const std::string& config_path);
    void apply_defaults();
};

} // namespace kiftd
