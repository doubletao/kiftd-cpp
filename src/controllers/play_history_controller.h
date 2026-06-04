#pragma once
#include <crow.h>

namespace kiftd {
class Database;
struct Config;

void register_play_history_routes(crow::SimpleApp& app, Database& db, const Config& cfg);

} // namespace kiftd
