#pragma once
#include "business_worker.hpp"
#include <nlohmann/json.hpp>

// JSON presentation of worker events and commands, shared by the Rhine web
// bridge process and the native Rhine Qt Quick client.
namespace ccfw::gui::rhine {
// 返回客户端界面使用的事件JSON。
nlohmann::json eventJson(const WorkerEvent& item);
// 返回客户端界面使用的命令FromJSON。
Command commandFromJson(const nlohmann::json& request);
}
