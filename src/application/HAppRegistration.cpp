#include "HAppRegistration.h"
#include "Hi18n.h"
#include "HAppPaths.h"
#include "HAppSettings.h"
#include "HPage.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <initguid.h>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
	const wchar_t* kAppDisplayName = L"HP CLEANER++";
	const wchar_t* kUninstallRegKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\HP CLEANER++";
	const wchar_t* kPublisher = L"HalfPeople Studio";

	std::wstring GetExePath()
	{
		wchar_t path[MAX_PATH] = {};
		if (::GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
			return {};
		}
		return path;
	}

	std::wstring GetExeDirectory()
	{
		const std::wstring exe = GetExePath();
		if (exe.empty()) {
			return {};
		}
		const size_t slash = exe.find_last_of(L"\\/");
		if (slash == std::wstring::npos) {
			return {};
		}
		return exe.substr(0, slash);
	}

	bool FileExistsW(const std::wstring& path)
	{
		if (path.empty()) {
			return false;
		}
		const DWORD attr = ::GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES;
	}

	bool CreateShellLink(const std::wstring& lnk_path, const std::wstring& target_exe,
		const std::wstring& work_dir, const wchar_t* description)
	{
		IShellLinkW* link = nullptr;
		if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&link)))) {
			return false;
		}
		link->SetPath(target_exe.c_str());
		link->SetWorkingDirectory(work_dir.c_str());
		if (description != nullptr) {
			link->SetDescription(description);
		}

		IPersistFile* file = nullptr;
		const HRESULT hr_qi = link->QueryInterface(IID_PPV_ARGS(&file));
		link->Release();
		if (FAILED(hr_qi)) {
			return false;
		}
		const HRESULT hr_save = file->Save(lnk_path.c_str(), TRUE);
		file->Release();
		return SUCCEEDED(hr_save);
	}

	bool WriteUninstallRegistry(const std::wstring& exe_path, const std::wstring& install_dir)
	{
		HKEY key = nullptr;
		if (::RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallRegKey, 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
			return false;
		}

		std::wstring uninstall_cmd = L"\"";
		uninstall_cmd += exe_path;
		uninstall_cmd += L"\" --mode=uninstall";

		const std::wstring icon = exe_path + L",0";
		auto set_sz = [&](const wchar_t* name, const std::wstring& value) {
			::RegSetValueExW(key, name, 0, REG_SZ,
				reinterpret_cast<const BYTE*>(value.c_str()),
				static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
		};

		set_sz(L"DisplayName", kAppDisplayName);
		set_sz(L"DisplayIcon", icon);
		set_sz(L"DisplayVersion", L"1.0.0.0");
		set_sz(L"Publisher", kPublisher);
		set_sz(L"InstallLocation", install_dir);
		set_sz(L"UninstallString", uninstall_cmd);
		set_sz(L"QuietUninstallString", uninstall_cmd);
		set_sz(L"InstallDate", L"20260605");

		::RegCloseKey(key);
		return true;
	}

	void RemoveUninstallRegistry()
	{
		::RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallRegKey);
	}

	bool RemoveFileIfExists(const std::wstring& path)
	{
		if (!FileExistsW(path)) {
			return true;
		}
		return ::DeleteFileW(path.c_str()) != FALSE;
	}

	bool RemoveDirectoryTree(const std::wstring& root)
	{
		if (root.empty() || !FileExistsW(root)) {
			return true;
		}
		std::wstring pattern = root;
		if (pattern.back() != L'\\') {
			pattern += L'\\';
		}
		pattern += L'*';

		WIN32_FIND_DATAW fd = {};
		HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) {
			return ::RemoveDirectoryW(root.c_str()) != FALSE;
		}
		do {
			if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
				continue;
			}
			std::wstring full = root;
			if (full.back() != L'\\') {
				full += L'\\';
			}
			full += fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				RemoveDirectoryTree(full);
			}
			else {
				::SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
				::DeleteFileW(full.c_str());
			}
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
		return ::RemoveDirectoryW(root.c_str()) != FALSE;
	}
}

