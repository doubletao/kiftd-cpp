#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"
#include "config.h"
#include <string>

namespace kiftd {

void register_file_routes(crow::SimpleApp& app, Database& db, FileStore& store, const Config& cfg);

} // namespace kiftd
