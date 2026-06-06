#pragma once
#ifndef HAPP_SETTINGS_H
#define HAPP_SETTINGS_H

// 應用程式偏好：%APPDATA%\HalfPeople\HP CLEANER++\config\app_settings.json
void HAppSettingsLoad();
void HAppSettingsSave();

bool HAppSettingsGetConsoleLogger();
void HAppSettingsSetConsoleLogger(bool enabled);

bool HAppSettingsGetRunAtStartup();
bool HAppSettingsGetRunAtStartupElevated();
bool HAppSettingsSetRunAtStartup(bool enabled);
// UAC 提權成功後：若已啟用開機自啟，改為排程工作（最高權限）註冊
bool HAppSettingsPromoteStartupToElevatedIfEnabled();

const char* HAppSettingsGetLanguageCode();
void HAppSettingsSetLanguageCode(const char* code);

#endif
