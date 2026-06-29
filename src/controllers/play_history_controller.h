#pragma once
#include <crow.h>
#include <string>

namespace kiftd {
class Database;
struct Config;

void register_play_history_routes(crow::SimpleApp& app, Database& db, const Config& cfg, const std::string& secret_key);

} // namespace kiftd
