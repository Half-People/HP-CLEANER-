#include "HLogPipe.h"
#include "HLogRing.h"
#include <spdlog/sinks/base_sink.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
	std::mutex g_write_mutex;

	class log_pipe_sink final : public spdlog::sinks::base_sink<std::mutex> {
	public:
		void SetWriteHandle(HANDLE handle)
		{
			write_handle_.store(handle, std::memory_order_release);
		}

		void ClearWriteHandle()
		{
			write_handle_.store(INVALID_HANDLE_VALUE, std::memory_order_release);
		}

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			const HANDLE handle = write_handle_.load(std::memory_order_acquire);
			if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
				return;
			}

			spdlog::memory_buf_t formatted;
			formatter_->format(msg, formatted);
			formatted.push_back('\n');

			std::lock_guard<std::mutex> lock(g_write_mutex);
			DWORD written = 0;
			::WriteFile(handle, formatted.data(), static_cast<DWORD>(formatted.size()), &written, nullptr);
		}

		void flush_() override
		{
		}

	private:
		std::atomic<HANDLE> write_handle_{ INVALID_HANDLE_VALUE };
	};

	std::shared_ptr<log_pipe_sink> g_pipe_sink;
	HANDLE g_active_write_handle = INVALID_HANDLE_VALUE;
}

bool HLogPipeWriteBytes(const void* data, DWORD size)
{
	if (data == nullptr || size == 0 || g_active_write_handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	std::lock_guard<std::mutex> lock(g_write_mutex);
	DWORD written = 0;
	return ::WriteFile(g_active_write_handle, data, size, &written, nullptr) != FALSE
		&& written == size;
}

bool HLogPipeIsActive()
{
	return g_active_write_handle != INVALID_HANDLE_VALUE;
}

void HLogPipeAttachToSinks(std::vector<spdlog::sink_ptr>& sinks)
{
	if (g_pipe_sink == nullptr || g_active_write_handle == INVALID_HANDLE_VALUE) {
		return;
	}
	sinks.push_back(g_pipe_sink);
}

bool HLogPipeSetWriteHandle(HANDLE write_handle)
{
	if (write_handle == nullptr || write_handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	if (g_pipe_sink == nullptr) {
		g_pipe_sink = std::make_shared<log_pipe_sink>();
		g_pipe_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
	}
	g_active_write_handle = write_handle;
	g_pipe_sink->SetWriteHandle(write_handle);
	return true;
}

void HLogPipeClearWriteHandle()
{
	if (g_pipe_sink != nullptr) {
		g_pipe_sink->ClearWriteHandle();
	}
	if (g_active_write_handle != INVALID_HANDLE_VALUE) {
		::CloseHandle(g_active_write_handle);
		g_active_write_handle = INVALID_HANDLE_VALUE;
	}
}

void HLogPipeFlushHistory()
{
	const std::vector<std::string> history = HLogRingLastFormatted(200);
	for (const std::string& line : history) {
		std::string payload = line;
		payload.push_back('\n');
		HLogPipeWriteBytes(payload.data(), static_cast<DWORD>(payload.size()));
	}
	const char* banner = "[info] HP CLEANER++ log stream connected\n";
	HLogPipeWriteBytes(banner, static_cast<DWORD>(std::strlen(banner)));
}
