#include "HElevationBroker.h"
#include "HCleanTask.h"
#include "HPage.h"
#include "HAppPaths.h"
#include <shellapi.h>
#include <aclapi.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
	HANDLE g_pipe = INVALID_HANDLE_VALUE;
	HANDLE g_broker_process = nullptr;
	PSECURITY_DESCRIPTOR g_pipe_psd = nullptr;

	std::string WideToUtf8(const wchar_t* wide)
	{
		if (wide == nullptr || wide[0] == L'\0') {
			return {};
		}
		const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (needed <= 0) {
			return {};
		}
		std::vector<char> buf(static_cast<size_t>(needed));
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), needed, nullptr, nullptr);
		return std::string(buf.data());
	}

	std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty()) {
			return {};
		}
		const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		if (needed <= 0) {
			return {};
		}
		std::vector<wchar_t> buf(static_cast<size_t>(needed));
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, buf.data(), needed);
		return std::wstring(buf.data());
	}

	std::string HandshakeFilePath(DWORD parent_pid)
	{
		return HAppPaths::GetConfigDir() + "\\elev_" + std::to_string(parent_pid) + ".pipe";
	}

	bool WriteHandshakeFile(DWORD parent_pid, const wchar_t* pipe_name)
	{
		if (pipe_name == nullptr || pipe_name[0] == L'\0') {
			return false;
		}
		HAppPaths::EnsureAppDataDirs();
		const std::string path = HandshakeFilePath(parent_pid);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			return false;
		}
		const std::string utf8 = WideToUtf8(pipe_name);
		out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
		return out.good();
	}

	std::wstring ReadHandshakeFile(DWORD parent_pid)
	{
		const std::string path = HandshakeFilePath(parent_pid);
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open()) {
			return {};
		}
		std::string utf8((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		while (!utf8.empty() && (utf8.back() == '\r' || utf8.back() == '\n' || utf8.back() == ' ')) {
			utf8.pop_back();
		}
		return Utf8ToWide(utf8);
	}

	void DeleteHandshakeFile(DWORD parent_pid)
	{
		const std::string path = HandshakeFilePath(parent_pid);
		DeleteFileA(path.c_str());
	}

	bool BuildPipeSecurityAttributes(SECURITY_ATTRIBUTES& sa)
	{
		if (g_pipe_psd != nullptr) {
			LocalFree(g_pipe_psd);
			g_pipe_psd = nullptr;
		}

		// 僅 DACL、不指定 Owner，避免 ERROR_INVALID_OWNER (1307)
		const wchar_t* kSddl = L"D:(A;;GA;;;WD)(A;;GA;;;BU)";
		if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
			kSddl, SDDL_REVISION_1, &g_pipe_psd, nullptr)) {
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.lpSecurityDescriptor = g_pipe_psd;
			sa.bInheritHandle = FALSE;
			return true;
		}

		static SECURITY_DESCRIPTOR null_dacl_sd = {};
		if (!InitializeSecurityDescriptor(&null_dacl_sd, SECURITY_DESCRIPTOR_REVISION)) {
			return false;
		}
		if (!SetSecurityDescriptorDacl(&null_dacl_sd, TRUE, nullptr, FALSE)) {
			return false;
		}
		sa.nLength = sizeof(SECURITY_ATTRIBUTES);
		sa.lpSecurityDescriptor = &null_dacl_sd;
		sa.bInheritHandle = FALSE;
		return true;
	}

	bool RunLocalHiddenCommand(const wchar_t* cmdline, DWORD timeout_ms, DWORD* exit_code_out)
	{
		if (cmdline == nullptr || cmdline[0] == L'\0') {
			return false;
		}
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::vector<wchar_t> cmd_buf(cmdline, cmdline + wcslen(cmdline) + 1);
		if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
			HLOG_WARN("HElevationBroker: CreateProcess failed err={}", GetLastError());
			return false;
		}
		WaitForSingleObject(pi.hProcess, timeout_ms);
		DWORD exit_code = 1;
		GetExitCodeProcess(pi.hProcess, &exit_code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		if (exit_code_out != nullptr) {
			*exit_code_out = exit_code;
		}
		return exit_code == 0;
	}

	bool WritePipeMessage(HANDLE pipe, const wchar_t* text)
	{
		if (pipe == INVALID_HANDLE_VALUE || text == nullptr) {
			return false;
		}
		const DWORD bytes = static_cast<DWORD>((wcslen(text) + 1) * sizeof(wchar_t));
		DWORD written = 0;
		return WriteFile(pipe, text, bytes, &written, nullptr) != FALSE && written == bytes;
	}

	bool ReadPipeMessage(HANDLE pipe, std::wstring& out)
	{
		out.clear();
		if (pipe == INVALID_HANDLE_VALUE) {
			return false;
		}
		wchar_t buf[2048] = {};
		DWORD read = 0;
		if (!ReadFile(pipe, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr) || read == 0) {
			return false;
		}
		buf[read / sizeof(wchar_t)] = L'\0';
		out = buf;
		return true;
	}

	bool BuildPipePath(const wchar_t* pipe_base_name, std::wstring& out_path)
	{
		if (pipe_base_name == nullptr || pipe_base_name[0] == L'\0') {
			return false;
		}
		out_path = L"\\\\.\\pipe\\";
		out_path += pipe_base_name;
		return true;
	}

	bool CreatePipeServer(const wchar_t* pipe_base_name, HANDLE& out_pipe)
	{
		std::wstring path;
		if (!BuildPipePath(pipe_base_name, path)) {
			return false;
		}

		SECURITY_ATTRIBUTES sa = {};
		if (!BuildPipeSecurityAttributes(sa)) {
			HLOG_WARN("HElevationBroker: pipe security setup failed err={}", GetLastError());
			return false;
		}

		out_pipe = CreateNamedPipeW(
			path.c_str(),
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			1, 8192, 8192, 5000, &sa);
		if (out_pipe == INVALID_HANDLE_VALUE) {
			HLOG_WARN("HElevationBroker: CreateNamedPipe failed err={}", GetLastError());
			return false;
		}
		return true;
	}

	struct ConnectThreadCtx {
		HANDLE pipe = INVALID_HANDLE_VALUE;
		volatile LONG connected = 0;
		volatile DWORD connect_err = 0;
	};

	DWORD WINAPI ConnectThreadProc(LPVOID param)
	{
		auto* ctx = static_cast<ConnectThreadCtx*>(param);
		if (ctx == nullptr || ctx->pipe == INVALID_HANDLE_VALUE) {
			return 1;
		}
		if (ConnectNamedPipe(ctx->pipe, nullptr)) {
			InterlockedExchange(&ctx->connected, 1);
			return 0;
		}
		const DWORD err = GetLastError();
		ctx->connect_err = err;
		if (err == ERROR_PIPE_CONNECTED) {
			InterlockedExchange(&ctx->connected, 1);
			return 0;
		}
		return 1;
	}

	bool ConnectClientPipe(const wchar_t* pipe_base_name, HANDLE& out_pipe, DWORD* last_err_out = nullptr)
	{
		std::wstring path;
		if (!BuildPipePath(pipe_base_name, path)) {
			return false;
		}

		DWORD last_err = 0;
		const DWORD start = GetTickCount();
		while (GetTickCount() - start < 45000) {
			out_pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, 0, nullptr);
			if (out_pipe != INVALID_HANDLE_VALUE) {
				if (last_err_out != nullptr) {
					*last_err_out = 0;
				}
				return true;
			}
			last_err = GetLastError();
			if (last_err != ERROR_PIPE_BUSY && last_err != ERROR_FILE_NOT_FOUND) {
				Sleep(100);
			}
			if (!WaitNamedPipeW(path.c_str(), 300)) {
				Sleep(50);
			}
		}
		if (last_err_out != nullptr) {
			*last_err_out = last_err;
		}
		return false;
	}

	std::wstring ResolveBrokerPipeName(DWORD parent_pid, const wchar_t* pipe_from_cmdline)
	{
		if (pipe_from_cmdline != nullptr && pipe_from_cmdline[0] != L'\0') {
			return pipe_from_cmdline;
		}
		if (parent_pid != 0) {
			return ReadHandshakeFile(parent_pid);
		}
		return {};
	}

	void CloseClient(DWORD parent_pid_for_handshake = 0)
	{
		if (g_pipe != INVALID_HANDLE_VALUE) {
			WritePipeMessage(g_pipe, L"QUIT");
			FlushFileBuffers(g_pipe);
			DisconnectNamedPipe(g_pipe);
			CloseHandle(g_pipe);
			g_pipe = INVALID_HANDLE_VALUE;
		}
		if (g_broker_process != nullptr) {
			WaitForSingleObject(g_broker_process, 3000);
			CloseHandle(g_broker_process);
			g_broker_process = nullptr;
		}
		if (parent_pid_for_handshake != 0) {
			DeleteHandshakeFile(parent_pid_for_handshake);
		}
	}
}

