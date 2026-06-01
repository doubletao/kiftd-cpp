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
    return db_.create_user(username, hash, salt);
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

// password: client-side SHA256(raw_password)
// result: SHA256(client_hash + salt)
std::string Auth::hash_password(const std::string& password, const std::string& salt) {
    return sha256(password + salt);
}

} // namespace kiftd
