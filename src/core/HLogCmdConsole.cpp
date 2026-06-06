#include "HLogCmdConsole.h"
#include "Hi18n.h"
#include "HLogPipe.h"
#include "HPage.h"
#include "resource.h"
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace {
	static const wchar_t* ConsoleTitle()
	{
		return W18N(u8"HP CLEANER++ - 日誌控制台");
	}
	constexpr wchar_t kMainWindowClass[] = L"HP_CLEANER_WindowClass";

	volatile bool g_console_running = true;
	HANDLE g_child_process = nullptr;
	std::thread g_child_watcher;
	std::atomic<bool> g_parent_initiated_shutdown{ false };

	struct PipeReaderState {
		HANDLE pipe = INVALID_HANDLE_VALUE;
		std::mutex pending_mutex;
		std::string pending;
		std::atomic<bool> running{ true };
		std::thread thread;
	};

	DWORD g_session_parent_pid = 0;
	PipeReaderState* g_session_reader = nullptr;
	std::atomic<bool> g_parent_notified{ false };

	void NotifyParentConsoleClosed(DWORD parent_pid);

	void NotifyParentConsoleClosedOnce(DWORD parent_pid)
	{
		if (parent_pid == 0 || g_parent_notified.exchange(true)) {
			return;
		}
		NotifyParentConsoleClosed(parent_pid);
	}

	void RequestConsoleShutdown()
	{
		g_console_running = false;
		NotifyParentConsoleClosedOnce(g_session_parent_pid);
		if (g_session_reader != nullptr) {
			g_session_reader->running.store(false);
			const HANDLE pipe = g_session_reader->pipe;
			if (pipe != nullptr && pipe != INVALID_HANDLE_VALUE) {
				g_session_reader->pipe = INVALID_HANDLE_VALUE;
				::CloseHandle(pipe);
			}
		}
	}

	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type)
	{
		if (ctrl_type == CTRL_CLOSE_EVENT || ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
			RequestConsoleShutdown();
			return TRUE;
		}
		return FALSE;
	}

	bool EnsureAttachedConsole()
	{
		if (::GetConsoleWindow() == nullptr) {
			if (!::AllocConsole()) {
				return false;
			}
		}

		FILE* stream = nullptr;
		(void)freopen_s(&stream, "CONOUT$", "w", stdout);
		(void)freopen_s(&stream, "CONOUT$", "w", stderr);
		(void)freopen_s(&stream, "CONIN$", "r", stdin);

		::SetConsoleTitleW(ConsoleTitle());
		::SetConsoleOutputCP(CP_UTF8);
		::SetConsoleCP(CP_UTF8);

		const HWND console_hwnd = ::GetConsoleWindow();
		if (console_hwnd != nullptr) {
			const HINSTANCE hinst = ::GetModuleHandleW(nullptr);
			const HICON icon = static_cast<HICON>(
				::LoadImageW(hinst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
			if (icon != nullptr) {
				::SendMessageW(console_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
				::SendMessageW(console_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
			}
			::ShowWindow(console_hwnd, SW_SHOW);
			::SetForegroundWindow(console_hwnd);
		}

		HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (out != nullptr && out != INVALID_HANDLE_VALUE) {
			DWORD mode = 0;
			if (::GetConsoleMode(out, &mode)) {
				::SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
			}
		}
		return true;
	}

	void WriteConsoleText(const std::wstring& text)
	{
		if (text.empty()) {
			return;
		}
		std::fputws(text.c_str(), stdout);
		std::fflush(stdout);
	}

	void TrimCr(std::string& line)
	{
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
	}

	void StripAnsiEscapes(std::string& text)
	{
		std::string cleaned;
		cleaned.reserve(text.size());
		for (size_t i = 0; i < text.size();) {
			if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
				i += 2;
				while (i < text.size() && text[i] != 'm') {
					++i;
				}
				if (i < text.size()) {
					++i;
				}
				continue;
			}
			cleaned.push_back(text[i]);
			++i;
		}
		text.swap(cleaned);
	}

	const char* AnsiColorForLine(const std::string& line)
	{
		auto has = [&](const char* token) { return line.find(token) != std::string::npos; };
		if (has("[critical]")) {
			return "\x1b[1;97;41m";
		}
		if (has("[err]") || has("[error]")) {
			return "\x1b[91m";
		}
		if (has("[warn]") || has("[warning]")) {
			return "\x1b[38;5;214m";
		}
		if (has("[info]")) {
			return "\x1b[92m";
		}
		if (has("[debug]")) {
			return "\x1b[96m";
		}
		if (has("[trace]")) {
			return "\x1b[90m";
		}
		return "\x1b[37m";
	}

	void WriteLine(const std::string& line)
	{
		if (line.empty()) {
			return;
		}
		std::string plain = line;
		StripAnsiEscapes(plain);
		const std::string payload = std::string(AnsiColorForLine(plain)) + plain + "\x1b[0m\r\n";
		std::fwrite(payload.data(), 1, payload.size(), stdout);
		std::fflush(stdout);
	}

	void EmitPendingLines(std::string& pending)
	{
		for (;;) {
			const size_t nl = pending.find('\n');
			if (nl == std::string::npos) {
				return;
			}
			std::string line = pending.substr(0, nl);
			TrimCr(line);
			if (!line.empty()) {
				WriteLine(line);
			}
			pending.erase(0, nl + 1);
		}
	}

	void NotifyParentConsoleClosed(DWORD parent_pid)
	{
		if (parent_pid == 0) {
			return;
		}
		HWND hwnd = ::FindWindowW(kMainWindowClass, nullptr);
		if (hwnd != nullptr) {
			DWORD window_pid = 0;
			::GetWindowThreadProcessId(hwnd, &window_pid);
			if (window_pid == parent_pid) {
				::PostMessageW(hwnd, WM_HP_CONSOLE_CLOSED, 0, 0);
			}
		}
	}

	void PipeReaderThreadMain(PipeReaderState* state)
	{
		char buffer[4096];
		while (state->running.load()) {
			DWORD read = 0;
			const BOOL ok = ::ReadFile(state->pipe, buffer, sizeof(buffer), &read, nullptr);
			if (!ok || read == 0) {
				state->running.store(false);
				break;
			}
			std::lock_guard<std::mutex> lock(state->pending_mutex);
			state->pending.append(buffer, buffer + read);
		}
	}

	void DrainPending(PipeReaderState& state)
	{
		std::string chunk;
		{
			std::lock_guard<std::mutex> lock(state.pending_mutex);
			chunk.swap(state.pending);
		}
		if (chunk.empty()) {
			return;
		}
		EmitPendingLines(chunk);
		std::lock_guard<std::mutex> lock(state.pending_mutex);
		state.pending = std::move(chunk);
	}

	void StreamLogFromHandle(HANDLE read_pipe, DWORD parent_pid)
	{
		if (read_pipe == nullptr || read_pipe == INVALID_HANDLE_VALUE) {
			if (EnsureAttachedConsole()) {
				WriteConsoleText(W18N(u8"無效的日誌管道句柄。\r\n"));
			}
			return;
		}

		PipeReaderState reader;
		reader.pipe = read_pipe;
		reader.thread = std::thread(PipeReaderThreadMain, &reader);

		g_console_running = true;
		g_parent_notified.store(false);
		g_session_parent_pid = parent_pid;
		g_session_reader = &reader;

		if (!EnsureAttachedConsole()) {
			RequestConsoleShutdown();
			if (reader.thread.joinable()) {
				reader.thread.join();
			}
			::SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
			g_session_reader = nullptr;
			g_session_parent_pid = 0;
			return;
		}
		::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

		std::fputs("\x1b[1;36mHP CLEANER++ \x1b[1;97m\u65e5\u5fd7\u63a7\u5236\u53f0\x1b[0m\r\n", stdout);
		std::fputs("\x1b[90m\u5df2\u9023\u7dda \u2014 \x1b[32m\u5373\u6642\u4e32\u6d41\uff08Pipe\uff09\x1b[0m\r\n\r\n", stdout);
		std::fflush(stdout);

		while (g_console_running && reader.running.load()) {
			DrainPending(reader);
			::Sleep(16);
		}

		reader.running.store(false);
		if (reader.thread.joinable()) {
			reader.thread.join();
		}
		DrainPending(reader);

		if (reader.pipe != INVALID_HANDLE_VALUE) {
			::CloseHandle(reader.pipe);
			reader.pipe = INVALID_HANDLE_VALUE;
		}

		::SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
		NotifyParentConsoleClosedOnce(parent_pid);
		g_session_reader = nullptr;
		g_session_parent_pid = 0;
	}

	void ChildExitWatcherMain()
	{
		const HANDLE process = g_child_process;
		if (process == nullptr) {
			return;
		}
		::WaitForSingleObject(process, INFINITE);
		if (g_parent_initiated_shutdown.load(std::memory_order_acquire)) {
			return;
		}
		HWND hwnd = ::FindWindowW(kMainWindowClass, nullptr);
		if (hwnd != nullptr) {
			::PostMessageW(hwnd, WM_HP_CONSOLE_CLOSED, 0, 0);
		}
	}

	void StartChildExitWatcher()
	{
		if (g_child_watcher.joinable()) {
			g_child_watcher.join();
		}
		g_parent_initiated_shutdown.store(false, std::memory_order_release);
		g_child_watcher = std::thread(ChildExitWatcherMain);
	}

	void TerminateChildProcessIfNeeded()
	{
		if (g_child_process == nullptr) {
			return;
		}
		if (::WaitForSingleObject(g_child_process, 0) == WAIT_TIMEOUT) {
			::TerminateProcess(g_child_process, 0);
		}
		::CloseHandle(g_child_process);
		g_child_process = nullptr;
	}
}

void HLogCmdConsoleLaunch()
{
	TerminateChildProcessIfNeeded();
	HLogPipeClearWriteHandle();

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_pipe = INVALID_HANDLE_VALUE;
	HANDLE write_pipe = INVALID_HANDLE_VALUE;
	if (!::CreatePipe(&read_pipe, &write_pipe, &sa, 256 * 1024)) {
		HLOG_WARN("HLogCmdConsole: CreatePipe failed err={}", ::GetLastError());
		return;
	}
	::SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0);

	wchar_t exe_path[MAX_PATH] = {};
	if (::GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
		::CloseHandle(read_pipe);
		::CloseHandle(write_pipe);
		return;
	}

	const DWORD parent_pid = ::GetCurrentProcessId();
	const uintptr_t read_handle_val = reinterpret_cast<uintptr_t>(read_pipe);
	wchar_t cmd_line[1024] = {};
	_snwprintf_s(cmd_line, _TRUNCATE,
		L"\"%s\" --mode=logconsole --log-read-handle=%llu --parent-pid=%lu",
		exe_path, static_cast<unsigned long long>(read_handle_val), parent_pid);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!::CreateProcessW(exe_path, cmd_line, nullptr, nullptr, TRUE, CREATE_NEW_CONSOLE,
		nullptr, nullptr, &si, &pi)) {
		HLOG_WARN("HLogCmdConsole: CreateProcess failed err={}", ::GetLastError());
		::CloseHandle(read_pipe);
		::CloseHandle(write_pipe);
		return;
	}

	::CloseHandle(pi.hThread);
	g_child_process = pi.hProcess;
	::CloseHandle(read_pipe);

	if (!HLogPipeSetWriteHandle(write_pipe)) {
		::CloseHandle(write_pipe);
		TerminateChildProcessIfNeeded();
		return;
	}

	HLoggingRebuildSinks();
	HLogPipeFlushHistory();
	StartChildExitWatcher();
	HLOG_INFO("HLogCmdConsole: launched pipe log console");
}

void HLogCmdConsoleShutdown()
{
	g_parent_initiated_shutdown.store(true, std::memory_order_release);
	HLogPipeClearWriteHandle();
	HLoggingRebuildSinks();
	if (g_child_process != nullptr) {
		if (::WaitForSingleObject(g_child_process, 2000) == WAIT_TIMEOUT) {
			::TerminateProcess(g_child_process, 0);
			::WaitForSingleObject(g_child_process, 1000);
		}
		::CloseHandle(g_child_process);
		g_child_process = nullptr;
	}
	if (g_child_watcher.joinable()) {
		g_child_watcher.join();
	}
}

int RunLogConsoleApplication(const std::string& /*log_path*/, DWORD parent_pid, uintptr_t read_handle_val)
{
	Hi18n::Init();
	if (read_handle_val == 0) {
		if (EnsureAttachedConsole()) {
			WriteConsoleText(W18N(u8"缺少 log-read-handle 參數。\r\n"));
			::Sleep(3000);
		}
		return 1;
	}

	g_console_running = true;
	const HANDLE read_pipe = reinterpret_cast<HANDLE>(read_handle_val);
	StreamLogFromHandle(read_pipe, parent_pid);
	return 0;
}
