#pragma once
#include <crow.h>
#include "database.h"

namespace kiftd {

void register_folder_routes(crow::SimpleApp& app, Database& db);

} // namespace kiftd
