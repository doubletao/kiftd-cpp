#include "database.h"
#include <iostream>

namespace kiftd {

static const char* safe_column_text(sqlite3_stmt* stmt, int col) {
    auto p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

Database::Database() = default;

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "SQLite open failed: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return false;
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    return true;
}

void Database::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQL error: " << (err ? err : "unknown") << std::endl;
        sqlite3_free(err);
        return false;
    }
    return true;
}

sqlite3_stmt* Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    return stmt;
}

bool Database::init_schema() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id TEXT PRIMARY KEY,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            role TEXT NOT NULL DEFAULT 'user',
            created_at TEXT DEFAULT (datetime('now','localtime'))
        );
        CREATE TABLE IF NOT EXISTS folders (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            parent_id TEXT,
            creator TEXT NOT NULL,
            created_at TEXT DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (parent_id) REFERENCES folders(id)
        );
        CREATE TABLE IF NOT EXISTS files (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            size INTEGER NOT NULL,
            disk_name TEXT NOT NULL,
            folder_id TEXT NOT NULL,
            creator TEXT NOT NULL,
            created_at TEXT DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (folder_id) REFERENCES folders(id)
        );
        CREATE TABLE IF NOT EXISTS shares (
            id TEXT PRIMARY KEY,
            file_id TEXT NOT NULL,
            creator TEXT NOT NULL,
            expire_at TEXT,
            created_at TEXT DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (file_id) REFERENCES files(id)
        );
        CREATE INDEX IF NOT EXISTS idx_folders_parent ON folders(parent_id);
        CREATE INDEX IF NOT EXISTS idx_files_folder ON files(folder_id);
        CREATE INDEX IF NOT EXISTS idx_shares_file ON shares(file_id);
    )") && init_play_history_schema();
}

// --- Users ---

