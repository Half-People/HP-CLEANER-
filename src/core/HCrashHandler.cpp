#include "HCrashHandler.h"
#include "Hi18n.h"
#include "HAppPaths.h"
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <signal.h>
#include <sstream>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace {
	volatile LONG g_crash_handling = 0;
	volatile LONG g_vectored_stamp_written = 0;
	volatile LONG g_shutting_down = 0;
	wchar_t g_crashes_dir_cached[MAX_PATH * 4] = {};
	DWORD g_watchdog_child_pid = 0;
	bool g_watchdog_spawn_ok = false;

	bool EnsureDirectoryTreeWide(const wchar_t* path);

	bool CopyCachedCrashesDir(wchar_t* out, size_t out_chars)
	{
		if (out == nullptr || out_chars == 0) {
			return false;
		}
		out[0] = L'\0';
		if (g_crashes_dir_cached[0] == L'\0') {
			return false;
		}
		wcsncpy_s(out, out_chars, g_crashes_dir_cached, _TRUNCATE);
		return true;
	}

	bool GetCrashesDirForExceptionContext(wchar_t* out, size_t out_chars)
	{
		if (CopyCachedCrashesDir(out, out_chars)) {
			return true;
		}

		wchar_t appdata[MAX_PATH] = {};
		const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) {
			return false;
		}
		_snwprintf_s(out, out_chars, _TRUNCATE, L"%s\\HalfPeople\\HP CLEANER++\\crashes\\", appdata);
		return out[0] != L'\0';
	}

	void WriteHandlerLog(const char* msg)
	{
		if (msg == nullptr) {
			return;
		}

		wchar_t crashes_dir[MAX_PATH * 4] = {};
		if (!CopyCachedCrashesDir(crashes_dir, sizeof(crashes_dir) / sizeof(crashes_dir[0]))) {
			return;
		}

		wchar_t log_path[MAX_PATH * 4] = {};
		_snwprintf_s(log_path, _TRUNCATE, L"%shandler.log", crashes_dir);

		HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}

		DWORD written = 0;
		WriteFile(file, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
		const char nl[] = "\r\n";
		WriteFile(file, nl, 2, &written, nullptr);
		CloseHandle(file);
		OutputDebugStringA(msg);
	}

	bool EnsureDirectoryTreeWide(const wchar_t* path)
	{
		if (path == nullptr || path[0] == L'\0') {
			return false;
		}

		wchar_t norm[MAX_PATH * 4] = {};
		wcsncpy_s(norm, path, _TRUNCATE);
		size_t len = wcslen(norm);
		while (len > 0 && norm[len - 1] == L'\\') {
			norm[--len] = L'\0';
		}
		if (len == 0) {
			return false;
		}

		const DWORD attr = GetFileAttributesW(norm);
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
			return true;
		}

		for (wchar_t* cursor = norm + 3; *cursor != L'\0'; ++cursor) {
			if (*cursor != L'\\') {
				continue;
			}
			*cursor = L'\0';
			if (GetFileAttributesW(norm) == INVALID_FILE_ATTRIBUTES) {
				CreateDirectoryW(norm, nullptr);
			}
			*cursor = L'\\';
		}
		if (GetFileAttributesW(norm) == INVALID_FILE_ATTRIBUTES) {
			CreateDirectoryW(norm, nullptr);
		}
		return GetFileAttributesW(norm) != INVALID_FILE_ATTRIBUTES;
	}

	void InitCrashesDirCache()
	{
		if (g_crashes_dir_cached[0] != L'\0') {
			return;
		}

		wchar_t appdata[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata))) {
			const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) {
				return;
			}
		}

		_snwprintf_s(g_crashes_dir_cached, _TRUNCATE, L"%s\\HalfPeople\\HP CLEANER++\\crashes\\", appdata);
		EnsureDirectoryTreeWide(g_crashes_dir_cached);
	}

	bool GetCrashesDirWide(wchar_t* out, size_t out_chars)
	{
		if (out == nullptr || out_chars == 0) {
			return false;
		}
		InitCrashesDirCache();
		return CopyCachedCrashesDir(out, out_chars);
	}

	void GetLogFilePathWide(wchar_t* out, size_t out_chars)
	{
		if (out == nullptr || out_chars == 0) {
			return;
		}
		out[0] = L'\0';
		const std::string latest = HAppPaths::GetLatestLogFilePath();
		if (latest.empty()) {
			return;
		}
		MultiByteToWideChar(CP_UTF8, 0, latest.c_str(), -1, out, static_cast<int>(out_chars));
	}

	void* CaptureTopExceptionAddress()
	{
		void* frames[32] = {};
		const USHORT depth = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
		for (USHORT i = 0; i < depth; ++i) {
			if (frames[i] != nullptr) {
				return frames[i];
			}
		}
		return nullptr;
	}

	const char* ReportSourceForCode(DWORD exception_code)
	{
		switch (exception_code) {
		case 0xE0000001u:
			return "cpp_terminate";
		case 0xE0000002u:
			return "invalid_parameter";
		case 0xE0000003u:
			return "purecall";
		case 0xE0000004u:
			return "sigabrt";
		default:
			return "unhandled_filter";
		}
	}

	void GetExePathAndWorkDir(wchar_t* exe_path, size_t exe_chars, wchar_t* work_dir, size_t work_chars)
	{
		if (exe_path != nullptr && exe_chars > 0) {
			exe_path[0] = L'\0';
			GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(exe_chars));
		}
		if (work_dir != nullptr && work_chars > 0 && exe_path != nullptr) {
			wcsncpy_s(work_dir, work_chars, exe_path, _TRUNCATE);
			wchar_t* last_slash = wcsrchr(work_dir, L'\\');
			if (last_slash != nullptr) {
				*last_slash = L'\0';
			}
		}
	}

	void WritePendingReportPath(const wchar_t* report_path)
	{
		if (report_path == nullptr || report_path[0] == L'\0') {
			return;
		}

		wchar_t pending[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(pending, sizeof(pending) / sizeof(pending[0]))) {
			return;
		}
		wcscat_s(pending, L"pending_report.txt");

		HANDLE file = CreateFileW(pending, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}

		const int bytes = WideCharToMultiByte(CP_UTF8, 0, report_path, -1, nullptr, 0, nullptr, nullptr);
		if (bytes > 1) {
			char* utf8 = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(bytes)));
			if (utf8 != nullptr) {
				WideCharToMultiByte(CP_UTF8, 0, report_path, -1, utf8, bytes, nullptr, nullptr);
				DWORD written = 0;
				WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
				FlushFileBuffers(file);
				HeapFree(GetProcessHeap(), 0, utf8);
			}
		}
		CloseHandle(file);
	}

	bool ReadPendingReportPathWide(wchar_t* out, size_t out_chars)
	{
		if (out == nullptr || out_chars == 0) {
			return false;
		}
		out[0] = L'\0';

		wchar_t pending[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(pending, sizeof(pending) / sizeof(pending[0]))) {
			return false;
		}
		wcscat_s(pending, L"pending_report.txt");

		HANDLE file = CreateFileW(pending, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		char buffer[MAX_PATH * 4] = {};
		DWORD read = 0;
		const BOOL ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
		CloseHandle(file);
		if (ok != TRUE || read == 0) {
			return false;
		}
		buffer[read] = '\0';

		char* start = buffer;
		while (*start == ' ' || *start == '"' || *start == '\r' || *start == '\n') {
			++start;
		}
		char* end = start + strlen(start);
		while (end > start && (end[-1] == ' ' || end[-1] == '"' || end[-1] == '\r' || end[-1] == '\n')) {
			--end;
		}
		*end = '\0';

		return MultiByteToWideChar(CP_UTF8, 0, start, -1, out, static_cast<int>(out_chars)) > 0;
	}

	void WriteReportUiLock(DWORD pid)
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		wcscat_s(path, L"report_ui.lock");

		wchar_t buf[32] = {};
		_snwprintf_s(buf, _TRUNCATE, L"%lu", static_cast<unsigned long>(pid));

		HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		const int bytes = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
		if (bytes > 1) {
			char utf8[32] = {};
			WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
			DWORD written = 0;
			WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
		}
		CloseHandle(file);
	}

	bool ReadReportUiLockPid(DWORD* out_pid)
	{
		if (out_pid == nullptr) {
			return false;
		}
		*out_pid = 0;

		wchar_t path[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(path, sizeof(path) / sizeof(path[0]))) {
			return false;
		}
		wcscat_s(path, L"report_ui.lock");

		HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		char buf[32] = {};
		DWORD read = 0;
		const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr);
		CloseHandle(file);
		if (ok != TRUE || read == 0) {
			return false;
		}
		buf[read] = '\0';
		*out_pid = static_cast<DWORD>(strtoul(buf, nullptr, 10));
		return *out_pid != 0;
	}

	void ClearReportUiLock()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		wcscat_s(path, L"report_ui.lock");
		DeleteFileW(path);
	}

	bool IsProcessAlive(DWORD pid)
	{
		if (pid == 0) {
			return false;
		}
		HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (proc == nullptr) {
			return false;
		}
		DWORD code = 0;
		const BOOL ok = GetExitCodeProcess(proc, &code);
		CloseHandle(proc);
		return ok == TRUE && code == STILL_ACTIVE;
	}

	std::string JsonEscapeUtf8(const char* text)
	{
		if (text == nullptr) {
			return {};
		}
		std::string out;
		out.reserve(strlen(text) + 16);
		for (const char* p = text; *p != '\0'; ++p) {
			const unsigned char c = static_cast<unsigned char>(*p);
			if (c == '"') {
				out += "\\\"";
			}
			else if (c == '\\') {
				out += "\\\\";
			}
			else if (c == '\n') {
				out += "\\n";
			}
			else if (c == '\r') {
				continue;
			}
			else if (c < 0x20) {
				char hex[8] = {};
				snprintf(hex, sizeof(hex), "\\u%04X", c);
				out += hex;
			}
			else {
				out += static_cast<char>(c);
			}
		}
		return out;
	}

	void ResolveModuleName(void* address, char* out_module, size_t out_module_size,
		char* out_offset, size_t out_offset_size);

	void EnsureDbgHelpSymbols()
	{
		static volatile LONG initialized = 0;
		if (InterlockedCompareExchange(&initialized, 1, 0) != 0) {
			return;
		}
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		SymInitialize(GetCurrentProcess(), nullptr, TRUE);
	}

	void ResolveSymbolName(void* address, char* out_symbol, size_t out_symbol_size)
	{
		if (out_symbol == nullptr || out_symbol_size == 0) {
			return;
		}
		out_symbol[0] = '\0';
		if (address == nullptr) {
			return;
		}
		EnsureDbgHelpSymbols();
		alignas(SYMBOL_INFO) unsigned char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
		auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;
		DWORD64 disp = 0;
		if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(address), &disp, sym)) {
			snprintf(out_symbol, out_symbol_size, "%s+0x%llX",
				sym->Name, static_cast<unsigned long long>(disp));
		}
	}

	const char* DescribeExceptionCode(DWORD exception_code)
	{
		switch (exception_code) {
		case 0xC0000005u:
			return "存取違規 (ACCESS_VIOLATION)";
		case 0xC00000FDu:
			return "堆疊溢出 (STACK_OVERFLOW)";
		case 0xE0000001u:
			return "std::terminate（未捕獲 C++ 例外）";
		case 0xE0000002u:
			return "CRT invalid parameter";
		case 0xE0000003u:
			return "purecall";
		case 0xE0000004u:
			return "SIGABRT / abort()（堆積損壞、assert 或 CRT 中止）";
		default:
			return "系統或執行期例外";
		}
	}

	std::string CaptureStackTraceText(int skip_frames, int max_frames)
	{
		void* frames[32] = {};
		const USHORT depth = RtlCaptureStackBackTrace(static_cast<ULONG>(skip_frames),
			static_cast<ULONG>(max_frames), frames, nullptr);
		std::ostringstream ss;
		for (USHORT i = 0; i < depth; ++i) {
			char module_name[128] = {};
			char module_offset[32] = {};
			char symbol_name[256] = {};
			ResolveModuleName(frames[i], module_name, sizeof(module_name),
				module_offset, sizeof(module_offset));
			ResolveSymbolName(frames[i], symbol_name, sizeof(symbol_name));
			ss << '#' << i << ' ';
			if (symbol_name[0] != '\0') {
				ss << symbol_name;
			}
			else if (module_name[0] != '\0') {
				ss << module_name;
				if (module_offset[0] != '\0') {
					ss << '+' << module_offset;
				}
			}
			else {
				ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(frames[i]);
			}
			ss << '\n';
		}
		return ss.str();
	}

	std::string BuildOsVersionText()
	{
		struct RtlOsVersionInfo {
			ULONG dwOSVersionInfoSize;
			ULONG dwMajorVersion;
			ULONG dwMinorVersion;
			ULONG dwBuildNumber;
			ULONG dwPlatformId;
			wchar_t szCSDVersion[128];
		} ver = {};
		ver.dwOSVersionInfoSize = sizeof(ver);
		char text[128] = {};
		using RtlGetVersionFn = LONG(WINAPI*)(void*);
		const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
		if (rtl_get_version != nullptr && rtl_get_version(&ver) == 0) {
			snprintf(text, sizeof(text), "Windows %lu.%lu build %lu",
				ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber);
		}
		else {
			strncpy_s(text, "Windows (未知版本)", _TRUNCATE);
		}
		return text;
	}

	std::string BuildCrashReportJsonBody(DWORD exception_code, void* address,
		const wchar_t* dump_path, const wchar_t* log_path, const char* report_source)
	{
		void* resolved_address = address;
		if (resolved_address == nullptr) {
			resolved_address = CaptureTopExceptionAddress();
		}

		char module_name[128] = {};
		char module_offset[32] = {};
		ResolveModuleName(resolved_address, module_name, sizeof(module_name),
			module_offset, sizeof(module_offset));

		char dump_utf8[MAX_PATH * 4] = {};
		char log_utf8[MAX_PATH * 4] = {};
		if (dump_path != nullptr) {
			WideCharToMultiByte(CP_UTF8, 0, dump_path, -1, dump_utf8, sizeof(dump_utf8), nullptr, nullptr);
		}
		if (log_path != nullptr && log_path[0] != L'\0') {
			WideCharToMultiByte(CP_UTF8, 0, log_path, -1, log_utf8, sizeof(log_utf8), nullptr, nullptr);
		}
		if (log_utf8[0] == '\0') {
			wchar_t wlog[MAX_PATH * 4] = {};
			GetLogFilePathWide(wlog, sizeof(wlog) / sizeof(wlog[0]));
			if (wlog[0] != L'\0') {
				WideCharToMultiByte(CP_UTF8, 0, wlog, -1, log_utf8, sizeof(log_utf8), nullptr, nullptr);
			}
			else {
				const std::string latest = HAppPaths::GetLatestLogFilePath();
				if (!latest.empty()) {
					strncpy_s(log_utf8, latest.c_str(), _TRUNCATE);
				}
			}
		}

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		char timestamp[64] = {};
		snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		const uintptr_t addr_val = reinterpret_cast<uintptr_t>(resolved_address);
		char addr_text[32] = {};
		if (resolved_address != nullptr) {
			snprintf(addr_text, sizeof(addr_text), "0x%llX", static_cast<unsigned long long>(addr_val));
		}

		const char* source = report_source != nullptr ? report_source : ReportSourceForCode(exception_code);
		const std::string stack = CaptureStackTraceText(2, 16);
		const std::string os_text = BuildOsVersionText();

		std::ostringstream json;
		json << "{\n";
		json << "  \"timestamp\": \"" << timestamp << "\",\n";
		json << "  \"exception_code\": \"0x" << std::hex << std::uppercase << exception_code << std::dec << "\",\n";
		json << "  \"exception_description\": \"" << JsonEscapeUtf8(DescribeExceptionCode(exception_code)) << "\",\n";
		json << "  \"exception_address\": \"" << addr_text << "\",\n";
		json << "  \"module\": \"" << JsonEscapeUtf8(module_name) << "\",\n";
		json << "  \"module_offset\": \"" << JsonEscapeUtf8(module_offset) << "\",\n";
		json << "  \"process_id\": " << GetCurrentProcessId() << ",\n";
		json << "  \"thread_id\": " << GetCurrentThreadId() << ",\n";
		json << "  \"os_version\": \"" << JsonEscapeUtf8(os_text.c_str()) << "\",\n";
		json << "  \"dump_file\": \"" << JsonEscapeUtf8(dump_utf8) << "\",\n";
		json << "  \"log_file\": \"" << JsonEscapeUtf8(log_utf8) << "\",\n";
		json << "  \"handler_log\": \"" << JsonEscapeUtf8("crashes/handler.log") << "\",\n";
		json << "  \"report_source\": \"" << JsonEscapeUtf8(source) << "\",\n";
		json << "  \"stack_trace\": \"" << JsonEscapeUtf8(stack.c_str()) << "\"\n";
		json << "}\n";
		return json.str();
	}

	void ResolveModuleName(void* address, char* out_module, size_t out_module_size, char* out_offset, size_t out_offset_size)
	{
		if (out_module != nullptr && out_module_size > 0) {
			out_module[0] = '\0';
		}
		if (out_offset != nullptr && out_offset_size > 0) {
			out_offset[0] = '\0';
		}
		if (address == nullptr) {
			return;
		}

		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCSTR>(address), &module) || module == nullptr) {
			return;
		}

		char module_path[MAX_PATH] = {};
		if (GetModuleFileNameA(module, module_path, MAX_PATH) == 0) {
			return;
		}

		const char* slash = std::strrchr(module_path, '\\');
		const char* module_name = slash != nullptr ? slash + 1 : module_path;
		if (out_module != nullptr && out_module_size > 0) {
			strncpy_s(out_module, out_module_size, module_name, _TRUNCATE);
		}

		const uintptr_t base = reinterpret_cast<uintptr_t>(module);
		const uintptr_t addr = reinterpret_cast<uintptr_t>(address);
		if (out_offset != nullptr && out_offset_size > 0) {
			snprintf(out_offset, out_offset_size, "0x%llX", static_cast<unsigned long long>(addr - base));
		}
	}

	bool WriteMiniDump(const wchar_t* dump_path, EXCEPTION_POINTERS* ep)
	{
		if (dump_path == nullptr) {
			return false;
		}

		HANDLE file = CreateFileW(dump_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		BOOL ok = FALSE;
		if (ep != nullptr && ep->ExceptionRecord != nullptr) {
			MINIDUMP_EXCEPTION_INFORMATION mei = {};
			mei.ThreadId = GetCurrentThreadId();
			mei.ExceptionPointers = ep;
			mei.ClientPointers = FALSE;
			ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
				static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory),
				&mei, nullptr, nullptr);
		}
		else {
			ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
				MiniDumpNormal, nullptr, nullptr, nullptr);
		}

		CloseHandle(file);
		return ok == TRUE;
	}

	bool InferStackAddressAllowed(const char* report_source)
	{
		return report_source == nullptr
			|| (strcmp(report_source, "watchdog_inferred") != 0
				&& strcmp(report_source, "watchdog_inferred_session") != 0);
	}

	bool WriteCrashReportJson(const wchar_t* report_path, DWORD exception_code, void* address,
		const wchar_t* dump_path, const wchar_t* log_path, const char* report_source)
	{
		if (report_path == nullptr) {
			return false;
		}

		void* resolved_address = address;
		if (resolved_address == nullptr && InferStackAddressAllowed(report_source)) {
			resolved_address = CaptureTopExceptionAddress();
		}

		const char* source = report_source != nullptr ? report_source : ReportSourceForCode(exception_code);
		const std::string json = BuildCrashReportJsonBody(exception_code, resolved_address,
			dump_path, log_path, source);

		HANDLE file = CreateFileW(report_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		DWORD written = 0;
		const BOOL ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
		CloseHandle(file);
		return ok == TRUE;
	}

	constexpr DWORD kDetachedChildFlags =
		CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;

	bool SpawnDetachedViaCmdStart(const wchar_t* exe_path, const wchar_t* args, const wchar_t* work_dir)
	{
		if (exe_path == nullptr || exe_path[0] == L'\0' || args == nullptr) {
			return false;
		}

		wchar_t inner[MAX_PATH * 2 + 4096] = {};
		_snwprintf_s(inner, _TRUNCATE, L"\"%s\" %s", exe_path, args);

		wchar_t cmd[5120] = {};
		if (work_dir != nullptr && work_dir[0] != L'\0') {
			_snwprintf_s(cmd, _TRUNCATE, L"cmd.exe /c start \"\" /D \"%s\" %s", work_dir, inner);
		}
		else {
			_snwprintf_s(cmd, _TRUNCATE, L"cmd.exe /c start \"\" %s", inner);
		}

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};

		const BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW | kDetachedChildFlags, nullptr,
			work_dir != nullptr && work_dir[0] != L'\0' ? work_dir : nullptr, &si, &pi);
		if (ok) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			WriteHandlerLog("SpawnDetachedViaCmdStart OK");
			return true;
		}

		char log_buf[128] = {};
		snprintf(log_buf, sizeof(log_buf), "SpawnDetachedViaCmdStart failed: %lu", GetLastError());
		WriteHandlerLog(log_buf);
		return false;
	}

	bool RelaunchCrashReportMode(const wchar_t* report_path)
	{
		if (report_path == nullptr || report_path[0] == L'\0') {
			return false;
		}

		DWORD existing_pid = 0;
		if (ReadReportUiLockPid(&existing_pid) && IsProcessAlive(existing_pid)) {
			WriteHandlerLog("Relaunch skipped: crash-report UI already running.");
			return true;
		}

		wchar_t exe_path[MAX_PATH] = {};
		wchar_t work_dir[MAX_PATH] = {};
		GetExePathAndWorkDir(exe_path, sizeof(exe_path) / sizeof(exe_path[0]),
			work_dir, sizeof(work_dir) / sizeof(work_dir[0]));

		wchar_t params[4096] = {};
		_snwprintf_s(params, _TRUNCATE, L"--mode=crash-report --report=\"%s\"", report_path);

		wchar_t cmd_line[4096] = {};
		_snwprintf_s(cmd_line, _TRUNCATE, L"\"%s\" %s", exe_path, params);

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWNORMAL;
		PROCESS_INFORMATION pi = {};

		const BOOL cp_ok = CreateProcessW(exe_path, cmd_line, nullptr, nullptr, FALSE,
			kDetachedChildFlags, nullptr, work_dir[0] != L'\0' ? work_dir : nullptr, &si, &pi);
		if (cp_ok) {
			WriteReportUiLock(pi.dwProcessId);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			WriteHandlerLog("Relaunch OK: CreateProcessW (detached)");
			return true;
		}

		char log_buf[128] = {};
		snprintf(log_buf, sizeof(log_buf), "CreateProcessW failed: %lu", GetLastError());
		WriteHandlerLog(log_buf);

		SHELLEXECUTEINFOW sei = {};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
		sei.lpVerb = L"open";
		sei.lpFile = exe_path;
		sei.lpParameters = params;
		sei.lpDirectory = work_dir[0] != L'\0' ? work_dir : nullptr;
		sei.nShow = SW_SHOWNORMAL;

		if (ShellExecuteExW(&sei)) {
			if (sei.hProcess != nullptr) {
				const DWORD child_pid = GetProcessId(sei.hProcess);
				if (child_pid != 0) {
					WriteReportUiLock(child_pid);
				}
				CloseHandle(sei.hProcess);
			}
			WriteHandlerLog("Relaunch OK: ShellExecuteEx");
			return true;
		}

		snprintf(log_buf, sizeof(log_buf), "ShellExecuteEx failed: %lu", GetLastError());
		WriteHandlerLog(log_buf);

		if (SpawnDetachedViaCmdStart(exe_path, params, work_dir)) {
			WriteHandlerLog("Relaunch OK: cmd start (fallback)");
			return true;
		}

		return false;
	}

	void BuildCrashFilePaths(wchar_t* report_path, size_t report_chars, wchar_t* dump_path, size_t dump_chars)
	{
		wchar_t crashes_dir[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(crashes_dir, sizeof(crashes_dir) / sizeof(crashes_dir[0]))) {
			if (report_path != nullptr && report_chars > 0) {
				report_path[0] = L'\0';
			}
			if (dump_path != nullptr && dump_chars > 0) {
				dump_path[0] = L'\0';
			}
			return;
		}

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t stamp[64] = {};
		_snwprintf_s(stamp, _TRUNCATE, L"%04u%02u%02u_%02u%02u%02u",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		if (report_path != nullptr && report_chars > 0) {
			_snwprintf_s(report_path, report_chars, _TRUNCATE, L"%scrash_%s.json", crashes_dir, stamp);
		}
		if (dump_path != nullptr && dump_chars > 0) {
			_snwprintf_s(dump_path, dump_chars, _TRUNCATE, L"%scrash_%s.dmp", crashes_dir, stamp);
		}
	}

	bool FileTimeWithinSeconds(const FILETIME& ft, DWORD max_age_seconds)
	{
		FILETIME now_ft = {};
		GetSystemTimeAsFileTime(&now_ft);
		ULARGE_INTEGER now;
		now.LowPart = now_ft.dwLowDateTime;
		now.HighPart = now_ft.dwHighDateTime;
		ULARGE_INTEGER file_time;
		file_time.LowPart = ft.dwLowDateTime;
		file_time.HighPart = ft.dwHighDateTime;
		if (now.QuadPart <= file_time.QuadPart) {
			return true;
		}
		const ULONGLONG age_100ns = now.QuadPart - file_time.QuadPart;
		return age_100ns <= static_cast<ULONGLONG>(max_age_seconds) * 10000000ULL;
	}

	bool FindLatestCrashReportJsonWide(wchar_t* out, size_t out_chars, DWORD max_age_seconds)
	{
		if (out == nullptr || out_chars == 0) {
			return false;
		}
		out[0] = L'\0';

		wchar_t dir[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(dir, sizeof(dir) / sizeof(dir[0]))) {
			return false;
		}

		wchar_t pattern[MAX_PATH * 4] = {};
		_snwprintf_s(pattern, _TRUNCATE, L"%scrash_*.json", dir);

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern, &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return false;
		}

		ULONGLONG best_time = 0;
		wchar_t best_path[MAX_PATH * 4] = {};
		bool found = false;

		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			}
			if (!FileTimeWithinSeconds(fd.ftLastWriteTime, max_age_seconds)) {
				continue;
			}

			ULARGE_INTEGER t;
			t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
			t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
			if (!found || t.QuadPart > best_time) {
				best_time = t.QuadPart;
				_snwprintf_s(best_path, _TRUNCATE, L"%s%s", dir, fd.cFileName);
				found = true;
			}
		} while (FindNextFileW(find, &fd));

		FindClose(find);
		if (!found) {
			return false;
		}

		wcsncpy_s(out, out_chars, best_path, _TRUNCATE);
		return true;
	}

	bool TryReuseRecentEmergencyReport(wchar_t* out_report_path, size_t out_chars)
	{
		if (out_report_path == nullptr || out_chars == 0) {
			return false;
		}
		out_report_path[0] = L'\0';

		wchar_t dir[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(dir, sizeof(dir) / sizeof(dir[0]))) {
			return false;
		}

		wchar_t pattern[MAX_PATH * 4] = {};
		_snwprintf_s(pattern, _TRUNCATE, L"%scrash_*.json", dir);

		WIN32_FIND_DATAW fd = {};
		const HANDLE find = FindFirstFileW(pattern, &fd);
		if (find == INVALID_HANDLE_VALUE) {
			return false;
		}

		ULONGLONG best_time = 0;
		wchar_t best_path[MAX_PATH * 4] = {};
		bool found = false;

		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			}
			if (!FileTimeWithinSeconds(fd.ftLastWriteTime, 120)) {
				continue;
			}

			wchar_t candidate[MAX_PATH * 4] = {};
			_snwprintf_s(candidate, _TRUNCATE, L"%s%s", dir, fd.cFileName);

			HANDLE file = CreateFileW(candidate, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				continue;
			}

			char buffer[2048] = {};
			DWORD read = 0;
			const BOOL ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
			CloseHandle(file);
			if (!ok || read == 0) {
				continue;
			}
			buffer[read] = '\0';
			if (strstr(buffer, "\"report_source\": \"watchdog_inferred\"") != nullptr
				|| strstr(buffer, "\"report_source\": \"watchdog_inferred_session\"") != nullptr) {
				continue;
			}
			if (strstr(buffer, "\"report_source\": \"vectored_handler\"") == nullptr
				&& strstr(buffer, "\"report_source\": \"unhandled_filter\"") == nullptr
				&& strstr(buffer, "\"report_source\": \"sigabrt\"") == nullptr) {
				continue;
			}

			ULARGE_INTEGER t;
			t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
			t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
			if (!found || t.QuadPart > best_time) {
				best_time = t.QuadPart;
				wcsncpy_s(best_path, candidate, _TRUNCATE);
				found = true;
			}
		} while (FindNextFileW(find, &fd));

		FindClose(find);
		if (!found) {
			return false;
		}

		wcsncpy_s(out_report_path, out_chars, best_path, _TRUNCATE);
		return true;
	}

	bool WriteMinimalCrashReport(DWORD exit_code, wchar_t* out_report_path, size_t out_chars, const char* report_source)
	{
		if (out_report_path == nullptr || out_chars == 0) {
			return false;
		}
		out_report_path[0] = L'\0';

		if (TryReuseRecentEmergencyReport(out_report_path, out_chars)) {
			WriteHandlerLog("Watchdog: reusing emergency crash json");
			return true;
		}

		wchar_t crashes_dir[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(crashes_dir, sizeof(crashes_dir) / sizeof(crashes_dir[0]))) {
			return false;
		}

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t stamp[64] = {};
		_snwprintf_s(stamp, _TRUNCATE, L"%04u%02u%02u_%02u%02u%02u",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		_snwprintf_s(out_report_path, out_chars, _TRUNCATE, L"%scrash_%s.json", crashes_dir, stamp);

		wchar_t log_path[MAX_PATH * 4] = {};
		GetLogFilePathWide(log_path, sizeof(log_path) / sizeof(log_path[0]));

		const char* source = report_source != nullptr ? report_source : "watchdog_inferred";
		return WriteCrashReportJson(out_report_path, exit_code, nullptr, nullptr, log_path, source);
	}

	void ShowReportLaunchFailureMessage(const wchar_t* report_path)
	{
		wchar_t msg[768] = {};
		if (report_path != nullptr && report_path[0] != L'\0') {
			const std::wstring fmt = Hi18n::TrZhWide(
				u8"HP CLEANER++ 已異常結束，但無法開啟報告視窗。\n\n報告檔：\n%s\n\n"
				u8"請手動重新執行程式，或至 crashes 資料夾查看。");
			_snwprintf_s(msg, _TRUNCATE, fmt.c_str(), report_path);
		}
		else {
			wcscpy_s(msg, W18N(u8"HP CLEANER++ 已異常結束，但無法開啟報告視窗。\n"
				u8"請至 %APPDATA%\\HalfPeople\\HP CLEANER++\\crashes\\ 查看。"));
		}
		MessageBoxW(nullptr, msg, W18N(u8"HP CLEANER++"), MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
	}

	bool GetGracefulExitFlagPath(wchar_t* out, size_t out_chars)
	{
		if (!GetCrashesDirWide(out, out_chars)) {
			return false;
		}
		wcscat_s(out, out_chars, L"graceful_exit.flag");
		return true;
	}

	void WriteGracefulExitFlag()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetGracefulExitFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		const char marker[] = "graceful";
		DWORD written = 0;
		WriteFile(file, marker, static_cast<DWORD>(sizeof(marker) - 1), &written, nullptr);
		CloseHandle(file);
	}

	bool HasGracefulExitFlag()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetGracefulExitFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return false;
		}
		const DWORD attr = GetFileAttributesW(path);
		return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	void ClearGracefulExitFlag()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetGracefulExitFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		DeleteFileW(path);
	}

	bool GetSessionActiveFlagPath(wchar_t* out, size_t out_chars)
	{
		if (!GetCrashesDirWide(out, out_chars)) {
			return false;
		}
		wcscat_s(out, out_chars, L"session_active.flag");
		return true;
	}

	void WriteSessionActiveMarker()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetSessionActiveFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		char marker[64] = {};
		snprintf(marker, sizeof(marker), "pid=%lu", static_cast<unsigned long>(GetCurrentProcessId()));
		DWORD written = 0;
		WriteFile(file, marker, static_cast<DWORD>(strlen(marker)), &written, nullptr);
		FlushFileBuffers(file);
		CloseHandle(file);
	}

	void ClearSessionActiveMarker()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetSessionActiveFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return;
		}
		DeleteFileW(path);
	}

	bool HasSessionActiveMarker()
	{
		wchar_t path[MAX_PATH * 4] = {};
		if (!GetSessionActiveFlagPath(path, sizeof(path) / sizeof(path[0]))) {
			return false;
		}
		const DWORD attr = GetFileAttributesW(path);
		return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	void WriteEmergencyCrashStamp(DWORD exception_code, void* address)
	{
		wchar_t crashes_dir[MAX_PATH * 4] = {};
		if (!GetCrashesDirForExceptionContext(crashes_dir, sizeof(crashes_dir) / sizeof(crashes_dir[0]))) {
			return;
		}

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t stamp[64] = {};
		_snwprintf_s(stamp, _TRUNCATE, L"%04u%02u%02u_%02u%02u%02u",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		wchar_t report_path[MAX_PATH * 4] = {};
		_snwprintf_s(report_path, _TRUNCATE, L"%scrash_%s.json", crashes_dir, stamp);
		if (report_path[0] == L'\0') {
			return;
		}

		void* resolved_address = address;
		if (resolved_address == nullptr) {
			resolved_address = CaptureTopExceptionAddress();
		}

		char module_name[128] = {};
		char module_offset[32] = {};
		ResolveModuleName(resolved_address, module_name, sizeof(module_name), module_offset, sizeof(module_offset));

		const uintptr_t addr_val = reinterpret_cast<uintptr_t>(resolved_address);
		char addr_text[32] = {};
		if (resolved_address != nullptr) {
			snprintf(addr_text, sizeof(addr_text), "0x%llX", static_cast<unsigned long long>(addr_val));
		}

		wchar_t log_path[MAX_PATH * 4] = {};
		GetLogFilePathWide(log_path, sizeof(log_path) / sizeof(log_path[0]));
		const std::string json = BuildCrashReportJsonBody(exception_code, resolved_address,
			nullptr, log_path, "vectored_handler");

		HANDLE report_file = CreateFileW(report_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (report_file != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(report_file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
			FlushFileBuffers(report_file);
			CloseHandle(report_file);
		}

		wchar_t pending[MAX_PATH * 4] = {};
		_snwprintf_s(pending, _TRUNCATE, L"%spending_report.txt", crashes_dir);
		HANDLE pending_file = CreateFileW(pending, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (pending_file != INVALID_HANDLE_VALUE) {
			char utf8[MAX_PATH * 4] = {};
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, report_path, -1, utf8, sizeof(utf8), nullptr, nullptr);
			if (bytes > 1) {
				DWORD written = 0;
				WriteFile(pending_file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
				FlushFileBuffers(pending_file);
			}
			CloseHandle(pending_file);
		}
	}

	DWORD GetPendingReportAgeSeconds()
	{
		wchar_t pending[MAX_PATH * 4] = {};
		if (!GetCrashesDirWide(pending, sizeof(pending) / sizeof(pending[0]))) {
			return UINT32_MAX;
		}
		wcscat_s(pending, L"pending_report.txt");

		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (!GetFileAttributesExW(pending, GetFileExInfoStandard, &fad)) {
			return UINT32_MAX;
		}

		FILETIME now_ft = {};
		GetSystemTimeAsFileTime(&now_ft);
		ULARGE_INTEGER now;
		now.LowPart = now_ft.dwLowDateTime;
		now.HighPart = now_ft.dwHighDateTime;
		ULARGE_INTEGER file_time;
		file_time.LowPart = fad.ftLastWriteTime.dwLowDateTime;
		file_time.HighPart = fad.ftLastWriteTime.dwHighDateTime;
		if (now.QuadPart <= file_time.QuadPart) {
			return 0;
		}
		return static_cast<DWORD>((now.QuadPart - file_time.QuadPart) / 10000000ULL);
	}

	void LaunchReportAfterParentExit(DWORD parent_exit_code)
	{
		if (HasGracefulExitFlag()) {
			WriteHandlerLog("Watchdog: graceful exit flag set, skip report");
			ClearGracefulExitFlag();
			ClearSessionActiveMarker();
			return;
		}

		wchar_t report_path[MAX_PATH * 4] = {};
		bool have_report = false;

		const bool pending_recent = ReadPendingReportPathWide(report_path, sizeof(report_path) / sizeof(report_path[0]))
			&& GetPendingReportAgeSeconds() <= 120;
		const bool session_abnormal = HasSessionActiveMarker();

		if (pending_recent) {
			WriteHandlerLog("Watchdog: found pending_report.txt");
			have_report = true;
		}
		else if (FindLatestCrashReportJsonWide(report_path, sizeof(report_path) / sizeof(report_path[0]), 120)) {
			WriteHandlerLog("Watchdog: found recent crash_*.json");
			have_report = true;
		}
		else if (parent_exit_code != 0 && parent_exit_code != static_cast<DWORD>(STILL_ACTIVE)) {
			char buf[96] = {};
			snprintf(buf, sizeof(buf), "Watchdog: abnormal exit 0x%08lX, writing minimal report",
				static_cast<unsigned long>(parent_exit_code));
			WriteHandlerLog(buf);
			have_report = WriteMinimalCrashReport(parent_exit_code, report_path,
				sizeof(report_path) / sizeof(report_path[0]), "watchdog_inferred");
			if (have_report) {
				WritePendingReportPath(report_path);
			}
		}
		else if (session_abnormal) {
			WriteHandlerLog("Watchdog: session_active without graceful exit");
			if (parent_exit_code == 0) {
				// 結束碼 0 且無近期真實崩潰 JSON：多為強制結束／Stop-Process，勿假造 ACCESS_VIOLATION。
				if (FindLatestCrashReportJsonWide(report_path, sizeof(report_path) / sizeof(report_path[0]), 120)) {
					WriteHandlerLog("Watchdog: exit 0 but recent crash json exists, reusing");
					have_report = true;
				}
				else {
					WriteHandlerLog("Watchdog: exit 0 without crash artifact, skip inferred report");
					ClearSessionActiveMarker();
					return;
				}
			}
			else {
				have_report = WriteMinimalCrashReport(parent_exit_code, report_path,
					sizeof(report_path) / sizeof(report_path[0]), "watchdog_inferred_session");
			}
			if (have_report) {
				WritePendingReportPath(report_path);
			}
		}

		ClearSessionActiveMarker();

		if (!have_report) {
			WriteHandlerLog("Watchdog: no crash report to show");
			return;
		}

		if (!RelaunchCrashReportMode(report_path)) {
			WriteHandlerLog("Watchdog: RelaunchCrashReportMode failed");
			ShowReportLaunchFailureMessage(report_path);
			return;
		}

		Sleep(2500);
		DWORD ui_pid = 0;
		if (ReadReportUiLockPid(&ui_pid) && IsProcessAlive(ui_pid)) {
			WriteHandlerLog("Watchdog: crash-report process is running");
			return;
		}

		WriteHandlerLog("Watchdog: crash-report process not detected, showing MessageBox");
		ShowReportLaunchFailureMessage(report_path);
	}

	void LaunchPendingReportFromWatchdog()
	{
		LaunchReportAfterParentExit(0);
	}

	bool IsGracefulOrShuttingDown()
	{
		return g_shutting_down != 0 || HasGracefulExitFlag();
	}

	void HandleFatalCrash(DWORD exception_code, void* address, EXCEPTION_POINTERS* ep)
	{
		if (IsGracefulOrShuttingDown()) {
			WriteHandlerLog("HandleFatalCrash: suppressed (graceful shutdown)");
			return;
		}

		if (InterlockedCompareExchange(&g_crash_handling, 1, 0) != 0) {
			return;
		}

		WriteHandlerLog("HandleFatalCrash entered");
		ClearGracefulExitFlag();
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

		wchar_t report_path[MAX_PATH * 4] = {};
		wchar_t dump_path[MAX_PATH * 4] = {};
		wchar_t log_path[MAX_PATH * 4] = {};
		BuildCrashFilePaths(report_path, sizeof(report_path) / sizeof(report_path[0]),
			dump_path, sizeof(dump_path) / sizeof(dump_path[0]));
		GetLogFilePathWide(log_path, sizeof(log_path) / sizeof(log_path[0]));

		if (report_path[0] == L'\0') {
			WriteHandlerLog("HandleFatalCrash: failed to build report path");
			return;
		}

		void* crash_address = address;
		if (crash_address == nullptr) {
			crash_address = CaptureTopExceptionAddress();
		}

		WritePendingReportPath(report_path);
		WriteCrashReportJson(report_path, exception_code, crash_address, dump_path, log_path,
			ReportSourceForCode(exception_code));

		if (exception_code != EXCEPTION_STACK_OVERFLOW && dump_path[0] != L'\0') {
			EXCEPTION_RECORD synthetic_er = {};
			CONTEXT synthetic_ctx = {};
			EXCEPTION_POINTERS synthetic_ep = {};
			bool wrote_dump = false;
			if (ep != nullptr) {
				wrote_dump = WriteMiniDump(dump_path, ep);
			}
			else {
				RtlCaptureContext(&synthetic_ctx);
				synthetic_er.ExceptionCode = exception_code;
				synthetic_er.ExceptionAddress = crash_address;
				synthetic_ep.ExceptionRecord = &synthetic_er;
				synthetic_ep.ContextRecord = &synthetic_ctx;
				wrote_dump = WriteMiniDump(dump_path, &synthetic_ep);
			}
			if (wrote_dump) {
				WriteHandlerLog("HandleFatalCrash: minidump written");
			}
			else {
				WriteHandlerLog("HandleFatalCrash: minidump failed");
			}
		}

		typedef BOOL(WINAPI * AllowSetForegroundWindowFn)(DWORD);
		const HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32 != nullptr) {
			const auto allow_fg = reinterpret_cast<AllowSetForegroundWindowFn>(
				GetProcAddress(user32, "AllowSetForegroundWindow"));
			if (allow_fg != nullptr) {
				allow_fg(ASFW_ANY);
			}
		}

		if (!RelaunchCrashReportMode(report_path)) {
			WriteHandlerLog("HandleFatalCrash: relaunch failed, watchdog should retry");
		}
		else {
			WriteHandlerLog("HandleFatalCrash: relaunch succeeded");
		}
	}

	LONG WINAPI HCrashVectoredExceptionHandler(EXCEPTION_POINTERS* ep)
	{
		if (ep == nullptr || ep->ExceptionRecord == nullptr) {
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const DWORD code = ep->ExceptionRecord->ExceptionCode;
		if (code == EXCEPTION_ACCESS_VIOLATION
			|| code == EXCEPTION_STACK_OVERFLOW
			|| code == EXCEPTION_INT_DIVIDE_BY_ZERO
			|| code == EXCEPTION_ILLEGAL_INSTRUCTION) {
			if (InterlockedCompareExchange(&g_vectored_stamp_written, 1, 0) == 0) {
				WriteEmergencyCrashStamp(code, ep->ExceptionRecord->ExceptionAddress);
			}
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}

	LONG WINAPI HCrashUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
	{
		DWORD code = 0;
		void* address = nullptr;
		if (ep != nullptr && ep->ExceptionRecord != nullptr) {
			code = ep->ExceptionRecord->ExceptionCode;
			address = ep->ExceptionRecord->ExceptionAddress;
		}
		HandleFatalCrash(code, address, ep);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void HCrashTerminateHandler()
	{
		if (IsGracefulOrShuttingDown()) {
			_exit(0);
		}
		HandleFatalCrash(static_cast<DWORD>(0xE0000001), nullptr, nullptr);
		std::abort();
	}

	void HCrashInvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
	{
		if (IsGracefulOrShuttingDown()) {
			return;
		}
		HandleFatalCrash(static_cast<DWORD>(0xE0000002), nullptr, nullptr);
	}

	void HCrashPurecallHandler()
	{
		if (IsGracefulOrShuttingDown()) {
			return;
		}
		HandleFatalCrash(static_cast<DWORD>(0xE0000003), nullptr, nullptr);
	}

	void HCrashSigabrtHandler(int)
	{
		if (IsGracefulOrShuttingDown()) {
			_exit(0);
		}
		void* crash_address = CaptureTopExceptionAddress();
		EXCEPTION_RECORD er = {};
		CONTEXT ctx = {};
		EXCEPTION_POINTERS ep = {};
		RtlCaptureContext(&ctx);
		er.ExceptionCode = static_cast<DWORD>(0xE0000004);
		er.ExceptionAddress = crash_address;
		ep.ExceptionRecord = &er;
		ep.ContextRecord = &ctx;
		HandleFatalCrash(static_cast<DWORD>(0xE0000004), crash_address, &ep);
		_exit(1);
	}
}

void HCrashHandlerInstall()
{
	InterlockedExchange(&g_vectored_stamp_written, 0);
	InitCrashesDirCache();
	ClearGracefulExitFlag();
	WriteSessionActiveMarker();
	AddVectoredExceptionHandler(1, HCrashVectoredExceptionHandler);
	SetUnhandledExceptionFilter(HCrashUnhandledExceptionFilter);
	std::set_terminate(HCrashTerminateHandler);
	_set_invalid_parameter_handler(HCrashInvalidParameterHandler);
	_set_purecall_handler(HCrashPurecallHandler);
	signal(SIGABRT, HCrashSigabrtHandler);
	WriteHandlerLog("HCrashHandlerInstall complete");
}

void HCrashHandlerReinstallFilter()
{
	SetUnhandledExceptionFilter(HCrashUnhandledExceptionFilter);
}

void HCrashMarkGracefulApplicationExit()
{
	InterlockedExchange(&g_shutting_down, 1);
	ClearSessionActiveMarker();
	WriteGracefulExitFlag();
	HAppPaths::ClearPendingCrashReport();
	WriteHandlerLog("Graceful application exit marked");
}

bool HCrashShouldShowPendingReportOnStartup()
{
	if (HasGracefulExitFlag()) {
		ClearGracefulExitFlag();
		return false;
	}
	return GetPendingReportAgeSeconds() <= 120;
}

void HCrashWatchdogSpawn()
{
	g_watchdog_child_pid = 0;
	g_watchdog_spawn_ok = false;

	wchar_t exe_path[MAX_PATH] = {};
	wchar_t work_dir[MAX_PATH] = {};
	GetExePathAndWorkDir(exe_path, sizeof(exe_path) / sizeof(exe_path[0]),
		work_dir, sizeof(work_dir) / sizeof(work_dir[0]));

	const DWORD parent_pid = GetCurrentProcessId();
	wchar_t args[512] = {};
	_snwprintf_s(args, _TRUNCATE, L"--mode=watchdog --parent-pid=%lu", static_cast<unsigned long>(parent_pid));

	const wchar_t* work = work_dir[0] != L'\0' ? work_dir : nullptr;
	wchar_t cmd_line[768] = {};
	_snwprintf_s(cmd_line, _TRUNCATE, L"\"%s\" %s", exe_path, args);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};

	bool ok = CreateProcessW(nullptr, cmd_line, nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW | kDetachedChildFlags, nullptr, work, &si, &pi);
	if (ok) {
		g_watchdog_child_pid = pi.dwProcessId;
		g_watchdog_spawn_ok = true;
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		char buf[96] = {};
		snprintf(buf, sizeof(buf), "Watchdog spawned pid=%lu", static_cast<unsigned long>(g_watchdog_child_pid));
		WriteHandlerLog(buf);
		return;
	}

	char buf[96] = {};
	snprintf(buf, sizeof(buf), "Watchdog CreateProcess failed: %lu", GetLastError());
	WriteHandlerLog(buf);

	ok = SpawnDetachedViaCmdStart(exe_path, args, work);
	if (ok) {
		g_watchdog_spawn_ok = true;
		WriteHandlerLog("Watchdog spawned via cmd (pid untracked)");
	}
	else {
		snprintf(buf, sizeof(buf), "Watchdog spawn failed: %lu", GetLastError());
		WriteHandlerLog(buf);
	}
}

bool HCrashIsWatchdogAlive()
{
	if (!g_watchdog_spawn_ok) {
		return false;
	}
	if (g_watchdog_child_pid == 0) {
		return true;
	}

	HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, g_watchdog_child_pid);
	if (proc == nullptr) {
		g_watchdog_child_pid = 0;
		return false;
	}
	const DWORD wait = WaitForSingleObject(proc, 0);
	CloseHandle(proc);
	if (wait == WAIT_OBJECT_0) {
		g_watchdog_child_pid = 0;
		return false;
	}
	return true;
}

int HCrashWatchdogRun(DWORD parent_process_id)
{
	WriteHandlerLog("Watchdog started");

	if (parent_process_id == 0) {
		WriteHandlerLog("Watchdog: invalid parent pid");
		return 1;
	}

	HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_process_id);
	if (parent == nullptr) {
		char buf[96] = {};
		snprintf(buf, sizeof(buf), "Watchdog: OpenProcess failed %lu", GetLastError());
		WriteHandlerLog(buf);
		return 1;
	}

	WaitForSingleObject(parent, INFINITE);

	DWORD parent_exit_code = 0;
	GetExitCodeProcess(parent, &parent_exit_code);
	CloseHandle(parent);

	char buf[96] = {};
	snprintf(buf, sizeof(buf), "Watchdog: parent exited code=0x%08lX",
		static_cast<unsigned long>(parent_exit_code));
	WriteHandlerLog(buf);
	Sleep(1200);

	LaunchReportAfterParentExit(parent_exit_code);
	return 0;
}

void HCrashClearReportUiLock()
{
	ClearReportUiLock();
}

void HCrashWriteReportUiLockCurrent()
{
	WriteReportUiLock(GetCurrentProcessId());
}
