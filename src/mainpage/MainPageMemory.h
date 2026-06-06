#pragma once
#ifndef MAIN_PAGE_MEMORY_H
#define MAIN_PAGE_MEMORY_H

#include <cstdint>

struct MainPageMemoryStatus {
	bool running = false;
	bool has_result = false;
	int64_t avail_before = 0;
	int64_t avail_after = 0;
	int64_t freed_bytes = 0;
	int processes_trimmed = 0;
	int processes_failed = 0;
	bool system_purge_ok = false;
	uint64_t finished_tick = 0;
	char message[128] = {};
};

namespace MainPageMemory {
	void RequestRelease();
	bool IsRunning();
	MainPageMemoryStatus GetStatus();
}

#endif