namespace HAppRegistration {

	std::wstring GetInstallDirectory()
	{
		return GetExeDirectory();
	}

	void EnsureRegisteredOnStartup()
	{
		const std::wstring exe = GetExePath();
		const std::wstring dir = GetExeDirectory();
		if (exe.empty() || dir.empty()) {
			return;
		}

		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool co_owned = SUCCEEDED(co) || co == RPC_E_CHANGED_MODE;

		wchar_t desktop[MAX_PATH] = {};
		wchar_t programs[MAX_PATH] = {};
		::SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop);
		::SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, programs);

		const std::wstring desktop_lnk = std::wstring(desktop) + L"\\HP CLEANER++.lnk";
		if (!FileExistsW(desktop_lnk)) {
			CreateShellLink(desktop_lnk, exe, dir, W18N(u8"HP CLEANER++ 系統清理與優化"));
			HLOG_INFO("HAppRegistration: created desktop shortcut");
		}

		const std::wstring menu_dir = std::wstring(programs) + L"\\HP CLEANER++";
		if (!FileExistsW(menu_dir)) {
			::SHCreateDirectoryExW(nullptr, menu_dir.c_str(), nullptr);
		}
		const std::wstring menu_lnk = menu_dir + L"\\HP CLEANER++.lnk";
		if (!FileExistsW(menu_lnk)) {
			CreateShellLink(menu_lnk, exe, dir, L"HP CLEANER++");
			HLOG_INFO("HAppRegistration: created start menu shortcut");
		}

		WriteUninstallRegistry(exe, dir);
		HLOG_INFO("HAppRegistration: ensured uninstall registry entry");

		if (co_owned && SUCCEEDED(co)) {
			::CoUninitialize();
		}
	}

	bool PerformUninstall(const HAppUninstallOptions& opts, std::wstring& out_message)
	{
		const std::wstring exe = GetExePath();
		const std::wstring dir = GetExeDirectory();
		if (exe.empty()) {
			out_message = Hi18n::TrZhWide(u8"無法取得程式路徑。");
			return false;
		}

		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool co_owned = SUCCEEDED(co) || co == RPC_E_CHANGED_MODE;

		if (opts.remove_startup) {
			HAppSettingsSetRunAtStartup(false);
		}

		if (opts.remove_shortcuts) {
			wchar_t desktop[MAX_PATH] = {};
			wchar_t programs[MAX_PATH] = {};
			::SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop);
			::SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, programs);
			RemoveFileIfExists(std::wstring(desktop) + L"\\HP CLEANER++.lnk");
			RemoveDirectoryTree(std::wstring(programs) + L"\\HP CLEANER++");
		}

		RemoveUninstallRegistry();

		if (opts.remove_app_data) {
			const std::string root = HAppPaths::GetAppDataRoot();
			const int wlen = MultiByteToWideChar(CP_UTF8, 0, root.c_str(), -1, nullptr, 0);
			if (wlen > 0) {
				std::vector<wchar_t> wide(static_cast<size_t>(wlen));
				MultiByteToWideChar(CP_UTF8, 0, root.c_str(), -1, wide.data(), wlen);
				RemoveDirectoryTree(wide.data());
			}
		}

		if (opts.remove_program_on_reboot) {
			::MoveFileExW(exe.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
		}

		if (co_owned && SUCCEEDED(co)) {
			::CoUninitialize();
		}

		out_message = Hi18n::TrZhWide(u8"卸載完成。\n");
		if (opts.remove_program_on_reboot) {
			out_message += Hi18n::TrZhWide(u8"程式檔案將於下次重新開機後刪除。\n");
		}
		else {
			out_message += Hi18n::TrZhWide(u8"程式資料夾仍保留：\n");
			out_message += dir;
			out_message += Hi18n::TrZhWide(u8"\n可手動刪除。");
		}
		return true;
	}
}
