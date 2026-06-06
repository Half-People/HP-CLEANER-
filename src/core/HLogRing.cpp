#include "HLogRing.h"
#include "HPage.h"
#include <spdlog/sinks/ringbuffer_sink.h>
#include <memory>

namespace {
	std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_ring_sink;
}

void HLogRingAttachToSinks(std::vector<spdlog::sink_ptr>& sinks)
{
	if (g_ring_sink == nullptr) {
		g_ring_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(800);
		g_ring_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
	}
	sinks.push_back(g_ring_sink);
}

std::vector<std::string> HLogRingLastFormatted(size_t max_lines)
{
	if (!g_ring_sink) {
		return {};
	}
	return g_ring_sink->last_formatted(max_lines);
}

size_t HLogRingLineCount()
{
	if (!g_ring_sink) {
		return 0;
	}
	return g_ring_sink->last_formatted(0).size();
}

void HLogRingFlushFileSink()
{
	if (spdlog::default_logger() != nullptr) {
		spdlog::default_logger()->flush();
	}
}
