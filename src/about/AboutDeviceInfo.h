#pragma once
#ifndef ABOUT_DEVICE_INFO_H
#define ABOUT_DEVICE_INFO_H

struct AboutDeviceInfoSnapshot {
	char os_product[96] = {};
	char os_version[64] = {};
	char os_display_version[64] = {};
	char computer_name[64] = {};
	char user_name[64] = {};
	char cpu_name[256] = {};
	char cpu_topology[48] = {};
	char ram_summary[64] = {};
	char gpu_name[256] = {};
	char system_arch[32] = {};
	char admin_status[32] = {};
	char system_drive[16] = {};
	char machine_id[48] = {};
};

namespace AboutDeviceInfo {
	void Refresh();
	const AboutDeviceInfoSnapshot& Get();
}

#endif
