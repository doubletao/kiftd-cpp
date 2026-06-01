#pragma once
#include <crow.h>
#include "database.h"
#include "auth.h"

namespace kiftd {

void register_auth_routes(crow::SimpleApp& app, Database& db, Auth& auth);

} // namespace kiftd