bool Database::create_user(const std::string& id, const std::string& password_hash, const std::string& salt, const std::string& role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("INSERT OR REPLACE INTO users (id, password_hash, salt, role) VALUES (?, ?, ?, ?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

User Database::get_user(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    User u;
    auto stmt = prepare("SELECT id, password_hash, salt, role, created_at FROM users WHERE id = ?");
    if (!stmt) return u;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        u.id = safe_column_text(stmt, 0);
        u.password_hash = safe_column_text(stmt, 1);
        u.salt = safe_column_text(stmt, 2);
        u.role = safe_column_text(stmt, 3);
        u.created_at = safe_column_text(stmt, 4);
    }
    sqlite3_finalize(stmt);
    return u;
}

bool Database::user_exists(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("SELECT 1 FROM users WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

bool Database::delete_user(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("DELETE FROM users WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::clear_admin_users() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return exec("DELETE FROM users WHERE role = 'admin'");
}

std::string Database::get_user_role(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("SELECT role FROM users WHERE id = ?");
    if (!stmt) return "";
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::string role;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        role = safe_column_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return role;
}

std::vector<User> Database::get_all_users() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<User> users;
    auto stmt = prepare("SELECT id, password_hash, salt, role, created_at FROM users ORDER BY role DESC, id ASC");
    if (!stmt) return users;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id = safe_column_text(stmt, 0);
        u.password_hash = safe_column_text(stmt, 1);
        u.salt = safe_column_text(stmt, 2);
        u.role = safe_column_text(stmt, 3);
        u.created_at = safe_column_text(stmt, 4);
        users.push_back(std::move(u));
    }
    sqlite3_finalize(stmt);
    return users;
}

bool Database::update_password(const std::string& id, const std::string& password_hash, const std::string& salt) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("UPDATE users SET password_hash = ?, salt = ? WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// --- Folders ---

bool Database::create_folder(const Folder& f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("INSERT INTO folders (id, name, parent_id, creator) VALUES (?, ?, ?, ?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, f.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, f.name.c_str(), -1, SQLITE_TRANSIENT);
    if (f.parent_id.empty()) {
        sqlite3_bind_null(stmt, 3);
    } else {
        sqlite3_bind_text(stmt, 3, f.parent_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, 4, f.creator.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

Folder Database::get_folder(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Folder f;
    auto stmt = prepare("SELECT id, name, parent_id, creator, created_at FROM folders WHERE id = ?");
    if (!stmt) return f;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto parent = sqlite3_column_text(stmt, 2);
        f.parent_id = parent ? reinterpret_cast<const char*>(parent) : "";
        f.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        f.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    }
    sqlite3_finalize(stmt);
    return f;
}

std::vector<Folder> Database::get_subfolders(const std::string& parent_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Folder> result;
    sqlite3_stmt* stmt = nullptr;
    if (parent_id.empty()) {
        stmt = prepare("SELECT id, name, parent_id, creator, created_at FROM folders WHERE parent_id IS NULL AND id != '' ORDER BY name");
    } else {
        stmt = prepare("SELECT id, name, parent_id, creator, created_at FROM folders WHERE parent_id = ? ORDER BY name");
    }
    if (!stmt) return result;
    if (!parent_id.empty()) {
        sqlite3_bind_text(stmt, 1, parent_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Folder f;
        f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto parent = sqlite3_column_text(stmt, 2);
        f.parent_id = parent ? reinterpret_cast<const char*>(parent) : "";
        f.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        f.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        result.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::rename_folder(const std::string& id, const std::string& new_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("UPDATE folders SET name = ? WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::delete_folder(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Delete all files in this folder first (just DB records, disk cleanup done separately)
    auto stmt = prepare("DELETE FROM files WHERE folder_id = ?");
    if (stmt) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // Delete subfolders recursively
    auto sub = get_subfolders(id);
    for (auto& sf : sub) {
        delete_folder(sf.id);
    }
    // Delete the folder itself
    stmt = prepare("DELETE FROM folders WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<std::string> Database::get_all_disk_names_in_folder(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> result;
    // Collect disk_names from files in this folder
    auto stmt = prepare("SELECT disk_name FROM files WHERE folder_id = ?");
    if (stmt) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (name) result.push_back(name);
        }
        sqlite3_finalize(stmt);
    }
    // Recurse into subfolders
    auto sub = get_subfolders(id);
    for (auto& sf : sub) {
        auto sub_names = get_all_disk_names_in_folder(sf.id);
        result.insert(result.end(), sub_names.begin(), sub_names.end());
    }
    return result;
}

std::vector<std::string> Database::get_all_file_ids_in_folder(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> result;
    auto stmt = prepare("SELECT id FROM files WHERE folder_id = ?");
    if (stmt) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (fid) result.push_back(fid);
        }
        sqlite3_finalize(stmt);
    }
    auto sub = get_subfolders(id);
    for (auto& sf : sub) {
        auto sub_ids = get_all_file_ids_in_folder(sf.id);
        result.insert(result.end(), sub_ids.begin(), sub_ids.end());
    }
    return result;
}

bool Database::has_children(const std::string& folder_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("SELECT 1 FROM folders WHERE parent_id = ? UNION ALL SELECT 1 FROM files WHERE folder_id = ? LIMIT 1");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    bool has = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return has;
}

std::string Database::get_folder_path(const std::string& folder_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Use recursive CTE to get full path in one query
    auto stmt = prepare(R"(
        WITH RECURSIVE ancestors(id, name, parent_id) AS (
            SELECT id, name, parent_id FROM folders WHERE id = ?
            UNION ALL
            SELECT f.id, f.name, f.parent_id FROM folders f JOIN ancestors a ON f.id = a.parent_id
        )
        SELECT name FROM ancestors ORDER BY name
    )");
    if (!stmt) return "";
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    
    std::string path;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (!name) continue;
        if (!path.empty()) path += "/";
        path += name;
    }
    sqlite3_finalize(stmt);
    return path;
}

std::vector<Folder> Database::get_folder_ancestors(const std::string& folder_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<Folder> result;
    auto stmt = prepare(R"(
        WITH RECURSIVE ancestors(id, name, parent_id, creator, created_at) AS (
            SELECT id, name, parent_id, creator, created_at FROM folders WHERE id = ?
            UNION ALL
            SELECT f.id, f.name, f.parent_id, f.creator, f.created_at FROM folders f JOIN ancestors a ON f.id = a.parent_id
        )
        SELECT id, name, parent_id FROM ancestors
    )");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Folder f;
        f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto parent = sqlite3_column_text(stmt, 2);
        f.parent_id = parent ? reinterpret_cast<const char*>(parent) : "";
        result.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

// --- Files ---

bool Database::create_file(const FileRecord& f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("INSERT INTO files (id, name, size, disk_name, folder_id, creator) VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, f.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, f.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, f.size);
    sqlite3_bind_text(stmt, 4, f.disk_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, f.folder_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, f.creator.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

FileRecord Database::get_file(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    FileRecord f;
    auto stmt = prepare("SELECT id, name, size, disk_name, folder_id, creator, created_at FROM files WHERE id = ?");
    if (!stmt) return f;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        f.id = safe_column_text(stmt, 0);
        f.name = safe_column_text(stmt, 1);
        f.size = sqlite3_column_int64(stmt, 2);
        f.disk_name = safe_column_text(stmt, 3);
        f.folder_id = safe_column_text(stmt, 4);
        f.creator = safe_column_text(stmt, 5);
        f.created_at = safe_column_text(stmt, 6);
    }
    sqlite3_finalize(stmt);
    return f;
}

std::vector<FileRecord> Database::get_files_in_folder(const std::string& folder_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<FileRecord> result;
    auto stmt = prepare("SELECT id, name, size, disk_name, folder_id, creator, created_at FROM files WHERE folder_id = ? ORDER BY name");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord f;
        f.id = safe_column_text(stmt, 0);
        f.name = safe_column_text(stmt, 1);
        f.size = sqlite3_column_int64(stmt, 2);
        f.disk_name = safe_column_text(stmt, 3);
        f.folder_id = safe_column_text(stmt, 4);
        f.creator = safe_column_text(stmt, 5);
        f.created_at = safe_column_text(stmt, 6);
        result.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<FileRecord> Database::get_files_in_folder(const std::string& folder_id, int offset, int limit, int64_t& total) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<FileRecord> result;
    
    // Get total count
    auto count_stmt = prepare("SELECT COUNT(*) FROM files WHERE folder_id = ?");
    if (count_stmt) {
        sqlite3_bind_text(count_stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            total = sqlite3_column_int64(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    
    // Get paginated results
    auto stmt = prepare("SELECT id, name, size, disk_name, folder_id, creator, created_at FROM files WHERE folder_id = ? ORDER BY name LIMIT ? OFFSET ?");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord f;
        f.id = safe_column_text(stmt, 0);
        f.name = safe_column_text(stmt, 1);
        f.size = sqlite3_column_int64(stmt, 2);
        f.disk_name = safe_column_text(stmt, 3);
        f.folder_id = safe_column_text(stmt, 4);
        f.creator = safe_column_text(stmt, 5);
        f.created_at = safe_column_text(stmt, 6);
        result.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::rename_file(const std::string& id, const std::string& new_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("UPDATE files SET name = ? WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::delete_file(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Delete associated shares first
    auto stmt = prepare("DELETE FROM shares WHERE file_id = ?");
    if (stmt) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    stmt = prepare("DELETE FROM files WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// --- Shares ---

bool Database::create_share(const ShareRecord& s) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("INSERT INTO shares (id, file_id, creator, expire_at) VALUES (?, ?, ?, ?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, s.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, s.file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, s.creator.c_str(), -1, SQLITE_TRANSIENT);
    if (s.expire_at.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, s.expire_at.c_str(), -1, SQLITE_TRANSIENT);
    }
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

ShareRecord Database::get_share(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ShareRecord s;
    auto stmt = prepare(
        "SELECT s.id, s.file_id, f.name, s.creator, s.expire_at, s.created_at "
        "FROM shares s JOIN files f ON s.file_id = f.id WHERE s.id = ?");
    if (!stmt) return s;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        s.id = safe_column_text(stmt, 0);
        s.file_id = safe_column_text(stmt, 1);
        s.file_name = safe_column_text(stmt, 2);
        s.creator = safe_column_text(stmt, 3);
        auto expire = sqlite3_column_text(stmt, 4);
        s.expire_at = expire ? reinterpret_cast<const char*>(expire) : "";
        s.created_at = safe_column_text(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return s;
}

std::vector<ShareRecord> Database::get_shares_by_user(const std::string& user) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<ShareRecord> result;
    auto stmt = prepare(
        "SELECT s.id, s.file_id, f.name, s.creator, s.expire_at, s.created_at "
        "FROM shares s JOIN files f ON s.file_id = f.id WHERE s.creator = ? ORDER BY s.created_at DESC");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ShareRecord s;
        s.id = safe_column_text(stmt, 0);
        s.file_id = safe_column_text(stmt, 1);
        s.file_name = safe_column_text(stmt, 2);
        s.creator = safe_column_text(stmt, 3);
        auto expire = sqlite3_column_text(stmt, 4);
        s.expire_at = expire ? reinterpret_cast<const char*>(expire) : "";
        s.created_at = safe_column_text(stmt, 5);
        result.push_back(std::move(s));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::delete_share(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("DELETE FROM shares WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// --- Play History ---

bool Database::init_play_history_schema() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return exec(R"(
        CREATE TABLE IF NOT EXISTS play_history (
            folder_id  TEXT NOT NULL,
            file_id    TEXT NOT NULL,
            position   REAL NOT NULL DEFAULT 0,
            duration   REAL NOT NULL DEFAULT 0,
            updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            preset     TEXT NOT NULL DEFAULT '',
            audio_index       INTEGER NOT NULL DEFAULT 0,
            subtitle_index    INTEGER NOT NULL DEFAULT -1,
            external_subtitle_path TEXT NOT NULL DEFAULT '',
            PRIMARY KEY (folder_id)
        );
    )");
}

bool Database::upsert_play_history(const std::string& folder_id, const std::string& file_id, double position, double duration,
                                   const std::string& preset, int audio_index, int subtitle_index, const std::string& external_subtitle_path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare(
        "INSERT INTO play_history (folder_id, file_id, position, duration, updated_at, preset, audio_index, subtitle_index, external_subtitle_path) "
        "VALUES (?, ?, ?, ?, datetime('now','localtime'), ?, ?, ?, ?) "
        "ON CONFLICT(folder_id) DO UPDATE SET file_id = excluded.file_id, "
        "position = excluded.position, duration = excluded.duration, updated_at = excluded.updated_at, "
        "preset = CASE WHEN excluded.preset != '' THEN excluded.preset ELSE play_history.preset END, "
        "audio_index = CASE WHEN excluded.preset != '' THEN excluded.audio_index ELSE play_history.audio_index END, "
        "subtitle_index = CASE WHEN excluded.preset != '' THEN excluded.subtitle_index ELSE play_history.subtitle_index END, "
        "external_subtitle_path = CASE WHEN excluded.preset != '' THEN excluded.external_subtitle_path ELSE play_history.external_subtitle_path END");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, position);
    sqlite3_bind_double(stmt, 4, duration);
    sqlite3_bind_text(stmt, 5, preset.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, audio_index);
    sqlite3_bind_int(stmt, 7, subtitle_index);
    sqlite3_bind_text(stmt, 8, external_subtitle_path.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<PlayHistoryRecord> Database::get_all_play_history() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<PlayHistoryRecord> result;
    auto stmt = prepare("SELECT folder_id, file_id, position, duration, updated_at, preset, audio_index, subtitle_index, external_subtitle_path FROM play_history ORDER BY updated_at DESC");
    if (!stmt) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayHistoryRecord r;
        r.folder_id = safe_column_text(stmt, 0);
        r.file_id = safe_column_text(stmt, 1);
        r.position = sqlite3_column_double(stmt, 2);
        r.duration = sqlite3_column_double(stmt, 3);
        r.updated_at = safe_column_text(stmt, 4);
        r.preset = safe_column_text(stmt, 5);
        r.audio_index = sqlite3_column_int(stmt, 6);
        r.subtitle_index = sqlite3_column_int(stmt, 7);
        r.external_subtitle_path = safe_column_text(stmt, 8);
        result.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PlayHistoryRecord> Database::get_play_history_with_names(std::vector<std::pair<std::string, std::string>>& names) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<PlayHistoryRecord> result;
    auto stmt = prepare(
        "SELECT ph.folder_id, ph.file_id, ph.position, ph.duration, ph.updated_at, "
        "ph.preset, ph.audio_index, ph.subtitle_index, ph.external_subtitle_path, "
        "COALESCE(f.name, ph.folder_id) as folder_name, "
        "COALESCE(fi.name, ph.file_id) as file_name "
        "FROM play_history ph "
        "LEFT JOIN folders f ON ph.folder_id = f.id "
        "LEFT JOIN files fi ON ph.file_id = fi.id "
        "ORDER BY ph.updated_at DESC");
    if (!stmt) return result;
    names.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayHistoryRecord r;
        r.folder_id = safe_column_text(stmt, 0);
        r.file_id = safe_column_text(stmt, 1);
        r.position = sqlite3_column_double(stmt, 2);
        r.duration = sqlite3_column_double(stmt, 3);
        r.updated_at = safe_column_text(stmt, 4);
        r.preset = safe_column_text(stmt, 5);
        r.audio_index = sqlite3_column_int(stmt, 6);
        r.subtitle_index = sqlite3_column_int(stmt, 7);
        r.external_subtitle_path = safe_column_text(stmt, 8);
        
        auto folder_name = sqlite3_column_text(stmt, 9);
        auto file_name = sqlite3_column_text(stmt, 10);
        names.emplace_back(
            folder_name ? reinterpret_cast<const char*>(folder_name) : r.folder_id,
            file_name ? reinterpret_cast<const char*>(file_name) : r.file_id
        );
        
        result.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::delete_play_history(const std::string& folder_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto stmt = prepare("DELETE FROM play_history WHERE folder_id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::delete_all_play_history_in_folder(const std::string& folder_id) {
    delete_play_history(folder_id);
    auto sub = get_subfolders(folder_id);
    for (auto& sf : sub) {
        delete_all_play_history_in_folder(sf.id);
    }
    return true;
}

} // namespace kiftd
