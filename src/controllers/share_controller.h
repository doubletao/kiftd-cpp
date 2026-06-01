#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"

namespace kiftd {

void register_share_routes(crow::SimpleApp& app, Database& db, FileStore& store);

} // namespace kiftd
