#include "controllers/file_controller.h"
#include "utils/uuid.h"
#include "utils/common.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iostream>

namespace fs = std::filesystem;

namespace kiftd {

// Extract multipart boundary from Content-Type header
static std::string extract_boundary(const crow::request& req) {
    auto ct = req.get_header_value("Content-Type");
    auto pos = ct.find("boundary=");
    if (pos == std::string::npos) return {};
    std::string b = ct.substr(pos + 9);
    if (!b.empty() && b.front() == '"') b = b.substr(1, b.size() - 2);
    auto semi = b.find(';');
    if (semi != std::string::npos) b = b.substr(0, semi);
    return b;
}

// Process a streamed upload: parse multipart from temp file, stream file data directly to disk.
// Strategy: read headers into memory to find boundaries, then stream file data between boundaries.
static bool process_streamed_upload(
    const std::string& temp_path, const std::string& boundary,
    FileStore& store, const Config& cfg,
    std::string& folder_id, std::string& file_name,
    std::string& disk_name, int64_t& file_size)
{
    std::cerr << "[upload-stream] temp_path=" << temp_path << " boundary=" << boundary << std::endl;

    std::ifstream ifs(temp_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        std::cerr << "[upload-stream] ERROR: cannot open temp file" << std::endl;
        return false;
    }

    int64_t total_file_size = ifs.tellg();
    ifs.seekg(0);
    std::cerr << "[upload-stream] total temp file size=" << total_file_size << std::endl;

    std::string delimiter = "--" + boundary;
    std::string delimiter_end = delimiter + "--";

    // Step 1: Read the first portion of the file to find all boundary markers and parse headers.
    // For a multipart upload with folder_id + file, the structure is:
    //   --boundary\r\n headers \r\n \r\n body \r\n --boundary\r\n headers \r\n \r\n file_data \r\n --boundary--\r\n
    // We read enough to capture the first two delimiters (the file data starts after the second delimiter).
    
    const size_t HEADER_BUF_SIZE = 256 * 1024; // 256KB should be more than enough for headers
    std::string header_buf;
    header_buf.resize(HEADER_BUF_SIZE);
    
    // Read up to HEADER_BUF_SIZE bytes
    ifs.read(header_buf.data(), HEADER_BUF_SIZE);
    auto bytes_read = ifs.gcount();
    header_buf.resize(bytes_read);

    std::cerr << "[upload-stream] read " << bytes_read << " bytes of header" << std::endl;

    // Find the first delimiter (start of first part)
    size_t first_delim = header_buf.find(delimiter);
    if (first_delim == std::string::npos) {
        std::cerr << "[upload-stream] ERROR: first delimiter not found" << std::endl;
        return false;
    }

    // Find the second delimiter (start of second part = file part)
    size_t second_delim_start = first_delim + delimiter.size();
    // Skip \r\n after delimiter
    if (second_delim_start + 1 < header_buf.size() && 
        header_buf[second_delim_start] == '\r' && header_buf[second_delim_start + 1] == '\n') {
        second_delim_start += 2;
    }
    size_t second_delim = header_buf.find(delimiter, second_delim_start);
    if (second_delim == std::string::npos) {
        std::cerr << "[upload-stream] ERROR: second delimiter not found" << std::endl;
        return false;
    }

    // Parse first part (folder_id) between first and second delimiters
    std::string first_part = header_buf.substr(first_delim + delimiter.size(), second_delim - first_delim - delimiter.size());
    // Remove leading \r\n
    if (first_part.size() >= 2 && first_part[0] == '\r' && first_part[1] == '\n') {
        first_part = first_part.substr(2);
    }
    // Split headers from body at \r\n\r\n
    auto header_body_sep = first_part.find("\r\n\r\n");
    if (header_body_sep != std::string::npos) {
        folder_id = first_part.substr(header_body_sep + 4);
        // Remove trailing \r\n
        if (folder_id.size() >= 2 && folder_id.substr(folder_id.size() - 2) == "\r\n") {
            folder_id.resize(folder_id.size() - 2);
        }
    }
    std::cerr << "[upload-stream] folder_id=[" << folder_id << "]" << std::endl;

    // Parse second part headers (file metadata)
    size_t second_part_start = second_delim + delimiter.size();
    // Skip \r\n after delimiter
    if (second_part_start + 1 < header_buf.size() && 
        header_buf[second_part_start] == '\r' && header_buf[second_part_start + 1] == '\n') {
        second_part_start += 2;
    }

    // Find the end of second part headers (\r\n\r\n)
    auto second_part_headers_end = header_buf.find("\r\n\r\n", second_part_start);
    if (second_part_headers_end == std::string::npos) {
        std::cerr << "[upload-stream] ERROR: second part headers not found (file headers may span beyond read buffer)" << std::endl;
        return false;
    }

    std::string second_part_headers = header_buf.substr(second_part_start, second_part_headers_end - second_part_start);
    
    // Extract filename
    auto fn_pos = second_part_headers.find("filename=\"");
    if (fn_pos != std::string::npos) {
        auto fn_end = second_part_headers.find("\"", fn_pos + 10);
        if (fn_end != std::string::npos) {
            file_name = second_part_headers.substr(fn_pos + 10, fn_end - fn_pos - 10);
        }
    }
    std::cerr << "[upload-stream] file_name=[" << file_name << "]" << std::endl;

    // The file data starts right after the \r\n\r\n of the second part headers
    auto file_data_start_offset = second_part_headers_end + 4; // skip \r\n\r\n
    // But file_data_start_offset is relative to header_buf, and header_buf starts at file offset 0
    // So file_data_start_offset is also the absolute file offset (since we read from the beginning)
    std::cerr << "[upload-stream] file_data_start_offset=" << file_data_start_offset << std::endl;

    // Find the end boundary: it's \r\n + delimiter_end somewhere in the file
    // The end boundary should be within the last few bytes of the file
    // Read the last portion to find it
    const size_t TAIL_BUF_SIZE = 4096;
    std::string tail_buf;
    
    if (total_file_size > static_cast<std::streamoff>(TAIL_BUF_SIZE + file_data_start_offset)) {
        // Read the last TAIL_BUF_SIZE bytes
        auto tail_start = total_file_size - TAIL_BUF_SIZE;
        ifs.seekg(tail_start);
        tail_buf.resize(TAIL_BUF_SIZE);
        ifs.read(tail_buf.data(), TAIL_BUF_SIZE);
        tail_buf.resize(ifs.gcount());
    } else {
        // File is small enough that we already have everything in header_buf
        tail_buf = header_buf.substr(file_data_start_offset);
    }

    // Find \r\n + delimiter_end in the tail buffer
    auto end_boundary_pos_in_tail = tail_buf.find("\r\n" + delimiter_end);
    
    // Calculate the absolute file offset where the end boundary starts
    int64_t file_data_end_offset;
    if (end_boundary_pos_in_tail != std::string::npos) {
        // end_boundary_pos_in_tail is relative to the start of tail_buf
        if (total_file_size > static_cast<std::streamoff>(TAIL_BUF_SIZE + file_data_start_offset)) {
            file_data_end_offset = (total_file_size - TAIL_BUF_SIZE) + end_boundary_pos_in_tail;
        } else {
            file_data_end_offset = file_data_start_offset + end_boundary_pos_in_tail;
        }
    } else {
        // Fallback: search in header_buf (for very small files)
        auto end_boundary_in_header = header_buf.find("\r\n" + delimiter_end, file_data_start_offset);
        if (end_boundary_in_header != std::string::npos) {
            file_data_end_offset = end_boundary_in_header;
        } else {
            std::cerr << "[upload-stream] ERROR: end boundary not found" << std::endl;
            return false;
        }
    }

    file_size = file_data_end_offset - file_data_start_offset;
    std::cerr << "[upload-stream] file_data_end_offset=" << file_data_end_offset 
              << " file_size=" << file_size << std::endl;

    // Check max upload size
    if (cfg.max_upload_size > 0 && file_size > cfg.max_upload_size) {
        std::cerr << "[upload-stream] ERROR: file too large (" << file_size << " > " << cfg.max_upload_size << ")" << std::endl;
        return false;
    }

    // Stream file data from temp file directly into the store
    std::string dest_name = generate_uuid() + ".bin";
    std::string dest_path = store.get_path(dest_name);
    std::ofstream file_ofs(dest_path, std::ios::binary);
    if (!file_ofs.is_open()) {
        std::cerr << "[upload-stream] ERROR: cannot create temp file for file data" << std::endl;
        return false;
    }

    ifs.seekg(file_data_start_offset);
    const size_t COPY_BUF_SIZE = 256 * 1024;
    char copy_buf[COPY_BUF_SIZE];
    int64_t remaining = file_size;
    while (remaining > 0) {
        size_t to_read = static_cast<size_t>(std::min(static_cast<int64_t>(COPY_BUF_SIZE), remaining));
        ifs.read(copy_buf, to_read);
        auto got = ifs.gcount();
        if (got == 0) break;
        file_ofs.write(copy_buf, got);
        remaining -= got;
    }
    file_ofs.close();

    disk_name = dest_name;

    std::cerr << "[upload-stream] success: folder_id=[" << folder_id << "] file_name=[" << file_name 
              << "] disk_name=[" << disk_name << "] file_size=" << file_size << std::endl;

    return !folder_id.empty() && !disk_name.empty();
}

void register_file_routes(crow::SimpleApp& app, Database& db, FileStore& store, const Config& cfg) {

    // POST /api/files/upload - upload file (multipart)
    CROW_ROUTE(app, "/api/files/upload")
        .methods("POST"_method)
    ([&db, &store, &cfg](const crow::request& req) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        std::string folder_id;
        std::string file_name;
        std::string disk_name;
        int64_t file_size = 0;

        if (!req.body_stream_path.empty()) {
            // Streaming path: body was written to a temp file by the parser
            std::cerr << "[upload] streaming path, body_stream_path=" << req.body_stream_path << std::endl;
            std::string boundary = extract_boundary(req);
            if (boundary.empty()) {
                std::cerr << "[upload] ERROR: missing multipart boundary" << std::endl;
                return crow::response(400, R"({"error":"missing multipart boundary"})");
            }

            if (!process_streamed_upload(req.body_stream_path, boundary, store, cfg,
                                         folder_id, file_name, disk_name, file_size)) {
                // Clean up any partially written file in the store
                if (!disk_name.empty()) store.remove(disk_name);
                return crow::response(400, R"({"error":"invalid upload or file too large"})");
            }

            // file already written directly to store by process_streamed_upload
        } else {
            // Normal path: body is in req.body (small files)
            crow::multipart::message msg(req);

            for (auto& part : msg.parts) {
                auto it = part.headers.find("Content-Disposition");
                if (it == part.headers.end()) continue;

                auto& params = it->second.params;
                auto name_it = params.find("name");
                if (name_it == params.end()) continue;

                if (name_it->second == "folder_id") {
                    folder_id = std::string(part.body.begin(), part.body.end());
                } else if (name_it->second == "file") {
                    auto fn_it = params.find("filename");
                    if (fn_it != params.end()) {
                        file_name = fn_it->second;
                    }
                    disk_name = store.save_from_buffer(part.body.data(), part.body.size());
                    file_size = part.body.size();
                }
            }
        }

        if (folder_id.empty() || file_name.empty() || disk_name.empty()) {
            return crow::response(400, R"({"error":"missing folder_id or file"})");
        }

        // Check folder exists
        if (folder_id != "root") {
            auto folder = db.get_folder(folder_id);
            if (folder.id.empty()) {
                store.remove(disk_name);
                return crow::response(404, R"({"error":"folder not found"})");
            }
        }

        // Create DB record
        FileRecord f;
        f.id = generate_uuid();
        f.name = file_name;
        f.size = file_size;
        f.disk_name = disk_name;
        f.folder_id = folder_id == "root" ? "" : folder_id;
        f.creator = user;

        if (!db.create_file(f)) {
            store.remove(disk_name);
            return crow::response(500, R"({"error":"db insert failed"})");
        }

        return crow::response(201, nlohmann::json{{"id", f.id}, {"name", f.name}, {"size", f.size}}.dump());
    });

