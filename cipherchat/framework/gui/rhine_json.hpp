#pragma once
#include "business_worker.hpp"
#include <nlohmann/json.hpp>

// JSON presentation of worker events and commands, shared by the Rhine web
// bridge process and the native Rhine Qt Quick client.
namespace ccfw::gui::rhine {
nlohmann::json eventJson(const WorkerEvent& item);
Command commandFromJson(const nlohmann::json& request);
}
