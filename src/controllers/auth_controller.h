#pragma once
#include <crow.h>
#include "database.h"
#include "auth.h"
#include "config.h"

namespace kiftd {

void register_auth_routes(crow::SimpleApp& app, Database& db, Auth& auth, const Config& cfg);

} // namespace kiftd
