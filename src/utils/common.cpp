#include "common.h"
#include "sha256.h"
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

namespace kiftd {

std::string sign_cookie(const std::string& username, const std::string& secret_key, int expire_hours) {
    auto now = std::chrono::system_clock::now();
    auto expires = now + std::chrono::hours(expire_hours);
    auto expires_t = std::chrono::system_clock::to_time_t(expires);
    
    std::string data = username + "|" + std::to_string(expires_t);
    std::string signature = sha256(data + "|" + secret_key);
    
    return username + "|" + std::to_string(expires_t) + "|" + signature;
}

std::string verify_cookie(const std::string& cookie_value, const std::string& secret_key) {
    auto pos1 = cookie_value.find('|');
    if (pos1 == std::string::npos) return "";
    
    auto pos2 = cookie_value.find('|', pos1 + 1);
    if (pos2 == std::string::npos) return "";
    
    std::string username = cookie_value.substr(0, pos1);
    std::string expires_str = cookie_value.substr(pos1 + 1, pos2 - pos1 - 1);
    std::string signature = cookie_value.substr(pos2 + 1);
    
    long long expires = std::stoll(expires_str);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (now > expires) return "";
    
    std::string data = username + "|" + expires_str;
    std::string expected = sha256(data + "|" + secret_key);
    if (signature != expected) return "";
    
    return username;
}

std::string get_user(const crow::request& req, const std::string& secret_key) {
    auto cookie = req.get_header_value("Cookie");
    auto pos = cookie.find("kiftd_user=");
    if (pos == std::string::npos) return "";
    auto start = pos + 11;
    auto end = cookie.find(';', start);
    std::string value = cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
    
    if (secret_key.empty()) {
        return value;
    }
    return verify_cookie(value, secret_key);
}

std::string get_content_type(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == "txt" || ext == "text") return "text/plain; charset=utf-8";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "zip") return "application/zip";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "wav") return "audio/wav";
    if (ext == "ogg") return "audio/ogg";
    if (ext == "flac") return "audio/flac";
    if (ext == "aac") return "audio/aac";
    if (ext == "webm") return "video/webm";
    if (ext == "mp4") return "video/mp4";
    if (ext == "m3u8") return "application/x-mpegURL";
    if (ext == "ts") return "video/mp2t";
    return "application/octet-stream";
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}
#endif

std::string escape_vf_path(const std::string& path) {
    std::string result;
    result.reserve(path.size() * 2);
    for (char c : path) {
        if (c == '\\' || c == '\'' || c == ':' || c == '[' || c == ']') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

} // namespace kiftd