namespace HElevationBroker {

	bool IsConnected()
	{
		return g_pipe != INVALID_HANDLE_VALUE;
	}

	bool RequestElevation()
	{
		if (HCleanIsRunningAsAdmin()) {
			return true;
		}
		if (IsConnected()) {
			return true;
		}

		const DWORD pid = GetCurrentProcessId();
		CloseClient(pid);

		wchar_t exe_path[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
			return false;
		}

		const ULONGLONG tick = GetTickCount64();
		wchar_t pipe_name[80] = {};
		_snwprintf_s(pipe_name, _TRUNCATE, L"HP_CLEANER_Elev_%lu_%llu", pid, tick);

		if (!WriteHandshakeFile(pid, pipe_name)) {
			HLOG_WARN("HElevationBroker: handshake file write failed");
		}

		HANDLE server_pipe = INVALID_HANDLE_VALUE;
		if (!CreatePipeServer(pipe_name, server_pipe)) {
			DeleteHandshakeFile(pid);
			return false;
		}

		ConnectThreadCtx connect_ctx = {};
		connect_ctx.pipe = server_pipe;
		HANDLE connect_thread = ::CreateThread(nullptr, 0, ConnectThreadProc, &connect_ctx, 0, nullptr);

		wchar_t args[192] = {};
		_snwprintf_s(args, _TRUNCATE, L"--mode=elev-broker --parent-pid=%lu --pipe=%s", pid, pipe_name);

		SHELLEXECUTEINFOW sei = {};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
		sei.lpVerb = L"runas";
		sei.lpFile = exe_path;
		sei.lpParameters = args;
		sei.nShow = SW_HIDE;

		HLOG_INFO("HElevationBroker: requesting UAC elevation pipe={} parent_pid={}",
			WideToUtf8(pipe_name), pid);
		if (!ShellExecuteExW(&sei)) {
			HLOG_WARN("HElevationBroker: ShellExecuteEx failed err={}", GetLastError());
			if (connect_thread != nullptr) {
				CloseHandle(server_pipe);
				WaitForSingleObject(connect_thread, 2000);
				CloseHandle(connect_thread);
			}
			else {
				CloseHandle(server_pipe);
			}
			DeleteHandshakeFile(pid);
			return false;
		}
		if (reinterpret_cast<intptr_t>(sei.hInstApp) <= 32) {
			HLOG_WARN("HElevationBroker: elevation cancelled code={}", reinterpret_cast<intptr_t>(sei.hInstApp));
			if (connect_thread != nullptr) {
				CloseHandle(server_pipe);
				WaitForSingleObject(connect_thread, 2000);
				CloseHandle(connect_thread);
			}
			else {
				CloseHandle(server_pipe);
			}
			DeleteHandshakeFile(pid);
			return false;
		}
		g_broker_process = sei.hProcess;

		bool connected = false;
		if (connect_thread != nullptr) {
			const DWORD wait_rc = WaitForSingleObject(connect_thread, 60000);
			connected = (connect_ctx.connected != 0);
			if (wait_rc == WAIT_TIMEOUT || !connected) {
				HLOG_WARN("HElevationBroker: broker client timeout wait={} connect_err={}",
					wait_rc, connect_ctx.connect_err);
				CloseHandle(server_pipe);
				WaitForSingleObject(connect_thread, 2000);
				CloseHandle(connect_thread);
				CloseClient(pid);
				return false;
			}
			CloseHandle(connect_thread);
		}

		g_pipe = server_pipe;

		std::wstring pong;
		if (!WritePipeMessage(g_pipe, L"PING") || !ReadPipeMessage(g_pipe, pong)
			|| pong.find(L"PONG") != 0) {
			HLOG_WARN("HElevationBroker: broker ping failed");
			CloseClient(pid);
			return false;
		}

		DeleteHandshakeFile(pid);
		HLOG_INFO("HElevationBroker: connected");
		return true;
	}

