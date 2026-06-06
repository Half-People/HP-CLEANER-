#pragma once
#ifndef HAPP_SHELL_H
#define HAPP_SHELL_H

#include <windows.h>

// 主視窗、系統匣與管理員提權（背景 broker，不關閉目前視窗）
void HAppShellSetMainWindow(HWND hwnd);
HWND HAppShellGetMainWindow();
void HAppShellUpdateWindowTitle();
void HAppShellShowMainWindow();
void HAppShellHideMainWindow();
void HAppShellRequestApplicationQuit();
bool HAppShellRequestAdminElevation(bool exit_current_on_success);

#endif
