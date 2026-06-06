#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace OptimizeNetworkScan {

	struct AdapterInfo {
		char friendly_name[128] = {};
		char desc_utf8[160] = {};
		char ipv4[48] = {};
		char gateway[48] = {};
		char dns_primary[48] = {};
		bool connected = false;
		uint32_t if_index = 0;
		uint64_t bytes_in = 0;
		uint64_t bytes_out = 0;
		uint64_t link_speed_bps = 0;
		int mtu = 0;
	};

	struct ProcessNetRow {
		uint32_t pid = 0;
		char name_utf8[96] = {};
		char path_utf8[260] = {};
		int tcp_count = 0;
		int udp_count = 0;
		int activity_score = 0;
	};

	struct CustomDnsEntry {
		char label[40] = {};
		char primary[48] = {};
		char secondary[48] = {};
	};

	struct DnsBenchRow {
		char label[40] = {};
		char hint[64] = {};
		char server_ip[48] = {};
		char secondary_ip[48] = {};
		int resolve_ms = -1;
		int ping_ms = -1;
		int apply_provider = -1;
		bool apply_as_custom = false;
		bool is_current = false;
		bool is_fastest = false;
	};

	struct LinkTestRow {
		char name[48] = {};
		char target[64] = {};
		int ping_ms = -1;
		int jitter_ms = -1;
		bool reachable = false;
	};

	struct BandwidthSample {
		float download_bps = 0.f;
		float upload_bps = 0.f;
	};

	struct BandwidthHistory {
		static constexpr int kMaxSamples = 90;
		int count = 0;
		BandwidthSample samples[kMaxSamples] = {};
	};

	struct SpeedHistoryEntry {
		float download_mbps = -1.f;
		float upload_mbps = -1.f;
		uint64_t tested_at_ms = 0;
	};

	struct LinkTestSnapshot {
		bool valid = false;
		bool running = false;
		float progress = 0.f;
		char status_text[128] = {};
		int dns_resolve_ms = -1;
		bool dns_ok = false;
		bool http_ok = false;
		int http_ms = -1;
		std::vector<LinkTestRow> rows;
	};

	struct SpeedTestSnapshot {
		bool valid = false;
		bool running = false;
		bool upload_mode = false;
		float progress = 0.f;
		char status_text[128] = {};
		float download_mbps = -1.f;
		float upload_mbps = -1.f;
		float peak_mbps = -1.f;
		uint64_t bytes_transferred = 0;
		int duration_ms = 0;
	};

	struct DnsBenchSnapshot {
		bool valid = false;
		bool running = false;
		char status_text[96] = {};
		char test_domain[64] = {};
		int best_index = -1;
		std::vector<DnsBenchRow> rows;
	};

	struct FirewallStatus {
		bool domain_enabled = true;
		bool private_enabled = true;
		bool public_enabled = true;
	};

	struct WifiInfo {
		bool connected = false;
		int signal_percent = -1;
		char ssid[64] = {};
		char band[16] = {};
	};

	struct TracerouteHop {
		int hop = 0;
		char addr[48] = {};
		int rtt_ms = -1;
		char status[24] = {};
		bool is_destination = false;
	};

	struct TracerouteSnapshot {
		bool valid = false;
		bool running = false;
		char target[64] = {};
		char status_text[128] = {};
		uint64_t tested_at_ms = 0;
		std::vector<TracerouteHop> hops;
	};

	struct Snapshot {
		bool valid = false;
		bool scanning = false;
		float progress = 0.f;
		char status_text[128] = {};

		char adapter_name[128] = {};
		char ipv4[48] = {};
		char ipv6[48] = {};
		char dns_primary[48] = {};
		char dns_secondary[48] = {};
		char gateway[48] = {};
		char proxy_server[160] = {};
		bool proxy_enabled = false;
		bool internet_reachable = false;
		int gateway_ping_ms = -1;
		int internet_ping_ms = -1;

		float download_bps = -1.f;
		float upload_bps = -1.f;
		uint32_t primary_if_index = 0;
		int total_tcp_connections = 0;
		int total_udp_connections = 0;
		int active_process_count = 0;

		int mtu = 0;
		int packet_loss_percent = -1;
		bool metered_connection = false;
		bool doh_auto_enabled = false;
		bool ipv6_enabled = true;
		bool nic_power_save_on = false;
		bool hosts_has_extra = false;
		int hosts_extra_count = 0;
		bool llmnr_enabled = true;
		bool netbios_over_tcp = true;
		int doh_policy = 0;
		bool parallel_dns_queries = true;
		char dns_suffix[128] = {};
		char dns_search_list[256] = {};

		FirewallStatus firewall;
		WifiInfo wifi;

		std::vector<AdapterInfo> adapters;
		std::vector<ProcessNetRow> processes;
	};

	void Init();
	void Shutdown();
	void RequestRefresh();
	void TickBandwidth();
	Snapshot GetSnapshot();
	bool IsScanning();
	const char* GetLastActionMessage();

	bool RunQuickNetworkRepair();
	bool FlushArpCache();
	bool ResetTcpIpStack();
	bool ReleaseAndRenewIp();
	bool SetAdapterDnsDhcp();
	bool SetAdapterDnsPublic(int provider);
	bool SetAdapterDnsCustom(const char* primary, const char* secondary = nullptr);
	bool AddCustomDnsEntry(const char* label, const char* primary, const char* secondary);
	bool RemoveCustomDnsEntry(int index);
	std::vector<CustomDnsEntry> GetCustomDnsEntries();
	bool PingDiagnostics();
	void RequestDnsBenchmark();
	DnsBenchSnapshot GetDnsBenchmark();
	bool IsDnsBenchmarkRunning();
	void RequestLinkTest();
	LinkTestSnapshot GetLinkTest();
	bool IsLinkTestRunning();
	void RequestSpeedTest();
	void RequestUploadSpeedTest();
	SpeedTestSnapshot GetSpeedTest();
	bool IsSpeedTestRunning();
	BandwidthHistory GetBandwidthHistory();
	std::vector<SpeedHistoryEntry> GetSpeedTestHistory();
	std::string BuildNetworkReportText();

	bool GetFirewallStatus(FirewallStatus& out);
	bool SetFirewallProfileEnabled(int profile, bool enabled);
	bool SetAllFirewallEnabled(bool enabled);
	bool ResetFirewallDefaults();
	bool SetMeteredConnection(bool metered);
	bool SetDnsOverHttpsEnabled(bool enabled);
	bool SetPrimaryIpv6Enabled(bool enabled);
	bool SetNicPowerSaveEnabled(bool enabled);
	void RequestTraceroute();
	TracerouteSnapshot GetTraceroute();
	bool IsTracerouteRunning();
	std::string BuildTracerouteReportText();
	void RequestNetworkDiagnostics();
	bool IsNetworkDiagnosticsRunning();
	int CheckHostsFileExtra();
	bool SetLlmnrEnabled(bool enabled);
	bool SetNetbiosOverTcpEnabled(bool enabled);
	bool SetDohPolicy(int policy);
	bool SetParallelDnsQueriesEnabled(bool enabled);
	void SetDnsBenchmarkDomain(const char* domain);
	const char* GetDnsBenchmarkDomain();
	void RequestFullDnsDiagnostics();
	bool IsFullDnsDiagnosticsRunning();
	void RequestFullNetworkTest();
	bool IsFullNetworkTestRunning();

	void SaveNetworkCache();
	uint64_t GetCacheSavedAtMs();

	bool OpenWindowsNetworkSettings();
	bool OpenNetworkAdapterSettings();
	bool OpenProxySettings();
	bool OpenResourceMonitorNetwork();
	bool OpenFirewallSettings();

}
