#include "utils/uuid.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace kiftd {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t data[4];
    for (auto& d : data) {
        d = dist(gen);
    }

    // Set version 4 (random)
    data[1] = (data[1] & 0xFFFF0FFFu) | 0x00004000u;
    // Set variant 1
    data[2] = (data[2] & 0x3FFFFFFFu) | 0x80000000u;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << data[0] << "-";
    oss << std::setw(4) << (data[1] >> 16) << "-";
    oss << std::setw(4) << (data[1] & 0xFFFF) << "-";
    oss << std::setw(4) << (data[2] >> 16) << "-";
    oss << std::setw(4) << (data[2] & 0xFFFF);
    oss << std::setw(8) << data[3];
    return oss.str();
}

} // namespace kiftd
