#pragma once
#ifndef HLOG_RING_H
#define HLOG_RING_H

#include <spdlog/sinks/sink.h>
#include <memory>
#include <string>
#include <vector>

void HLogRingAttachToSinks(std::vector<spdlog::sink_ptr>& sinks);
std::vector<std::string> HLogRingLastFormatted(size_t max_lines);
size_t HLogRingLineCount();
void HLogRingFlushFileSink();

#endif
