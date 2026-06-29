#pragma once
#include <string>
#include <crow.h>

namespace kiftd {

// Cookie签名验证相关
std::string sign_cookie(const std::string& username, const std::string& secret_key, int expire_hours = 24);
std::string verify_cookie(const std::string& cookie_value, const std::string& secret_key);

// 从请求中提取用户名（已签名验证）
std::string get_user(const crow::request& req, const std::string& secret_key);

// MIME类型映射
std::string get_content_type(const std::string& filename);

// Windows encoding conversion
#ifdef _WIN32
#include <windows.h>
std::wstring utf8_to_wide(const std::string& s);
#endif

// FFmpeg path escaping
std::string escape_vf_path(const std::string& path);

} // namespace kiftd
