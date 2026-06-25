#pragma once
#include <string>
#include "database.h"
#include "config.h"

namespace kiftd {

class Auth {
public:
    Auth(Database& db);

    bool init_admin(const std::string& username, const std::string& password);
    bool init_from_config(const std::vector<Account>& accounts);
    bool login(const std::string& username, const std::string& password);
    bool user_exists(const std::string& username);
    bool is_admin(const std::string& username);

    // Hash password with salt
    static std::string hash_password(const std::string& password, const std::string& salt);

private:
    Database& db_;
};

} // namespace kiftd
