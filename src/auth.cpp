#include "auth.h"
#include "utils/sha256.h"

namespace kiftd {

Auth::Auth(Database& db) : db_(db) {}

bool Auth::init_admin(const std::string& username, const std::string& password) {
    // password is raw plaintext from config — simulate client-side hash first
    std::string client_hash = sha256(password);
    if (db_.user_exists(username)) {
        // Migrate: delete old account and recreate with double-hash
        db_.delete_user(username);
    }
    std::string salt = generate_salt();
    std::string hash = hash_password(client_hash, salt);
    return db_.create_user(username, hash, salt, "admin");
}

bool Auth::init_from_config(const std::vector<Account>& accounts) {
    // Only clear admin users, preserve normal users
    if (!db_.clear_admin_users()) {
        return false;
    }
    // Create admin accounts from config
    for (const auto& acc : accounts) {
        if (!init_admin(acc.username, acc.password)) {
            return false;
        }
    }
    return true;
}

bool Auth::login(const std::string& username, const std::string& password) {
    // password here is already client-hashed (SHA256 of raw password)
    User u = db_.get_user(username);
    if (u.id.empty()) return false;
    return hash_password(password, u.salt) == u.password_hash;
}

bool Auth::user_exists(const std::string& username) {
    return db_.user_exists(username);
}

bool Auth::is_admin(const std::string& username) {
    return db_.get_user_role(username) == "admin";
}

// password: client-side SHA256(raw_password)
// result: SHA256(client_hash + salt)
std::string Auth::hash_password(const std::string& password, const std::string& salt) {
    return sha256(password + salt);
}

} // namespace kiftd
