#pragma once
#include <crow.h>
#include "database.h"
#include <string>

namespace kiftd {

void register_folder_routes(crow::SimpleApp& app, Database& db, const std::string& secret_key);

} // namespace kiftd
