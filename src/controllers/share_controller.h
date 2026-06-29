#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"
#include <string>

namespace kiftd {

void register_share_routes(crow::SimpleApp& app, Database& db, FileStore& store, const std::string& secret_key);

} // namespace kiftd
