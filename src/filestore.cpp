#include "filestore.h"
#include "utils/uuid.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace kiftd {

FileStore::FileStore(const std::string& files_dir) : files_dir_(files_dir) {
    fs::create_directories(files_dir);
}

std::string FileStore::save(const std::string& source_path) {
    std::string disk_name = generate_uuid() + ".bin";
    std::string dest = files_dir_ + "/" + disk_name;
    try {
        fs::copy_file(source_path, dest, fs::copy_options::overwrite_existing);
        return disk_name;
    } catch (...) {
        return "";
    }
}

std::string FileStore::save_from_buffer(const char* data, size_t size) {
    std::string disk_name = generate_uuid() + ".bin";
    std::string dest = files_dir_ + "/" + disk_name;
    std::ofstream ofs(dest, std::ios::binary);
    if (!ofs.is_open()) return "";
    ofs.write(data, size);
    ofs.close();
    return disk_name;
}

std::string FileStore::get_path(const std::string& disk_name) const {
    return files_dir_ + "/" + disk_name;
}

bool FileStore::remove(const std::string& disk_name) {
    std::string path = files_dir_ + "/" + disk_name;
    std::error_code ec;
    return fs::remove(path, ec);
}

bool FileStore::exists(const std::string& disk_name) const {
    return fs::exists(files_dir_ + "/" + disk_name);
}

} // namespace kiftd
