#pragma once
#include <string>
#include <vector>
#include <functional>
#include <sqlite3.h>

namespace kiftd {

struct User {
    std::string id;
    std::string password_hash;
    std::string salt;
    std::string created_at;
};

struct Folder {
    std::string id;
    std::string name;
    std::string parent_id;  // empty = root
    std::string creator;
    std::string created_at;
};

struct FileRecord {
    std::string id;
    std::string name;
    int64_t size;
    std::string disk_name;
    std::string folder_id;
    std::string creator;
    std::string created_at;
};

struct ShareRecord {
    std::string id;
    std::string file_id;
    std::string file_name;   // joined from files
    std::string creator;
    std::string expire_at;
    std::string created_at;
};

struct PlayHistoryRecord {
    std::string folder_id;
    std::string file_id;
    double position = 0;    // seconds
    double duration = 0;    // seconds
    std::string updated_at;
    // Transcode params (per-folder default for auto-transcode)
    std::string preset;
    int audio_index = 0;
    int subtitle_index = -1;
    std::string external_subtitle_path;
};

class Database {
public:
    Database();
    ~Database();

    bool open(const std::string& path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Schema
    bool init_schema();

    // Users
    bool create_user(const std::string& id, const std::string& password_hash, const std::string& salt);
    User get_user(const std::string& id);
    bool user_exists(const std::string& id);
    bool delete_user(const std::string& id);

    // Folders
    bool create_folder(const Folder& f);
    Folder get_folder(const std::string& id);
    std::vector<Folder> get_subfolders(const std::string& parent_id);
    bool rename_folder(const std::string& id, const std::string& new_name);
    bool delete_folder(const std::string& id);
    bool has_children(const std::string& folder_id);
    std::string get_folder_path(const std::string& folder_id);

    // Files
    bool create_file(const FileRecord& f);
    FileRecord get_file(const std::string& id);
    std::vector<FileRecord> get_files_in_folder(const std::string& folder_id);
    bool rename_file(const std::string& id, const std::string& new_name);
    bool delete_file(const std::string& id);

    // Shares
    bool create_share(const ShareRecord& s);
    ShareRecord get_share(const std::string& id);
    std::vector<ShareRecord> get_shares_by_user(const std::string& user);
    bool delete_share(const std::string& id);

    // Play History
    bool init_play_history_schema();
    bool upsert_play_history(const std::string& folder_id, const std::string& file_id, double position, double duration,
                             const std::string& preset = "", int audio_index = 0, int subtitle_index = -1, const std::string& external_subtitle_path = "");
    std::vector<PlayHistoryRecord> get_all_play_history();
    bool delete_play_history(const std::string& folder_id);

private:
    sqlite3* db_ = nullptr;

    bool exec(const std::string& sql);
    sqlite3_stmt* prepare(const std::string& sql);
};

} // namespace kiftd
