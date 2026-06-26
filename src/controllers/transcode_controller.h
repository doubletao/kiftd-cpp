#pragma once
#include <crow.h>
#include "database.h"
#include "filestore.h"
#include "config.h"
#include "transcode_manager.h"
#include "live_segmenter.h"

namespace kiftd {

void register_transcode_routes(crow::SimpleApp& app, Database& db, FileStore& store,
                                TranscodeManager& mgr, LiveSegmenter& segmenter, const Config& cfg);

} // namespace kiftd
