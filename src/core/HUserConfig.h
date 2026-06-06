#pragma once
#ifndef HUSER_CONFIG_H
#define HUSER_CONFIG_H

class HCleanTask;

// 設定檔：%APPDATA%\HalfPeople\HP CLEANER++\config\clean_tasks.json
void SaveTaskDetailConfig(const char* task_id);
void LoadAllTaskDetailConfigs();
void ApplyLoadedDetailConfig(HCleanTask* task);

#endif
