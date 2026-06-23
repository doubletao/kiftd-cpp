#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"
#include "config.h"
#include "transcode_manager.h"
#include "live_session_manager.h"

namespace kiftd {

void register_transcode_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                                TranscodeManager& mgr, LiveSessionManager& live_mgr, const Config& cfg);

} // namespace kiftd
