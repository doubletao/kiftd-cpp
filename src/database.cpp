#include "database.h"
#include <iostream>

namespace kiftd {

Database::Database() = default;

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "SQLite open failed: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return false;
    }
    // Enable WAL mode for better concurrency
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    return true;
}

void Database::close() {
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
    return exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id TEXT PRIMARY KEY,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
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
    )");
}

// --- Users ---

bool Database::create_user(const std::string& id, const std::string& password_hash, const std::string& salt) {
    auto stmt = prepare("INSERT INTO users (id, password_hash, salt) VALUES (?, ?, ?)");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

User Database::get_user(const std::string& id) {
    User u;
    auto stmt = prepare("SELECT id, password_hash, salt, created_at FROM users WHERE id = ?");
    if (!stmt) return u;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        u.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    }
    sqlite3_finalize(stmt);
    return u;
}

bool Database::user_exists(const std::string& id) {
    auto stmt = prepare("SELECT 1 FROM users WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

// --- Folders ---

bool Database::create_folder(const Folder& f) {
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
    auto stmt = prepare("UPDATE folders SET name = ? WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::delete_folder(const std::string& id) {
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

bool Database::has_children(const std::string& folder_id) {
    auto stmt = prepare("SELECT 1 FROM folders WHERE parent_id = ? UNION ALL SELECT 1 FROM files WHERE folder_id = ? LIMIT 1");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    bool has = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return has;
}

// --- Files ---

bool Database::create_file(const FileRecord& f) {
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
    FileRecord f;
    auto stmt = prepare("SELECT id, name, size, disk_name, folder_id, creator, created_at FROM files WHERE id = ?");
    if (!stmt) return f;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        f.size = sqlite3_column_int64(stmt, 2);
        f.disk_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        f.folder_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        f.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        f.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    }
    sqlite3_finalize(stmt);
    return f;
}

std::vector<FileRecord> Database::get_files_in_folder(const std::string& folder_id) {
    std::vector<FileRecord> result;
    auto stmt = prepare("SELECT id, name, size, disk_name, folder_id, creator, created_at FROM files WHERE folder_id = ? ORDER BY name");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, folder_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord f;
        f.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        f.size = sqlite3_column_int64(stmt, 2);
        f.disk_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        f.folder_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        f.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        f.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        result.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::rename_file(const std::string& id, const std::string& new_name) {
    auto stmt = prepare("UPDATE files SET name = ? WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::delete_file(const std::string& id) {
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
    ShareRecord s;
    auto stmt = prepare(
        "SELECT s.id, s.file_id, f.name, s.creator, s.expire_at, s.created_at "
        "FROM shares s JOIN files f ON s.file_id = f.id WHERE s.id = ?");
    if (!stmt) return s;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        s.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        s.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        auto expire = sqlite3_column_text(stmt, 4);
        s.expire_at = expire ? reinterpret_cast<const char*>(expire) : "";
        s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    }
    sqlite3_finalize(stmt);
    return s;
}

std::vector<ShareRecord> Database::get_shares_by_user(const std::string& user) {
    std::vector<ShareRecord> result;
    auto stmt = prepare(
        "SELECT s.id, s.file_id, f.name, s.creator, s.expire_at, s.created_at "
        "FROM shares s JOIN files f ON s.file_id = f.id WHERE s.creator = ? ORDER BY s.created_at DESC");
    if (!stmt) return result;
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ShareRecord s;
        s.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        s.creator = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        auto expire = sqlite3_column_text(stmt, 4);
        s.expire_at = expire ? reinterpret_cast<const char*>(expire) : "";
        s.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        result.push_back(std::move(s));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::delete_share(const std::string& id) {
    auto stmt = prepare("DELETE FROM shares WHERE id = ?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace kiftd
