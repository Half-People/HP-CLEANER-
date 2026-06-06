#pragma once
#ifndef HLOG_PIPE_H
#define HLOG_PIPE_H

#include <spdlog/sinks/sink.h>
#include <vector>
#include <windows.h>

// 主進程透過匿名 Pipe 寫入端即時推送日志至控制台子進程
void HLogPipeAttachToSinks(std::vector<spdlog::sink_ptr>& sinks);
bool HLogPipeSetWriteHandle(HANDLE write_handle);
void HLogPipeClearWriteHandle();
bool HLogPipeIsActive();
bool HLogPipeWriteBytes(const void* data, DWORD size);
void HLogPipeFlushHistory();

#endif
