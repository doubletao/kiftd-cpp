#include "filestore.h"
#include "utils/uuid.h"
#include <fstream>
#include <filesystem>
#include <iostream>

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
    } catch (const std::exception& e) {
        std::cerr << "FileStore::save failed: " << e.what() << std::endl;
        return "";
    }
}

std::string FileStore::save_from_buffer(const char* data, size_t size) {
    std::string disk_name = generate_uuid() + ".bin";
    std::string dest = files_dir_ + "/" + disk_name;
    std::ofstream ofs(dest, std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "FileStore::save_from_buffer: failed to open " << dest << std::endl;
        return "";
    }
    ofs.write(data, size);
    if (ofs.fail()) {
        std::cerr << "FileStore::save_from_buffer: write failed" << std::endl;
        fs::remove(dest);
        return "";
    }
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

std::string FileStore::adopt_temp_file(const std::string& temp_path) {
    std::string disk_name = generate_uuid() + ".bin";
    std::string dest = files_dir_ + "/" + disk_name;
    try {
        fs::rename(temp_path, dest);
        return disk_name;
    } catch (const fs::filesystem_error&) {
        // Cross-device move fallback: copy + delete
        try {
            fs::copy_file(temp_path, dest, fs::copy_options::overwrite_existing);
            fs::remove(temp_path);
            return disk_name;
        } catch (const std::exception& e) {
            std::cerr << "FileStore::adopt_temp_file failed: " << e.what() << std::endl;
            return "";
        }
    }
}

} // namespace kiftd
