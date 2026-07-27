#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"
#include "transcode_manager.h"
#include "config.h"
#include <string>

namespace kiftd {

void register_folder_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                            TranscodeManager& transcode_mgr, const Config& cfg,
                            const std::string& secret_key);

} // namespace kiftd
