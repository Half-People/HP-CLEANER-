#include "HUserConfig.h"
#include "HAppPaths.h"
#include "HCleanTask.h"
#include "HPage.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

namespace {
	const char* kConfigFileName = "clean_tasks.json";

	const char* MapLegacyTaskId(const char* task_id)
	{
		static const std::pair<const char*, const char*> kLegacyTaskIds[] = {
			{ "bb_legacy_web", "advanced_legacy_web" },
			{ "bb_legacy_plugins", "advanced_legacy_plugins" },
			{ "bb_rdp_remote", "advanced_rdp_remote" },
			{ "bb_office_media", "advanced_office_media" },
			{ "bb_skype_pidgin", "advanced_skype_pidgin" },
			{ "bb_vm_transfer", "advanced_vm_transfer" },
			{ "bb_explorer_extras", "advanced_explorer_extras" },
			{ "bb_deepscan_lite", "advanced_deepscan_lite" },
			{ "bb_cleaner_logs", "advanced_third_party_logs" },
		};
		if (task_id == nullptr) {
			return nullptr;
		}
		for (const auto& entry : kLegacyTaskIds) {
			if (std::strcmp(task_id, entry.first) == 0) {
				return entry.second;
			}
		}
		return task_id;
	}

	std::string GetConfigFilePath()
	{
		const std::string dir = HAppPaths::GetConfigDir();
		if (dir.empty()) {
			return kConfigFileName;
		}
		return dir + kConfigFileName;
	}

	void MigrateLegacyTaskKeys(nlohmann::json& root)
	{
		if (!root.contains("tasks") || !root["tasks"].is_object()) {
			return;
		}

		nlohmann::json& tasks = root["tasks"];
		std::vector<std::string> legacy_keys;
		for (auto it = tasks.begin(); it != tasks.end(); ++it) {
			const char* mapped = MapLegacyTaskId(it.key().c_str());
			if (mapped != nullptr && std::strcmp(mapped, it.key().c_str()) != 0) {
				legacy_keys.push_back(it.key());
			}
		}

		bool changed = false;
		for (const std::string& legacy_key : legacy_keys) {
			const char* new_key = MapLegacyTaskId(legacy_key.c_str());
			if (new_key == nullptr || tasks.contains(new_key)) {
				tasks.erase(legacy_key);
			}
			else {
				tasks[new_key] = std::move(tasks[legacy_key]);
				tasks.erase(legacy_key);
			}
			changed = true;
			HLOG_INFO("Migrated detail config task id '{}' -> '{}'", legacy_key, new_key);
		}

		if (changed) {
			HAppPaths::EnsureAppDataDirs();
			const std::string config_path = GetConfigFilePath();
			std::ofstream out(config_path, std::ios::trunc);
			if (out.is_open()) {
				out << root.dump(2);
				HLOG_INFO("Saved migrated detail config to '{}'", config_path);
			}
			else {
				HLOG_WARN("MigrateLegacyTaskKeys: cannot write '{}'", config_path);
			}
		}
	}

	void ApplyEntriesFromJson(HCleanTask* task, const nlohmann::json& entries_json)
	{
		if (task == nullptr || !entries_json.is_array()) {
			return;
		}

		const size_t count = task->GetDetailEntryCount();
		if (count == 0) {
			return;
		}

		std::unordered_map<std::string, bool> path_selected;
		for (const auto& item : entries_json) {
			if (!item.is_object()) {
				continue;
			}
			const auto path_it = item.find("path");
			const auto sel_it = item.find("selected");
			if (path_it == item.end() || !path_it->is_string()) {
				continue;
			}
			const std::string path = path_it->get<std::string>();
			const bool selected = (sel_it != item.end() && sel_it->is_boolean())
				? sel_it->get<bool>()
				: true;
			path_selected[path] = selected;
		}

		for (size_t i = 0; i < count; ++i) {
			HCleanDetailEntry* entry = task->GetDetailEntry(i);
			if (entry == nullptr || entry->path == nullptr || entry->path[0] == '\0') {
				continue;
			}
			const auto it = path_selected.find(entry->path);
			if (it != path_selected.end()) {
				entry->selected = it->second;
			}
		}

		task->OnDetailConfigLoaded();
	}
}

