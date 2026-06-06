#pragma once
#ifndef HAPP_TRAY_H
#define HAPP_TRAY_H

#include <windows.h>

constexpr UINT WM_HP_TRAY_CMD = WM_APP + 43;

bool HAppTrayInit(HWND hwnd, HICON icon);
void HAppTrayShutdown();
bool HAppTrayHandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool HAppTrayExecuteCommand(UINT command_id);
void HAppTrayHideMainWindow(HWND hwnd);
void HAppTrayShowMainWindow(HWND hwnd);
void HAppTrayRequestQuit(HWND hwnd);
void HAppTrayRebuildMenu();

#endif
