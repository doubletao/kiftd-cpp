#pragma once
#include <crow.h>
#include "database.h"
#include "auth.h"
#include <string>

namespace kiftd {

void register_admin_routes(crow::SimpleApp& app, Database& db, Auth& auth, const std::string& secret_key);

} // namespace kiftd
