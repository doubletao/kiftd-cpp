#pragma once
#include <string>
#include <cstdint>

namespace kiftd {

std::string sha256(const std::string& input);
std::string generate_salt(size_t length = 32);

} // namespace kiftd