    // GET /api/files/<string>/download - download file
    CROW_ROUTE(app, "/api/files/<string>/download")
        .methods("GET"_method)
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing on disk"})");

        // Stream file
        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Disposition", "attachment; filename=\"" + file.name + "\"");
        res.add_header("Content-Type", get_content_type(file.name));
        return res;
    });

    // GET /api/files/<string>/preview - preview txt/image
    CROW_ROUTE(app, "/api/files/<string>/preview")
        .methods("GET"_method)
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string path = store.get_path(file.disk_name);
        if (!fs::exists(path)) return crow::response(404, R"({"error":"file missing"})");

        std::string ct = get_content_type(file.name);
        // Only allow preview for text, images, audio and mp4 video
        if (ct.find("text/") == std::string::npos &&
            ct.find("image/") == std::string::npos &&
            ct.find("audio/") == std::string::npos &&
            ct != "video/mp4" &&
            ct != "application/json" &&
            ct != "application/javascript") {
            return crow::response(403, R"({"error":"preview not supported"})");
        }

        // Handle Range requests for video seeking
        if (ct == "video/mp4") {
            crow::response res;
            auto range_header = req.get_header_value("Range");
            uint64_t file_size = fs::file_size(path);

            if (!range_header.empty() && range_header.find("bytes=") == 0) {
                try {
                    std::string range_val = range_header.substr(6);
                    auto dash_pos = range_val.find('-');
                    uint64_t start = 0, end = file_size - 1;

                    if (dash_pos == 0) {
                        start = file_size - std::stoull(range_val.substr(1));
                    } else if (dash_pos == std::string::npos) {
                        start = std::stoull(range_val);
                    } else {
                        start = std::stoull(range_val.substr(0, dash_pos));
                        if (dash_pos + 1 < range_val.size()) {
                            end = std::stoull(range_val.substr(dash_pos + 1));
                        }
                    }

                    if (start >= file_size) {
                        res.code = 416;
                        res.add_header("Content-Range", "bytes */" + std::to_string(file_size));
                        return res;
                    }

                    uint64_t length = end - start + 1;
                    std::ifstream ifs(path, std::ios::binary);
                    ifs.seekg(start);
                    std::string buf(length, '\0');
                    ifs.read(buf.data(), length);

                    res.code = 206;
                    res.body = buf;
                    res.add_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
                    res.add_header("Content-Length", std::to_string(length));
                } catch (const std::exception&) {
                    res.code = 416;
                    res.add_header("Content-Range", "bytes */" + std::to_string(file_size));
                    return res;
                }
            } else {
                // No Range header: return first 1MB to avoid sending entire file
                uint64_t length = std::min(static_cast<uint64_t>(1024 * 1024), file_size);
                std::ifstream ifs(path, std::ios::binary);
                std::string buf(length, '\0');
                ifs.read(buf.data(), length);

                res.code = 206;
                res.body = buf;
                res.add_header("Content-Range", "bytes 0-" + std::to_string(length - 1) + "/" + std::to_string(file_size));
                res.add_header("Content-Length", std::to_string(length));
            }

            res.add_header("Content-Type", ct);
            res.add_header("Accept-Ranges", "bytes");
            return res;
        }

        crow::response res;
        res.set_static_file_info(path);
        res.add_header("Content-Type", ct);
        return res;
    });

    // PUT /api/files/<string> - rename file
    CROW_ROUTE(app, "/api/files/<string>")
        .methods("PUT"_method)
    ([&db, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("name")) {
            return crow::response(400, R"({"error":"invalid request"})");
        }

        std::string new_name = body["name"].get<std::string>();
        if (new_name.empty()) return crow::response(400, R"({"error":"name required"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        if (!db.rename_file(file_id, new_name)) {
            return crow::response(500, R"({"error":"rename failed"})");
        }

        return crow::response(200, R"({"ok":true})");
    });

    // DELETE /api/files/<string> - delete file
    CROW_ROUTE(app, "/api/files/<string>")
        .methods("DELETE"_method)
    ([&db, &store, &cfg](const crow::request& req, const std::string& file_id) {
        std::string user = get_user(req, cfg.secret_key);
        if (user.empty()) return crow::response(401, R"({"error":"not logged in"})");

        auto file = db.get_file(file_id);
        if (file.id.empty()) return crow::response(404, R"({"error":"file not found"})");

        std::string disk_name = file.disk_name;
        if (!db.delete_file(file_id)) {
            return crow::response(500, R"({"error":"delete failed"})");
        }
        store.remove(disk_name);

        return crow::response(200, R"({"ok":true})");
    });
}

} // namespace kiftd
