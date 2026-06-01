#include "auth.h"
#include "utils/sha256.h"

namespace kiftd {

Auth::Auth(Database& db) : db_(db) {}

bool Auth::init_admin(const std::string& username, const std::string& password) {
    if (db_.user_exists(username)) {
        return true; // Already exists
    }
    std::string salt = generate_salt();
    std::string hash = hash_password(password, salt);
    return db_.create_user(username, hash, salt);
}

bool Auth::login(const std::string& username, const std::string& password) {
    User u = db_.get_user(username);
    if (u.id.empty()) return false;
    return hash_password(password, u.salt) == u.password_hash;
}

bool Auth::user_exists(const std::string& username) {
    return db_.user_exists(username);
}

std::string Auth::hash_password(const std::string& password, const std::string& salt) {
    return sha256(password + salt);
}

} // namespace kiftd
