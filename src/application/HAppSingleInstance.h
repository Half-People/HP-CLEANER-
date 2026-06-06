#pragma once
#ifndef HAPP_SINGLE_INSTANCE_H
#define HAPP_SINGLE_INSTANCE_H

#include <windows.h>

constexpr UINT WM_HP_SHOW_WINDOW = WM_APP + 42;

// 僅允許一個 GUI 主程式實例；重複啟動時喚醒既有視窗。
bool HAppSingleInstanceAcquire();
void HAppSingleInstanceRelease();

#endif
