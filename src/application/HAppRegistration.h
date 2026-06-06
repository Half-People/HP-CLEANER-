#pragma once
#ifndef HAPP_REGISTRATION_H
#define HAPP_REGISTRATION_H

#include <string>

struct HAppUninstallOptions {
	bool remove_shortcuts = true;
	bool remove_startup = true;
	bool remove_app_data = false;
	bool remove_program_on_reboot = false;
};

namespace HAppRegistration {
	void EnsureRegisteredOnStartup();
	bool PerformUninstall(const HAppUninstallOptions& opts, std::wstring& out_message);
	std::wstring GetInstallDirectory();
}

#endif