	bool RunHiddenCommand(const wchar_t* command_line, DWORD timeout_ms, DWORD* exit_code_out)
	{
		if (command_line == nullptr || command_line[0] == L'\0') {
			return false;
		}
		if (HCleanIsRunningAsAdmin()) {
			return RunLocalHiddenCommand(command_line, timeout_ms, exit_code_out);
		}
		if (!IsConnected()) {
			return false;
		}

		std::wstring req = L"RUN\t";
		req += command_line;
		if (!WritePipeMessage(g_pipe, req.c_str())) {
			return false;
		}
		std::wstring resp;
		if (!ReadPipeMessage(g_pipe, resp)) {
			return false;
		}
		if (resp.rfind(L"OK\t", 0) != 0) {
			if (exit_code_out != nullptr) {
				*exit_code_out = 1;
			}
			return false;
		}
		const DWORD code = static_cast<DWORD>(wcstoul(resp.c_str() + 3, nullptr, 10));
		if (exit_code_out != nullptr) {
			*exit_code_out = code;
		}
		return code == 0;
	}

	void Shutdown()
	{
		CloseClient();
	}

	int RunBrokerMain(DWORD parent_pid, const wchar_t* pipe_base_name, const wchar_t* ready_event_name)
	{
		(void)ready_event_name;
		HInitLogging();

		const std::wstring cmdline = GetCommandLineW() != nullptr ? GetCommandLineW() : L"";
		HLOG_INFO("HElevationBroker: broker starting parent_pid={} cmdline={}",
			parent_pid, WideToUtf8(cmdline.c_str()));

		const std::wstring pipe_name = ResolveBrokerPipeName(parent_pid, pipe_base_name);
		if (pipe_name.empty()) {
			HLOG_WARN("HElevationBroker: broker missing pipe name (parent_pid={})", parent_pid);
			return 1;
		}

		HLOG_INFO("HElevationBroker: broker connecting pipe={}", WideToUtf8(pipe_name.c_str()));

		HANDLE client_pipe = INVALID_HANDLE_VALUE;
		DWORD connect_err = 0;
		if (!ConnectClientPipe(pipe_name.c_str(), client_pipe, &connect_err)) {
			HLOG_WARN("HElevationBroker: pipe connect failed pipe={} err={}",
				WideToUtf8(pipe_name.c_str()), connect_err);
			return 1;
		}

		HLOG_INFO("HElevationBroker: broker connected to parent pipe");

		for (;;) {
			wchar_t req[2048] = {};
			DWORD read = 0;
			if (!ReadFile(client_pipe, req, sizeof(req) - sizeof(wchar_t), &read, nullptr) || read == 0) {
				break;
			}
			req[read / sizeof(wchar_t)] = L'\0';

			if (wcscmp(req, L"PING") == 0) {
				WritePipeMessage(client_pipe, L"PONG");
				continue;
			}
			if (wcscmp(req, L"QUIT") == 0) {
				break;
			}
			if (wcsncmp(req, L"RUN\t", 4) == 0) {
				DWORD exit_code = 1;
				const bool ok = RunLocalHiddenCommand(req + 4, 120000, &exit_code);
				wchar_t resp[64] = {};
				_snwprintf_s(resp, _TRUNCATE, L"OK\t%lu", static_cast<unsigned long>(exit_code));
				WritePipeMessage(client_pipe, resp);
				(void)ok;
				continue;
			}
			WritePipeMessage(client_pipe, L"ERR\tbad_request");
		}

		CloseHandle(client_pipe);
		HLOG_INFO("HElevationBroker: broker exiting");
		return 0;
	}
}

bool HCleanHasElevatedAccess()
{
	return HCleanIsRunningAsAdmin() || HElevationBroker::IsConnected();
}