void ApplyLoadedDetailConfig(HCleanTask* task)
{
	if (task == nullptr) {
		return;
	}

	const std::string config_path = GetConfigFilePath();
	std::ifstream in(config_path);
	if (!in.is_open()) {
		return;
	}

	nlohmann::json root;
	try {
		in >> root;
	}
	catch (const std::exception& ex) {
		HLOG_WARN("Load detail config: parse error for task '{}': {}", task->GetId(), ex.what());
		return;
	}

	if (!root.contains("tasks") || !root["tasks"].is_object()) {
		return;
	}

	MigrateLegacyTaskKeys(root);

	const nlohmann::json& tasks = root["tasks"];
	const char* task_id = task->GetId();
	const char* lookup_id = MapLegacyTaskId(task_id);
	if (lookup_id == nullptr || !tasks.contains(lookup_id) || !tasks[lookup_id].is_object()) {
		return;
	}

	const nlohmann::json& task_obj = tasks[lookup_id];
	if (!task_obj.contains("entries") || !task_obj["entries"].is_array()) {
		return;
	}

	ApplyEntriesFromJson(task, task_obj["entries"]);
	HLOG_INFO("Loaded detail config for task '{}'", task_id);
}

void LoadAllTaskDetailConfigs()
{
	const std::string config_path = GetConfigFilePath();
	std::ifstream in(config_path);
	if (!in.is_open()) {
		HLOG_INFO("No detail config file at '{}' (using defaults)", config_path);
		return;
	}

	nlohmann::json root;
	try {
		in >> root;
	}
	catch (const std::exception& ex) {
		HLOG_WARN("LoadAllTaskDetailConfigs: parse error: {}", ex.what());
		return;
	}

	if (!root.contains("tasks") || !root["tasks"].is_object()) {
		HLOG_WARN("LoadAllTaskDetailConfigs: missing 'tasks' object");
		return;
	}

	MigrateLegacyTaskKeys(root);

	const nlohmann::json& tasks_json = root["tasks"];
	size_t applied = 0;
	for (auto it = tasks_json.begin(); it != tasks_json.end(); ++it) {
		HCleanTask* task = FindCleanTask(it.key().c_str());
		if (task == nullptr) {
			HLOG_WARN("LoadAllTaskDetailConfigs: unknown task id '{}'", it.key());
			continue;
		}
		if (!it.value().is_object() || !it.value().contains("entries")) {
			continue;
		}
		ApplyEntriesFromJson(task, it.value()["entries"]);
		++applied;
	}

	HLOG_INFO("LoadAllTaskDetailConfigs: applied {} task(s) from '{}'", applied, config_path);
}

void SaveTaskDetailConfig(const char* task_id)
{
	if (task_id == nullptr || task_id[0] == '\0') {
		return;
	}

	HCleanTask* task = FindCleanTask(task_id);
	if (task == nullptr) {
		HLOG_WARN("SaveTaskDetailConfig: task '{}' not found", task_id);
		return;
	}

	const size_t count = task->GetDetailEntryCount();
	nlohmann::json entries = nlohmann::json::array();
	for (size_t i = 0; i < count; ++i) {
		const HCleanDetailEntry* entry = task->GetDetailEntry(i);
		if (entry == nullptr || entry->path == nullptr || entry->path[0] == '\0') {
			continue;
		}
		entries.push_back({
			{ "path", entry->path },
			{ "selected", entry->selected },
		});
	}

	const std::string config_path = GetConfigFilePath();
	nlohmann::json root;
	std::ifstream in(config_path);
	if (in.is_open()) {
		try {
			in >> root;
		}
		catch (...) {
			root = nlohmann::json::object();
		}
	}
	if (!root.is_object()) {
		root = nlohmann::json::object();
	}
	if (!root.contains("tasks") || !root["tasks"].is_object()) {
		root["tasks"] = nlohmann::json::object();
	}
	root["tasks"][task_id] = { { "entries", std::move(entries) } };

	HAppPaths::EnsureAppDataDirs();
	std::ofstream out(config_path, std::ios::trunc);
	if (!out.is_open()) {
		HLOG_ERROR("SaveTaskDetailConfig: cannot write '{}'", config_path);
		return;
	}
	out << root.dump(2);
	HLOG_INFO("Saved detail config for task '{}' to '{}'", task_id, config_path);
}
