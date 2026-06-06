#include "OptimizeNetworkScan.h"
#include "OptimizeScan.h"
#include "HCleanTask.h"
#include "HPage.h"
#include "HAppPaths.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wlanapi.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include "Hi18n.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wlanapi.lib")

namespace OptimizeNetworkScan {
	namespace {
		using FnIcmpCreateFile = HANDLE(WINAPI*)();
		using FnIcmpCloseHandle = BOOL(WINAPI*)(HANDLE);
		using FnIcmpSendEcho = DWORD(WINAPI*)(HANDLE, IPAddr, LPVOID, WORD,
			PIP_OPTION_INFORMATION, LPVOID, DWORD, DWORD);

		struct IcmpApi {
			HMODULE dll = nullptr;
			FnIcmpCreateFile create_file = nullptr;
			FnIcmpCloseHandle close_handle = nullptr;
			FnIcmpSendEcho send_echo = nullptr;

			bool EnsureLoaded()
			{
				if (create_file != nullptr) {
					return true;
				}
				dll = LoadLibraryW(L"icmp.dll");
				if (dll == nullptr) {
					return false;
				}
				create_file = reinterpret_cast<FnIcmpCreateFile>(
					GetProcAddress(dll, "IcmpCreateFile"));
				close_handle = reinterpret_cast<FnIcmpCloseHandle>(
					GetProcAddress(dll, "IcmpCloseHandle"));
				send_echo = reinterpret_cast<FnIcmpSendEcho>(
					GetProcAddress(dll, "IcmpSendEcho"));
				if (create_file == nullptr || close_handle == nullptr || send_echo == nullptr) {
					FreeLibrary(dll);
					dll = nullptr;
					return false;
				}
				return true;
			}
		};

		IcmpApi g_icmp;
		std::mutex g_mutex;
		Snapshot g_snapshot;
		std::thread g_worker;
		std::mutex g_dns_mutex;
		DnsBenchSnapshot g_dns_bench;
		std::thread g_dns_worker;
		std::mutex g_custom_dns_mutex;
		std::vector<CustomDnsEntry> g_custom_dns;
		std::mutex g_link_mutex;
		LinkTestSnapshot g_link_test;
		std::thread g_link_worker;
		std::mutex g_speed_mutex;
		SpeedTestSnapshot g_speed_test;
		std::vector<SpeedHistoryEntry> g_speed_history;
		std::thread g_speed_worker;
		std::mutex g_bw_hist_mutex;
		BandwidthHistory g_bw_history;
		std::mutex g_trace_mutex;
		TracerouteSnapshot g_traceroute;
		std::thread g_trace_worker;
		std::atomic<bool> g_trace_running{ false };
		std::thread g_diag_worker;
		std::atomic<bool> g_diag_running{ false };
		std::thread g_full_dns_worker;
		std::atomic<bool> g_full_dns_running{ false };
		std::thread g_full_net_worker;
		std::atomic<bool> g_full_net_running{ false };
		char g_dns_benchmark_domain[64] = "www.msftconnecttest.com";
		char g_last_action[256] = {};
		uint64_t g_cache_saved_at_ms = 0;
		uint64_t g_last_cache_save_ms = 0;

		static int g_bw_write_head = 0;

		static std::string CacheFilePath()
		{
			return HAppPaths::GetConfigDir() + "\\network_cache.json";
		}

		static void PushBandwidthSample(float dl_bps, float ul_bps)
		{
			std::lock_guard<std::mutex> lock(g_bw_hist_mutex);
			g_bw_history.samples[g_bw_write_head].download_bps = dl_bps;
			g_bw_history.samples[g_bw_write_head].upload_bps = ul_bps;
			g_bw_write_head = (g_bw_write_head + 1) % BandwidthHistory::kMaxSamples;
			if (g_bw_history.count < BandwidthHistory::kMaxSamples) {
				++g_bw_history.count;
			}
		}

		static bool IsValidIPv4(const char* ip_utf8)
		{
			if (ip_utf8 == nullptr || ip_utf8[0] == '\0') {
				return false;
			}
			IN_ADDR addr = {};
			return InetPtonA(AF_INET, ip_utf8, &addr) == 1;
		}

		uint64_t g_last_bytes_in = 0;
		uint64_t g_last_bytes_out = 0;
		uint64_t g_last_sample_ms = 0;
		uint32_t g_tracked_if_index = 0;

		void SetLastAction(const char* msg)
		{
			if (msg == nullptr) {
				g_last_action[0] = '\0';
				return;
			}
			strncpy_s(g_last_action, msg, _TRUNCATE);
		}

		static bool RunHiddenCommand(const wchar_t* cmdline)
		{
			if (cmdline == nullptr || cmdline[0] == L'\0') {
				return false;
			}
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			PROCESS_INFORMATION pi = {};
			std::wstring cmd = cmdline;
			std::vector<wchar_t> buf(cmd.begin(), cmd.end());
			buf.push_back(L'\0');
			if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
				nullptr, nullptr, &si, &pi)) {
				return false;
			}
			WaitForSingleObject(pi.hProcess, 120000);
			DWORD exit_code = 1;
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return exit_code == 0;
		}

		static bool RunHiddenCommandCapture(const wchar_t* cmdline, std::string& out_utf8)
		{
			out_utf8.clear();
			if (cmdline == nullptr || cmdline[0] == L'\0') {
				return false;
			}
			SECURITY_ATTRIBUTES sa = {};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;
			HANDLE read_pipe = nullptr;
			HANDLE write_pipe = nullptr;
			if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
				return false;
			}
			SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
			si.wShowWindow = SW_HIDE;
			si.hStdOutput = write_pipe;
			si.hStdError = write_pipe;
			PROCESS_INFORMATION pi = {};
			std::wstring cmd = cmdline;
			std::vector<wchar_t> buf(cmd.begin(), cmd.end());
			buf.push_back(L'\0');
			const BOOL created = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
			CloseHandle(write_pipe);
			if (!created) {
				CloseHandle(read_pipe);
				return false;
			}
			char chunk[4096] = {};
			DWORD read_bytes = 0;
			for (;;) {
				if (!ReadFile(read_pipe, chunk, sizeof(chunk), &read_bytes, nullptr) || read_bytes == 0) {
					break;
				}
				out_utf8.append(chunk, chunk + read_bytes);
			}
			WaitForSingleObject(pi.hProcess, 120000);
			DWORD exit_code = 1;
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			CloseHandle(read_pipe);
			return exit_code == 0;
		}

		static bool OutputContainsOn(const std::string& text)
		{
			return text.find("ON") != std::string::npos
				|| text.find("On") != std::string::npos
				|| text.find(I18N(u8"開啟")) != std::string::npos
				|| text.find(I18N(u8"启用")) != std::string::npos;
		}

		static void LoadNetworkCacheInternal()
		{
			const std::string path = CacheFilePath();
			std::ifstream in(path);
			if (!in.is_open()) {
				return;
			}
			try {
				nlohmann::json j;
				in >> j;
				{
					std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
					g_custom_dns.clear();
					if (j.contains("custom_dns") && j["custom_dns"].is_array()) {
						for (const auto& item : j["custom_dns"]) {
							CustomDnsEntry e = {};
							strncpy_s(e.label, item.value("label", u8"自訂 DNS").c_str(), _TRUNCATE);
							strncpy_s(e.primary, item.value("primary", "").c_str(), _TRUNCATE);
							strncpy_s(e.secondary, item.value("secondary", "").c_str(), _TRUNCATE);
							if (e.primary[0] != '\0') {
								g_custom_dns.push_back(e);
							}
						}
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_speed_mutex);
					g_speed_history.clear();
					if (j.contains("speed_history") && j["speed_history"].is_array()) {
						for (const auto& item : j["speed_history"]) {
							SpeedHistoryEntry e = {};
							e.download_mbps = item.value("download_mbps", -1.f);
							e.upload_mbps = item.value("upload_mbps", -1.f);
							e.tested_at_ms = item.value("tested_at_ms", static_cast<uint64_t>(0));
							g_speed_history.push_back(e);
						}
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_bw_hist_mutex);
					g_bw_history = {};
					g_bw_write_head = 0;
					if (j.contains("bandwidth") && j["bandwidth"].is_object()) {
						const auto& bw = j["bandwidth"];
						g_bw_history.count = std::min(bw.value("count", 0),
							BandwidthHistory::kMaxSamples);
						if (bw.contains("samples") && bw["samples"].is_array()) {
							const int n = std::min(g_bw_history.count,
								static_cast<int>(bw["samples"].size()));
							for (int i = 0; i < n; ++i) {
								g_bw_history.samples[i].download_bps =
									bw["samples"][i].value("download_bps", 0.f);
								g_bw_history.samples[i].upload_bps =
									bw["samples"][i].value("upload_bps", 0.f);
							}
							g_bw_write_head = n % BandwidthHistory::kMaxSamples;
						}
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_dns_mutex);
					if (j.contains("dns_bench") && j["dns_bench"].is_object()) {
						const auto& db = j["dns_bench"];
						g_dns_bench.valid = db.value("valid", false);
						strncpy_s(g_dns_bench.status_text,
							db.value("status_text", "").c_str(), _TRUNCATE);
						g_dns_bench.rows.clear();
						if (db.contains("rows") && db["rows"].is_array()) {
							for (const auto& row : db["rows"]) {
								DnsBenchRow r = {};
								strncpy_s(r.label, row.value("label", "").c_str(), _TRUNCATE);
								strncpy_s(r.server_ip, row.value("server_ip", "").c_str(), _TRUNCATE);
								r.resolve_ms = row.value("resolve_ms", -1);
								r.is_fastest = row.value("is_fastest", false);
								r.is_current = row.value("is_current", false);
								g_dns_bench.rows.push_back(r);
							}
						}
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_link_mutex);
					if (j.contains("link_test") && j["link_test"].is_object()) {
						const auto& lt = j["link_test"];
						g_link_test.valid = lt.value("valid", false);
						strncpy_s(g_link_test.status_text,
							lt.value("status_text", "").c_str(), _TRUNCATE);
						g_link_test.dns_resolve_ms = lt.value("dns_resolve_ms", -1);
						g_link_test.dns_ok = lt.value("dns_ok", false);
						g_link_test.http_ok = lt.value("http_ok", false);
						g_link_test.http_ms = lt.value("http_ms", -1);
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_speed_mutex);
					if (j.contains("speed_test") && j["speed_test"].is_object()) {
						const auto& st = j["speed_test"];
						g_speed_test.valid = st.value("valid", false);
						strncpy_s(g_speed_test.status_text,
							st.value("status_text", "").c_str(), _TRUNCATE);
						g_speed_test.download_mbps = st.value("download_mbps", -1.f);
						g_speed_test.upload_mbps = st.value("upload_mbps", -1.f);
						g_speed_test.peak_mbps = st.value("peak_mbps", -1.f);
					}
				}
				{
					std::lock_guard<std::mutex> lock(g_trace_mutex);
					g_traceroute = {};
					if (j.contains("traceroute") && j["traceroute"].is_object()) {
						const auto& tr = j["traceroute"];
						g_traceroute.valid = tr.value("valid", false);
						strncpy_s(g_traceroute.target, tr.value("target", "").c_str(), _TRUNCATE);
						strncpy_s(g_traceroute.status_text,
							tr.value("status_text", "").c_str(), _TRUNCATE);
						g_traceroute.tested_at_ms = tr.value("tested_at_ms", static_cast<uint64_t>(0));
						if (tr.contains("hops") && tr["hops"].is_array()) {
							for (const auto& hop : tr["hops"]) {
								TracerouteHop h = {};
								h.hop = hop.value("hop", 0);
								strncpy_s(h.addr, hop.value("addr", "").c_str(), _TRUNCATE);
								h.rtt_ms = hop.value("rtt_ms", -1);
								strncpy_s(h.status, hop.value("status", "").c_str(), _TRUNCATE);
								h.is_destination = hop.value("is_destination", false);
								g_traceroute.hops.push_back(h);
							}
						}
					}
					else if (j.contains("traceroute") && j["traceroute"].is_array()) {
						g_traceroute.valid = true;
						strncpy_s(g_traceroute.target, "1.1.1.1", _TRUNCATE);
						for (const auto& hop : j["traceroute"]) {
							TracerouteHop h = {};
							h.hop = hop.value("hop", 0);
							strncpy_s(h.addr, hop.value("addr", "").c_str(), _TRUNCATE);
							h.rtt_ms = hop.value("rtt_ms", -1);
							g_traceroute.hops.push_back(h);
						}
					}
				}
				g_cache_saved_at_ms = j.value("saved_at_ms", static_cast<uint64_t>(0));
				if (j.contains("dns_benchmark_domain") && j["dns_benchmark_domain"].is_string()) {
					strncpy_s(g_dns_benchmark_domain,
						j["dns_benchmark_domain"].get<std::string>().c_str(), _TRUNCATE);
				}
			}
			catch (...) {
			}
		}

		static void SaveNetworkCacheInternal()
		{
			try {
			HAppPaths::EnsureAppDataDirs();
			nlohmann::json j;
			j["saved_at_ms"] = GetTickCount64();
			{
				std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
				nlohmann::json arr = nlohmann::json::array();
				for (const auto& e : g_custom_dns) {
					arr.push_back({
						{ "label", e.label },
						{ "primary", e.primary },
						{ "secondary", e.secondary },
					});
				}
				j["custom_dns"] = arr;
			}
			{
				std::lock_guard<std::mutex> lock(g_speed_mutex);
				nlohmann::json arr = nlohmann::json::array();
				for (const auto& e : g_speed_history) {
					arr.push_back({
						{ "download_mbps", e.download_mbps },
						{ "upload_mbps", e.upload_mbps },
						{ "tested_at_ms", e.tested_at_ms },
					});
				}
				j["speed_history"] = arr;
				j["speed_test"] = {
					{ "valid", g_speed_test.valid },
					{ "status_text", g_speed_test.status_text },
					{ "download_mbps", g_speed_test.download_mbps },
					{ "upload_mbps", g_speed_test.upload_mbps },
					{ "peak_mbps", g_speed_test.peak_mbps },
				};
			}
			{
				std::lock_guard<std::mutex> lock(g_bw_hist_mutex);
				nlohmann::json samples = nlohmann::json::array();
				const BandwidthHistory ordered = [&]() {
					BandwidthHistory h = {};
					h.count = g_bw_history.count;
					if (g_bw_history.count <= 0) {
						return h;
					}
					const int start = (g_bw_history.count < BandwidthHistory::kMaxSamples)
						? 0 : g_bw_write_head;
					for (int i = 0; i < g_bw_history.count; ++i) {
						const int src = (start + i) % BandwidthHistory::kMaxSamples;
						h.samples[i] = g_bw_history.samples[src];
					}
					return h;
				}();
				for (int i = 0; i < ordered.count; ++i) {
					samples.push_back({
						{ "download_bps", ordered.samples[i].download_bps },
						{ "upload_bps", ordered.samples[i].upload_bps },
					});
				}
				j["bandwidth"] = {
					{ "count", ordered.count },
					{ "samples", samples },
				};
			}
			{
				std::lock_guard<std::mutex> lock(g_dns_mutex);
				nlohmann::json rows = nlohmann::json::array();
				for (const auto& r : g_dns_bench.rows) {
					rows.push_back({
						{ "label", r.label },
						{ "server_ip", r.server_ip },
						{ "resolve_ms", r.resolve_ms },
						{ "is_fastest", r.is_fastest },
						{ "is_current", r.is_current },
					});
				}
				j["dns_bench"] = {
					{ "valid", g_dns_bench.valid },
					{ "status_text", g_dns_bench.status_text },
					{ "rows", rows },
				};
			}
			{
				std::lock_guard<std::mutex> lock(g_link_mutex);
				j["link_test"] = {
					{ "valid", g_link_test.valid },
					{ "status_text", g_link_test.status_text },
					{ "dns_resolve_ms", g_link_test.dns_resolve_ms },
					{ "dns_ok", g_link_test.dns_ok },
					{ "http_ok", g_link_test.http_ok },
					{ "http_ms", g_link_test.http_ms },
				};
			}
			{
				std::lock_guard<std::mutex> lock(g_trace_mutex);
				nlohmann::json hops = nlohmann::json::array();
				for (const auto& h : g_traceroute.hops) {
					hops.push_back({
						{ "hop", h.hop },
						{ "addr", h.addr },
						{ "rtt_ms", h.rtt_ms },
						{ "status", h.status },
						{ "is_destination", h.is_destination },
					});
				}
				j["traceroute"] = {
					{ "valid", g_traceroute.valid },
					{ "target", g_traceroute.target },
					{ "status_text", g_traceroute.status_text },
					{ "tested_at_ms", g_traceroute.tested_at_ms },
					{ "hops", hops },
				};
			}
			j["dns_benchmark_domain"] = g_dns_benchmark_domain;
			try {
				const std::string path = CacheFilePath();
				std::ofstream out(path, std::ios::trunc);
				if (out.is_open()) {
					out << j.dump(2);
					g_cache_saved_at_ms = j["saved_at_ms"].get<uint64_t>();
				}
			}
			catch (...) {
			}
			}
			catch (...) {
			}
		}

		static void WideToUtf8(const wchar_t* wide, char* out, size_t out_sz)
		{
			if (out == nullptr || out_sz == 0) {
				return;
			}
			out[0] = '\0';
			if (wide == nullptr || wide[0] == L'\0') {
				return;
			}
			const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out,
				static_cast<int>(out_sz), nullptr, nullptr);
			if (n <= 0) {
				return;
			}
			out[out_sz - 1] = '\0';
		}

		static int PingHostIPv4(const char* ip_utf8, DWORD timeout_ms = 1200)
		{
			if (ip_utf8 == nullptr || ip_utf8[0] == '\0' || !g_icmp.EnsureLoaded()) {
				return -1;
			}
			IN_ADDR addr = {};
			if (InetPtonA(AF_INET, ip_utf8, &addr) != 1) {
				return -1;
			}
			const HANDLE icmp = g_icmp.create_file();
			if (icmp == INVALID_HANDLE_VALUE) {
				return -1;
			}
			char send_buf[32] = { 'H', 'P' };
			DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + sizeof(send_buf) + 8;
			std::vector<char> reply(reply_size);
			const DWORD sent = g_icmp.send_echo(icmp, addr.S_un.S_addr, send_buf, sizeof(send_buf),
				nullptr, reply.data(), reply_size, timeout_ms);
			g_icmp.close_handle(icmp);
			if (sent == 0) {
				return -1;
			}
			const auto* echo = reinterpret_cast<PICMP_ECHO_REPLY>(reply.data());
			if (echo->Status != IP_SUCCESS) {
				return -1;
			}
			return static_cast<int>(echo->RoundTripTime);
		}

		static int PingHostname(const char* host_utf8, DWORD timeout_ms = 1200)
		{
			if (host_utf8 == nullptr || host_utf8[0] == '\0') {
				return -1;
			}
			addrinfo hints = {};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* res = nullptr;
			if (getaddrinfo(host_utf8, nullptr, &hints, &res) != 0 || res == nullptr) {
				return -1;
			}
			char ip[48] = {};
			const auto* sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
			InetNtopA(AF_INET, &sa->sin_addr, ip, sizeof(ip));
			freeaddrinfo(res);
			return PingHostIPv4(ip, timeout_ms);
		}

		static void PingWithJitter(const char* target, bool hostname,
			int& out_avg_ms, int& out_jitter_ms)
		{
			out_avg_ms = -1;
			out_jitter_ms = -1;
			int samples[3] = { -1, -1, -1 };
			for (int i = 0; i < 3; ++i) {
				samples[i] = hostname ? PingHostname(target, 1500) : PingHostIPv4(target, 1500);
			}
			int valid[3] = {};
			int valid_count = 0;
			for (int i = 0; i < 3; ++i) {
				if (samples[i] >= 0) {
					valid[valid_count++] = samples[i];
				}
			}
			if (valid_count == 0) {
				return;
			}
			int sum = 0;
			int vmin = valid[0];
			int vmax = valid[0];
			for (int i = 0; i < valid_count; ++i) {
				sum += valid[i];
				vmin = std::min(vmin, valid[i]);
				vmax = std::max(vmax, valid[i]);
			}
			out_avg_ms = sum / valid_count;
			out_jitter_ms = (valid_count > 1) ? (vmax - vmin) : 0;
		}

		static bool PingHostWithTtlEx(const char* ip_utf8, int ttl, int& out_rtt_ms,
			char* out_hop_ip, size_t out_hop_ip_sz, bool& out_is_dest, DWORD timeout_ms = 900)
		{
			out_rtt_ms = -1;
			out_is_dest = false;
			if (out_hop_ip != nullptr && out_hop_ip_sz > 0) {
				out_hop_ip[0] = '\0';
			}
			if (ip_utf8 == nullptr || ip_utf8[0] == '\0' || ttl <= 0 || !g_icmp.EnsureLoaded()) {
				return false;
			}
			IN_ADDR addr = {};
			if (InetPtonA(AF_INET, ip_utf8, &addr) != 1) {
				return false;
			}
			const HANDLE icmp = g_icmp.create_file();
			if (icmp == INVALID_HANDLE_VALUE) {
				return false;
			}
			char send_buf[32] = { 'H', 'P' };
			IP_OPTION_INFORMATION opts = {};
			opts.Ttl = static_cast<UCHAR>(ttl);
			DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + sizeof(send_buf) + 8;
			std::vector<char> reply(reply_size);
			const DWORD sent = g_icmp.send_echo(icmp, addr.S_un.S_addr, send_buf, sizeof(send_buf),
				&opts, reply.data(), reply_size, timeout_ms);
			g_icmp.close_handle(icmp);
			if (sent == 0) {
				return false;
			}
			const auto* echo = reinterpret_cast<PICMP_ECHO_REPLY>(reply.data());
			if (echo->Status == IP_SUCCESS) {
				out_rtt_ms = static_cast<int>(echo->RoundTripTime);
				out_is_dest = true;
				if (out_hop_ip != nullptr && out_hop_ip_sz > 0) {
					IN_ADDR hop = {};
					hop.S_un.S_addr = echo->Address;
					InetNtopA(AF_INET, &hop, out_hop_ip, static_cast<DWORD>(out_hop_ip_sz));
				}
				return true;
			}
			if (echo->Status == IP_TTL_EXPIRED_TRANSIT) {
				out_rtt_ms = static_cast<int>(echo->RoundTripTime);
				if (out_hop_ip != nullptr && out_hop_ip_sz > 0) {
					IN_ADDR hop = {};
					hop.S_un.S_addr = echo->Address;
					InetNtopA(AF_INET, &hop, out_hop_ip, static_cast<DWORD>(out_hop_ip_sz));
				}
				return true;
			}
			return false;
		}

		static void SanitizePrintableUtf8(char* text, size_t text_sz)
		{
			if (text == nullptr || text_sz == 0) {
				return;
			}
			for (size_t i = 0; text[i] != '\0' && i < text_sz; ++i) {
				const unsigned char c = static_cast<unsigned char>(text[i]);
				if (c < 0x20 || c == 0x7F) {
					text[i] = '?';
				}
			}
		}

		static int MeasurePacketLossPercent(const char* ip_utf8)
		{
			if (ip_utf8 == nullptr || ip_utf8[0] == '\0') {
				return -1;
			}
			int received = 0;
			constexpr int kSent = 4;
			for (int i = 0; i < kSent; ++i) {
				if (PingHostIPv4(ip_utf8, 1200) >= 0) {
					++received;
				}
			}
			return ((kSent - received) * 100) / kSent;
		}

		static void QueryFirewallStatus(Snapshot& snap)
		{
			struct ProfileDef {
				const wchar_t* name;
				bool* enabled;
			};
			ProfileDef profiles[] = {
				{ L"domainprofile", &snap.firewall.domain_enabled },
				{ L"privateprofile", &snap.firewall.private_enabled },
				{ L"publicprofile", &snap.firewall.public_enabled },
			};
			for (const auto& p : profiles) {
				wchar_t cmd[160] = {};
				_snwprintf_s(cmd, _TRUNCATE, L"netsh.exe advfirewall show %s state", p.name);
				std::string out;
				if (RunHiddenCommandCapture(cmd, out)) {
					*p.enabled = OutputContainsOn(out);
				}
			}
		}

		static void QueryWifiInfo(Snapshot& snap)
		{
			HANDLE client = nullptr;
			DWORD version = 0;
			if (WlanOpenHandle(2, nullptr, &version, &client) != ERROR_SUCCESS) {
				return;
			}
			PWLAN_INTERFACE_INFO_LIST if_list = nullptr;
			if (WlanEnumInterfaces(client, nullptr, &if_list) != ERROR_SUCCESS || if_list == nullptr) {
				WlanCloseHandle(client, nullptr);
				return;
			}
			for (ULONG i = 0; i < if_list->dwNumberOfItems; ++i) {
				const auto& iface = if_list->InterfaceInfo[i];
				if (iface.isState != wlan_interface_state_connected) {
					continue;
				}
				PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
				DWORD attr_size = 0;
				WLAN_OPCODE_VALUE_TYPE op_type = wlan_opcode_value_type_invalid;
				if (WlanQueryInterface(client, &iface.InterfaceGuid,
					wlan_intf_opcode_current_connection, nullptr, &attr_size,
					reinterpret_cast<PVOID*>(&attrs), &op_type) != ERROR_SUCCESS
					|| attrs == nullptr) {
					continue;
				}
				snap.wifi.connected = true;
				snap.wifi.signal_percent = static_cast<int>(attrs->wlanAssociationAttributes.wlanSignalQuality);
				const auto& ssid = attrs->wlanAssociationAttributes.dot11Ssid;
				if (ssid.uSSIDLength > 0) {
					const int copy_len = std::min(static_cast<int>(ssid.uSSIDLength), 63);
					memcpy(snap.wifi.ssid, ssid.ucSSID, static_cast<size_t>(copy_len));
					snap.wifi.ssid[copy_len] = '\0';
					SanitizePrintableUtf8(snap.wifi.ssid, sizeof(snap.wifi.ssid));
				}
				const DOT11_PHY_TYPE phy = attrs->wlanAssociationAttributes.dot11PhyType;
				if (phy == dot11_phy_type_vht || phy == dot11_phy_type_he) {
					strncpy_s(snap.wifi.band, "5G", _TRUNCATE);
				}
				else if (phy == dot11_phy_type_ht || phy == dot11_phy_type_erp) {
					strncpy_s(snap.wifi.band, "2.4G", _TRUNCATE);
				}
				else {
					strncpy_s(snap.wifi.band, "Wi-Fi", _TRUNCATE);
				}
				WlanFreeMemory(attrs);
				break;
			}
			WlanFreeMemory(if_list);
			WlanCloseHandle(client, nullptr);
		}

		static bool QueryRegistryDword(HKEY root, const wchar_t* subkey, const wchar_t* value,
			DWORD& out)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
				return false;
			}
			DWORD size = sizeof(out);
			DWORD type = 0;
			const LONG err = RegQueryValueExW(key, value, nullptr, &type,
				reinterpret_cast<LPBYTE>(&out), &size);
			RegCloseKey(key);
			return err == ERROR_SUCCESS && type == REG_DWORD;
		}

		static int CheckHostsFileExtraInternal()
		{
			const wchar_t* path = L"C:\\Windows\\System32\\drivers\\etc\\hosts";
			std::ifstream in(path);
			if (!in.is_open()) {
				return 0;
			}
			int extra = 0;
			std::string line;
			while (std::getline(in, line)) {
				size_t start = 0;
				while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
					++start;
				}
				if (start >= line.size() || line[start] == '#') {
					continue;
				}
				if (line.find("127.0.0.1") != std::string::npos
					|| line.find("::1") != std::string::npos
					|| line.find("localhost") != std::string::npos) {
					continue;
				}
				++extra;
			}
			return extra;
		}

		static void QueryDnsRegistrySettings(Snapshot& snap)
		{
			DWORD doh = 0;
			if (QueryRegistryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
				L"EnableAutoDoh", doh)) {
				snap.doh_auto_enabled = (doh != 0);
			}
			DWORD llmnr = 1;
			if (QueryRegistryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
				L"EnableMulticast", llmnr)) {
				snap.llmnr_enabled = (llmnr != 0);
			}
			DWORD doh_policy = 0;
			if (QueryRegistryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
				L"DohPolicy", doh_policy)) {
				snap.doh_policy = static_cast<int>(doh_policy);
			}
			DWORD parallel = 0;
			if (QueryRegistryDword(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
				L"DisableParallelAorAAAA", parallel)) {
				snap.parallel_dns_queries = (parallel == 0);
			}
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
				0, KEY_READ, &key) == ERROR_SUCCESS) {
				wchar_t search[512] = {};
				DWORD sz = sizeof(search);
				DWORD type = 0;
				if (RegQueryValueExW(key, L"SearchList", nullptr, &type,
					reinterpret_cast<LPBYTE>(search), &sz) == ERROR_SUCCESS && type == REG_SZ) {
					WideToUtf8(search, snap.dns_search_list, sizeof(snap.dns_search_list));
				}
				RegCloseKey(key);
			}
		}

		static void QueryDnsSuffixFromAdapter(Snapshot& snap)
		{
			ULONG buf_len = 0;
			if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				nullptr, nullptr, &buf_len) != ERROR_BUFFER_OVERFLOW || buf_len == 0) {
				return;
			}
			std::vector<uint8_t> buffer(buf_len);
			auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
			if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				nullptr, addrs, &buf_len) != NO_ERROR) {
				return;
			}
			for (auto* a = addrs; a != nullptr; a = a->Next) {
				if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
					continue;
				}
				if (snap.primary_if_index > 0 && a->IfIndex != snap.primary_if_index) {
					continue;
				}
				if (a->DnsSuffix != nullptr && a->DnsSuffix[0] != L'\0') {
					WideToUtf8(a->DnsSuffix, snap.dns_suffix, sizeof(snap.dns_suffix));
				}
				break;
			}
		}

		static void QueryExtendedNetworkSettings(Snapshot& snap)
		{
			QueryDnsRegistrySettings(snap);

			if (snap.primary_if_index > 0) {
				std::string ps_out;
				wchar_t cmd[420] = {};
				_snwprintf_s(cmd, _TRUNCATE,
					L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
					L"\"$p=Get-NetConnectionProfile -InterfaceIndex %u -ErrorAction SilentlyContinue;"
					L"if($p){$p.NetworkCost}else{'Unrestricted'}\"",
					snap.primary_if_index);
				if (RunHiddenCommandCapture(cmd, ps_out)) {
					snap.metered_connection = (ps_out.find("Variable") != std::string::npos);
				}

				wchar_t pwr_cmd[420] = {};
				_snwprintf_s(pwr_cmd, _TRUNCATE,
					L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
					L"\"$a=Get-NetAdapterPowerManagement -InterfaceIndex %u -ErrorAction SilentlyContinue;"
					L"if($a){$a.AllowComputerToTurnOffDevice}else{'Enabled'}\"",
					snap.primary_if_index);
				std::string nic_out;
				if (RunHiddenCommandCapture(pwr_cmd, nic_out)) {
					snap.nic_power_save_on = (nic_out.find("Disabled") == std::string::npos
						&& nic_out.find("No") == std::string::npos);
				}
			}

			if (snap.adapter_name[0] != '\0') {
				wchar_t wname[128] = {};
				MultiByteToWideChar(CP_UTF8, 0, snap.adapter_name, -1, wname, 128);
				wchar_t cmd6[256] = {};
				_snwprintf_s(cmd6, _TRUNCATE,
					L"netsh.exe interface ipv6 show interface \"%s\"", wname);
				std::string ipv6_out;
				if (RunHiddenCommandCapture(cmd6, ipv6_out)) {
					snap.ipv6_enabled = (ipv6_out.find("disabled") == std::string::npos
						&& ipv6_out.find(I18N(u8"停用")) == std::string::npos);
				}
			}

			if (snap.primary_if_index > 0) {
				wchar_t nb_cmd[420] = {};
				_snwprintf_s(nb_cmd, _TRUNCATE,
					L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
					L"\"$b=Get-NetAdapterBinding -InterfaceIndex %u -ComponentID ms_netbios "
					L"-ErrorAction SilentlyContinue;if($b){$b.Enabled}else{$false}\"",
					snap.primary_if_index);
				std::string nb_out;
				if (RunHiddenCommandCapture(nb_cmd, nb_out)) {
					snap.netbios_over_tcp = (nb_out.find("True") != std::string::npos);
				}
			}

			const int extra = CheckHostsFileExtraInternal();
			snap.hosts_extra_count = extra;
			snap.hosts_has_extra = (extra > 0);
		}

		static void RunTracerouteWorker()
		{
			g_trace_running = true;
			TracerouteSnapshot result = {};
			result.running = true;
			const Snapshot snap = GetSnapshot();
			const char* target = snap.gateway[0] != '\0' ? snap.gateway : "1.1.1.1";
			strncpy_s(result.target, target, _TRUNCATE);
			for (int ttl = 1; ttl <= 20; ++ttl) {
				TracerouteHop hop = {};
				hop.hop = ttl;
				bool is_dest = false;
				int rtt = -1;
				if (PingHostWithTtlEx(target, ttl, rtt, hop.addr, sizeof(hop.addr), is_dest, 1100)) {
					hop.rtt_ms = rtt;
					hop.is_destination = is_dest;
					strncpy_s(hop.status, is_dest ? I18N(u8"到達") : I18N(u8"轉發"), _TRUNCATE);
				}
				else {
					strncpy_s(hop.status, u8"逾時", _TRUNCATE);
					strncpy_s(hop.addr, "*", _TRUNCATE);
				}
				result.hops.push_back(hop);
				if (is_dest) {
					break;
				}
			}
			result.valid = !result.hops.empty();
			result.running = false;
			result.tested_at_ms = GetTickCount64();
			snprintf(result.status_text, sizeof(result.status_text),
				I18N(u8"追蹤 %s · %zu 跳"), result.target, result.hops.size());
			{
				std::lock_guard<std::mutex> lock(g_trace_mutex);
				g_traceroute = std::move(result);
			}
			SaveNetworkCacheInternal();
			g_trace_running = false;
		}

		static void RunNetworkDiagnosticsWorker()
		{
			g_diag_running = true;
			const int loss = MeasurePacketLossPercent("1.1.1.1");
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.packet_loss_percent = loss;
			}
			Snapshot snap = GetSnapshot();
			QueryExtendedNetworkSettings(snap);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.metered_connection = snap.metered_connection;
				g_snapshot.nic_power_save_on = snap.nic_power_save_on;
				g_snapshot.ipv6_enabled = snap.ipv6_enabled;
				g_snapshot.llmnr_enabled = snap.llmnr_enabled;
				g_snapshot.netbios_over_tcp = snap.netbios_over_tcp;
				g_snapshot.doh_policy = snap.doh_policy;
				g_snapshot.doh_auto_enabled = snap.doh_auto_enabled;
				g_snapshot.parallel_dns_queries = snap.parallel_dns_queries;
				strncpy_s(g_snapshot.dns_suffix, snap.dns_suffix, _TRUNCATE);
				strncpy_s(g_snapshot.dns_search_list, snap.dns_search_list, _TRUNCATE);
				g_snapshot.hosts_has_extra = snap.hosts_has_extra;
				g_snapshot.hosts_extra_count = snap.hosts_extra_count;
			}
			g_diag_running = false;
		}

		static bool EncodeDnsName(const char* host, std::vector<uint8_t>& out)
		{
			if (host == nullptr || host[0] == '\0') {
				return false;
			}
			const char* part = host;
			while (part[0] != '\0') {
				const char* dot = strchr(part, '.');
				const size_t len = dot != nullptr
					? static_cast<size_t>(dot - part) : strlen(part);
				if (len == 0 || len > 63) {
					return false;
				}
				out.push_back(static_cast<uint8_t>(len));
				for (size_t i = 0; i < len; ++i) {
					out.push_back(static_cast<uint8_t>(part[i]));
				}
				if (dot == nullptr) {
					break;
				}
				part = dot + 1;
			}
			out.push_back(0);
			return true;
		}

		static int DnsResolveMs(const char* dns_ip_utf8, const char* host_utf8, DWORD timeout_ms = 1800)
		{
			if (dns_ip_utf8 == nullptr || dns_ip_utf8[0] == '\0'
				|| host_utf8 == nullptr || host_utf8[0] == '\0') {
				return -1;
			}
			SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (sock == INVALID_SOCKET) {
				return -1;
			}
			const DWORD tv = timeout_ms;
			setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
			setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

			sockaddr_in dns_addr = {};
			dns_addr.sin_family = AF_INET;
			dns_addr.sin_port = htons(53);
			if (InetPtonA(AF_INET, dns_ip_utf8, &dns_addr.sin_addr) != 1) {
				closesocket(sock);
				return -1;
			}

			std::vector<uint8_t> packet;
			packet.reserve(128);
			const uint16_t txn_id = static_cast<uint16_t>(GetTickCount() & 0xFFFF);
			packet.push_back(static_cast<uint8_t>((txn_id >> 8) & 0xFF));
			packet.push_back(static_cast<uint8_t>(txn_id & 0xFF));
			packet.push_back(0x01);
			packet.push_back(0x00);
			packet.push_back(0x00);
			packet.push_back(0x01);
			for (int i = 0; i < 6; ++i) {
				packet.push_back(0);
			}
			if (!EncodeDnsName(host_utf8, packet)) {
				closesocket(sock);
				return -1;
			}
			packet.push_back(0x00);
			packet.push_back(0x01);
			packet.push_back(0x00);
			packet.push_back(0x01);

			const uint64_t t0 = GetTickCount64();
			if (sendto(sock, reinterpret_cast<const char*>(packet.data()),
				static_cast<int>(packet.size()),
				0, reinterpret_cast<sockaddr*>(&dns_addr), sizeof(dns_addr)) == SOCKET_ERROR) {
				closesocket(sock);
				return -1;
			}

			char recv_buf[512] = {};
			const int recv_len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, nullptr, nullptr);
			const uint64_t t1 = GetTickCount64();
			closesocket(sock);
			if (recv_len < 12) {
				return -1;
			}
			const uint16_t rid = static_cast<uint16_t>(
				(static_cast<uint8_t>(recv_buf[0]) << 8) | static_cast<uint8_t>(recv_buf[1]));
			if (rid != txn_id) {
				return -1;
			}
			const uint8_t rcode = static_cast<uint8_t>(recv_buf[3]) & 0x0F;
			if (rcode != 0) {
				return -1;
			}
			return static_cast<int>(t1 - t0);
		}

		struct DnsCandidate {
			char label[40] = {};
			char hint[64] = {};
			char ip[48] = {};
			char secondary[48] = {};
			int provider = -1;
			bool is_current = false;
			bool apply_custom = false;
		};

		static bool DnsIpAlreadyListed(const std::vector<DnsCandidate>& list, const char* ip)
		{
			for (const auto& c : list) {
				if (strcmp(c.ip, ip) == 0) {
					return true;
				}
			}
			return false;
		}

		static void RunDnsBenchmarkWorker()
		{
			Snapshot snap = GetSnapshot();
			DnsBenchSnapshot bench = {};
			bench.running = true;
			strncpy_s(bench.status_text, u8"正在測試 DNS 速度…", _TRUNCATE);
			strncpy_s(bench.test_domain, g_dns_benchmark_domain, _TRUNCATE);
			if (bench.test_domain[0] == '\0') {
				strncpy_s(bench.test_domain, "www.msftconnecttest.com", _TRUNCATE);
			}
			{
				std::lock_guard<std::mutex> lock(g_dns_mutex);
				g_dns_bench = bench;
			}

			std::vector<DnsCandidate> list;
			if (snap.dns_primary[0] != '\0') {
				DnsCandidate cur = {};
				strncpy_s(cur.label, u8"目前使用", _TRUNCATE);
				strncpy_s(cur.hint, u8"你電腦正在用的 DNS", _TRUNCATE);
				strncpy_s(cur.ip, snap.dns_primary, _TRUNCATE);
				cur.is_current = true;
				list.push_back(cur);
			}
			const DnsCandidate extras[] = {
				{ "Cloudflare", u8"國際 · 隱私佳", "1.1.1.1", {}, 1, false, false },
				{ "Google", u8"國際 · 穩定", "8.8.8.8", {}, 2, false, false },
				{ u8"阿里 DNS", u8"國內 · 速度快", "223.5.5.5", {}, 3, false, false },
				{ "114 DNS", u8"國內 · 公共 DNS", "114.114.114.114", {}, 4, false, false },
				{ u8"騰訊 DNS", u8"國內 · 遊戲常用", "119.29.29.29", {}, 5, false, false },
				{ "Quad9", u8"國際 · 安全過濾", "9.9.9.9", {}, 6, false, false },
				{ "OpenDNS", u8"國際 · Cisco", "208.67.222.222", {}, 7, false, false },
				{ "AdGuard", u8"國際 · 廣告過濾", "94.140.14.14", {}, 8, false, false },
			};
			for (const auto& ex : extras) {
				if (!DnsIpAlreadyListed(list, ex.ip)) {
					list.push_back(ex);
				}
			}
			{
				std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
				for (const auto& custom : g_custom_dns) {
					if (custom.primary[0] == '\0' || DnsIpAlreadyListed(list, custom.primary)) {
						continue;
					}
					DnsCandidate row = {};
					strncpy_s(row.label, custom.label[0] ? custom.label : I18N(u8"自訂 DNS"), _TRUNCATE);
					strncpy_s(row.hint, u8"手動新增的 DNS", _TRUNCATE);
					strncpy_s(row.ip, custom.primary, _TRUNCATE);
					strncpy_s(row.secondary, custom.secondary, _TRUNCATE);
					row.apply_custom = true;
					list.push_back(row);
				}
			}

			bench.rows.reserve(list.size());
			for (size_t i = 0; i < list.size(); ++i) {
				const auto& item = list[i];
				DnsBenchRow row = {};
				strncpy_s(row.label, item.label, _TRUNCATE);
				strncpy_s(row.hint, item.hint, _TRUNCATE);
				strncpy_s(row.server_ip, item.ip, _TRUNCATE);
				strncpy_s(row.secondary_ip, item.secondary, _TRUNCATE);
				row.apply_provider = item.provider;
				row.apply_as_custom = item.apply_custom;
				row.is_current = item.is_current;
				row.ping_ms = PingHostIPv4(item.ip, 900);
				row.resolve_ms = DnsResolveMs(item.ip, bench.test_domain, 1800);
				bench.rows.push_back(row);
			}

			int best = -1;
			int best_ms = 99999;
			for (int i = 0; i < static_cast<int>(bench.rows.size()); ++i) {
				const int ms = bench.rows[static_cast<size_t>(i)].resolve_ms;
				if (ms >= 0 && ms < best_ms) {
					best_ms = ms;
					best = i;
				}
			}
			bench.best_index = best;
			if (best >= 0) {
				bench.rows[static_cast<size_t>(best)].is_fastest = true;
			}
			bench.valid = true;
			bench.running = false;
			if (best >= 0) {
				snprintf(bench.status_text, sizeof(bench.status_text),
					I18N(u8"最快：%s（%d 毫秒）"), bench.rows[static_cast<size_t>(best)].label, best_ms);
			}
			else {
				strncpy_s(bench.status_text, u8"DNS 測速完成，但全部逾時", _TRUNCATE);
			}

			{
				std::lock_guard<std::mutex> lock(g_dns_mutex);
				g_dns_bench = std::move(bench);
			}
			SaveNetworkCacheInternal();
		}

		static void QueryProxy(Snapshot& snap)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
				KEY_READ, &key) != ERROR_SUCCESS) {
				return;
			}
			DWORD enabled = 0;
			DWORD size = sizeof(enabled);
			if (RegQueryValueExW(key, L"ProxyEnable", nullptr, nullptr,
				reinterpret_cast<LPBYTE>(&enabled), &size) == ERROR_SUCCESS) {
				snap.proxy_enabled = (enabled != 0);
			}
			wchar_t proxy[256] = {};
			DWORD proxy_size = sizeof(proxy);
			DWORD type = 0;
			if (RegQueryValueExW(key, L"ProxyServer", nullptr, &type,
				reinterpret_cast<LPBYTE>(proxy), &proxy_size) == ERROR_SUCCESS
				&& type == REG_SZ) {
				WideToUtf8(proxy, snap.proxy_server, sizeof(snap.proxy_server));
			}
			RegCloseKey(key);
		}

		static void QueryAdapters(Snapshot& snap)
		{
			ULONG buf_len = 0;
			if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				nullptr, nullptr, &buf_len) != ERROR_BUFFER_OVERFLOW || buf_len == 0) {
				return;
			}
			std::vector<uint8_t> buffer(buf_len);
			auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
			if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				nullptr, addrs, &buf_len) != NO_ERROR) {
				return;
			}

			MIB_IF_TABLE2* if_table = nullptr;
			if (GetIfTable2(&if_table) != NO_ERROR) {
				if_table = nullptr;
			}

			int best_score = -1;
			for (auto* a = addrs; a != nullptr; a = a->Next) {
				if (a->OperStatus != IfOperStatusUp) {
					continue;
				}
				if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
					continue;
				}

				AdapterInfo info = {};
				info.if_index = a->IfIndex;
				info.connected = true;
				WideToUtf8(a->FriendlyName, info.friendly_name, sizeof(info.friendly_name));
				WideToUtf8(a->Description, info.desc_utf8, sizeof(info.desc_utf8));

				for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next) {
					if (u->Address.lpSockaddr == nullptr) {
						continue;
					}
					if (u->Address.lpSockaddr->sa_family == AF_INET && info.ipv4[0] == '\0') {
						char host[NI_MAXHOST] = {};
						if (getnameinfo(u->Address.lpSockaddr, static_cast<socklen_t>(u->Address.iSockaddrLength),
							host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
							strncpy_s(info.ipv4, host, _TRUNCATE);
						}
					}
					if (u->Address.lpSockaddr->sa_family == AF_INET6 && snap.ipv6[0] == '\0') {
						char host[NI_MAXHOST] = {};
						if (getnameinfo(u->Address.lpSockaddr, static_cast<socklen_t>(u->Address.iSockaddrLength),
							host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
							if (strstr(host, "fe80:") == nullptr && strcmp(host, "::1") != 0) {
								strncpy_s(snap.ipv6, host, _TRUNCATE);
							}
						}
					}
				}
				for (auto* g = a->FirstGatewayAddress; g != nullptr; g = g->Next) {
					if (g->Address.lpSockaddr != nullptr
						&& g->Address.lpSockaddr->sa_family == AF_INET
						&& info.gateway[0] == '\0') {
						char host[NI_MAXHOST] = {};
						if (getnameinfo(g->Address.lpSockaddr, static_cast<socklen_t>(g->Address.iSockaddrLength),
							host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
							strncpy_s(info.gateway, host, _TRUNCATE);
						}
					}
				}
				if (a->FirstDnsServerAddress != nullptr
					&& a->FirstDnsServerAddress->Address.lpSockaddr != nullptr
					&& a->FirstDnsServerAddress->Address.lpSockaddr->sa_family == AF_INET) {
					char host[NI_MAXHOST] = {};
					if (getnameinfo(a->FirstDnsServerAddress->Address.lpSockaddr,
						static_cast<socklen_t>(a->FirstDnsServerAddress->Address.iSockaddrLength),
						host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
						strncpy_s(info.dns_primary, host, _TRUNCATE);
					}
				}

				if (if_table != nullptr) {
					for (ULONG i = 0; i < if_table->NumEntries; ++i) {
						const MIB_IF_ROW2& row = if_table->Table[i];
						if (row.InterfaceIndex == a->IfIndex) {
							info.bytes_in = row.InOctets;
							info.bytes_out = row.OutOctets;
							info.link_speed_bps = row.ReceiveLinkSpeed;
							info.mtu = static_cast<int>(row.Mtu);
							break;
						}
					}
				}

				snap.adapters.push_back(info);

				int score = 0;
				if (a->IfType == IF_TYPE_ETHERNET_CSMACD) {
					score += 100;
				}
				else if (a->IfType == IF_TYPE_IEEE80211) {
					score += 80;
				}
				else {
					score += 40;
				}
				if (info.ipv4[0] != '\0') {
					score += 50;
				}
				if (score > best_score) {
					best_score = score;
					snap.primary_if_index = info.if_index;
					snap.mtu = info.mtu;
					strncpy_s(snap.adapter_name, info.friendly_name, _TRUNCATE);
					strncpy_s(snap.ipv4, info.ipv4, _TRUNCATE);
					strncpy_s(snap.gateway, info.gateway, _TRUNCATE);
					strncpy_s(snap.dns_primary, info.dns_primary, _TRUNCATE);
					g_tracked_if_index = info.if_index;
				}
			}

			if (if_table != nullptr) {
				FreeMibTable(if_table);
			}

			for (const auto& ad : snap.adapters) {
				if (ad.dns_primary[0] != '\0'
					&& strcmp(ad.dns_primary, snap.dns_primary) != 0
					&& snap.dns_secondary[0] == '\0') {
					strncpy_s(snap.dns_secondary, ad.dns_primary, _TRUNCATE);
					break;
				}
			}
		}

		static void ResolveProcessName(DWORD pid, char* name, size_t name_sz,
			char* path, size_t path_sz)
		{
			if (name != nullptr && name_sz > 0) {
				name[0] = '\0';
			}
			if (path != nullptr && path_sz > 0) {
				path[0] = '\0';
			}
			if (pid == 0) {
				strncpy_s(name, name_sz, "System", _TRUNCATE);
				return;
			}
			const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (proc == nullptr) {
				snprintf(name, name_sz, "PID %lu", pid);
				return;
			}
			wchar_t wpath[MAX_PATH] = {};
			DWORD wpath_sz = MAX_PATH;
			if (QueryFullProcessImageNameW(proc, 0, wpath, &wpath_sz)) {
				WideToUtf8(wpath, path, path_sz);
				const wchar_t* slash = wcsrchr(wpath, L'\\');
				const wchar_t* base = slash != nullptr ? slash + 1 : wpath;
				WideToUtf8(base, name, name_sz);
			}
			else {
				snprintf(name, name_sz, "PID %lu", pid);
			}
			CloseHandle(proc);
		}

		static void QueryProcessConnections(Snapshot& snap)
		{
			std::unordered_map<DWORD, std::pair<int, int>> counts;

			auto aggregate_tcp = [&](DWORD family) {
				ULONG size = 0;
				if (GetExtendedTcpTable(nullptr, &size, TRUE, family, TCP_TABLE_OWNER_PID_ALL, 0)
					!= ERROR_INSUFFICIENT_BUFFER) {
					return;
				}
				std::vector<uint8_t> buf(size);
				if (GetExtendedTcpTable(buf.data(), &size, TRUE, family, TCP_TABLE_OWNER_PID_ALL, 0)
					!= NO_ERROR) {
					return;
				}
				const auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
				for (DWORD i = 0; i < table->dwNumEntries; ++i) {
					const auto& row = table->table[i];
					if (row.dwState == MIB_TCP_STATE_CLOSED || row.dwState == MIB_TCP_STATE_DELETE_TCB) {
						continue;
					}
					auto& c = counts[row.dwOwningPid];
					c.first += 1;
				}
			};

			auto aggregate_udp = [&](DWORD family) {
				ULONG size = 0;
				if (GetExtendedUdpTable(nullptr, &size, TRUE, family, UDP_TABLE_OWNER_PID, 0)
					!= ERROR_INSUFFICIENT_BUFFER) {
					return;
				}
				std::vector<uint8_t> buf(size);
				if (GetExtendedUdpTable(buf.data(), &size, TRUE, family, UDP_TABLE_OWNER_PID, 0)
					!= NO_ERROR) {
					return;
				}
				const auto* table = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buf.data());
				for (DWORD i = 0; i < table->dwNumEntries; ++i) {
					const auto& row = table->table[i];
					auto& c = counts[row.dwOwningPid];
					c.second += 1;
				}
			};

			aggregate_tcp(AF_INET);
			aggregate_tcp(AF_INET6);
			aggregate_udp(AF_INET);
			aggregate_udp(AF_INET6);

			snap.processes.clear();
			snap.processes.reserve(counts.size());
			for (const auto& kv : counts) {
				ProcessNetRow row = {};
				row.pid = kv.first;
				row.tcp_count = kv.second.first;
				row.udp_count = kv.second.second;
				row.activity_score = row.tcp_count + row.udp_count;
				if (row.activity_score <= 0) {
					continue;
				}
				ResolveProcessName(kv.first, row.name_utf8, sizeof(row.name_utf8),
					row.path_utf8, sizeof(row.path_utf8));
				snap.processes.push_back(row);
			}

			std::sort(snap.processes.begin(), snap.processes.end(),
				[](const ProcessNetRow& a, const ProcessNetRow& b) {
					if (a.activity_score != b.activity_score) {
						return a.activity_score > b.activity_score;
					}
					return a.tcp_count > b.tcp_count;
				});

			snap.active_process_count = static_cast<int>(snap.processes.size());
			for (const auto& row : snap.processes) {
				snap.total_tcp_connections += row.tcp_count;
				snap.total_udp_connections += row.udp_count;
			}

			if (snap.processes.size() > 24) {
				snap.processes.resize(24);
			}
		}

		static void RunScanWorker()
		{
			Snapshot snap = {};
			snap.scanning = true;
			strncpy_s(snap.status_text, u8"正在掃描網路狀態…", _TRUNCATE);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.scanning = true;
				g_snapshot.progress = 0.1f;
				strncpy_s(g_snapshot.status_text, snap.status_text, _TRUNCATE);
			}

			QueryAdapters(snap);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.35f;
			}

			QueryProxy(snap);
			QueryProcessConnections(snap);
			QueryFirewallStatus(snap);
			QueryWifiInfo(snap);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.55f;
			}

			if (snap.gateway[0] != '\0') {
				snap.gateway_ping_ms = PingHostIPv4(snap.gateway);
			}
			snap.internet_ping_ms = PingHostIPv4("1.1.1.1");
			if (snap.internet_ping_ms < 0) {
				snap.internet_ping_ms = PingHostIPv4("8.8.8.8");
			}
			snap.internet_reachable = (snap.internet_ping_ms >= 0);
			QueryDnsRegistrySettings(snap);
			QueryDnsSuffixFromAdapter(snap);
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (g_snapshot.packet_loss_percent >= 0) {
					snap.packet_loss_percent = g_snapshot.packet_loss_percent;
				}
				if (g_snapshot.netbios_over_tcp != snap.netbios_over_tcp
					&& g_snapshot.primary_if_index == snap.primary_if_index) {
					snap.netbios_over_tcp = g_snapshot.netbios_over_tcp;
				}
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_snapshot.progress = 0.82f;
			}

			snap.valid = true;
			snap.scanning = false;
			snap.progress = 1.f;
			strncpy_s(snap.status_text, u8"掃描完成", _TRUNCATE);

			std::lock_guard<std::mutex> lock(g_mutex);
			g_snapshot = std::move(snap);
		}

		static std::wstring PrimaryAdapterWideName()
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			wchar_t wide[128] = {};
			MultiByteToWideChar(CP_UTF8, 0, g_snapshot.adapter_name, -1, wide, 128);
			return wide;
		}
	}

	void Init()
	{
		WSADATA wsa = {};
		(void)WSAStartup(MAKEWORD(2, 2), &wsa);
		HAppPaths::EnsureAppDataDirs();
		LoadNetworkCacheInternal();
	}

	void Shutdown()
	{
		if (g_worker.joinable()) {
			g_worker.join();
		}
		if (g_dns_worker.joinable()) {
			g_dns_worker.join();
		}
		if (g_link_worker.joinable()) {
			g_link_worker.join();
		}
		if (g_speed_worker.joinable()) {
			g_speed_worker.join();
		}
		if (g_trace_worker.joinable()) {
			g_trace_worker.join();
		}
		if (g_diag_worker.joinable()) {
			g_diag_worker.join();
		}
		if (g_full_dns_worker.joinable()) {
			g_full_dns_worker.join();
		}
		if (g_full_net_worker.joinable()) {
			g_full_net_worker.join();
		}
		SaveNetworkCacheInternal();
		WSACleanup();
	}

	void RequestRefresh()
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (g_snapshot.scanning) {
				return;
			}
		}
		if (g_worker.joinable()) {
			g_worker.join();
		}
		g_worker = std::thread([] { RunScanWorker(); });
	}

	void TickBandwidth()
	{
		if (g_tracked_if_index == 0) {
			return;
		}
		MIB_IF_TABLE2* if_table = nullptr;
		if (GetIfTable2(&if_table) != NO_ERROR || if_table == nullptr) {
			return;
		}
		uint64_t in_octets = 0;
		uint64_t out_octets = 0;
		for (ULONG i = 0; i < if_table->NumEntries; ++i) {
			if (if_table->Table[i].InterfaceIndex == g_tracked_if_index) {
				in_octets = if_table->Table[i].InOctets;
				out_octets = if_table->Table[i].OutOctets;
				break;
			}
		}
		FreeMibTable(if_table);

		const uint64_t now_ms = GetTickCount64();
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_last_sample_ms == 0) {
			g_last_bytes_in = in_octets;
			g_last_bytes_out = out_octets;
			g_last_sample_ms = now_ms;
			return;
		}
		const double dt = static_cast<double>(now_ms - g_last_sample_ms) / 1000.0;
		if (dt < 0.4) {
			return;
		}
		float dl_bps = g_snapshot.download_bps;
		float ul_bps = g_snapshot.upload_bps;
		if (in_octets >= g_last_bytes_in) {
			dl_bps = static_cast<float>((in_octets - g_last_bytes_in) / dt);
			g_snapshot.download_bps = dl_bps;
		}
		if (out_octets >= g_last_bytes_out) {
			ul_bps = static_cast<float>((out_octets - g_last_bytes_out) / dt);
			g_snapshot.upload_bps = ul_bps;
		}
		g_last_bytes_in = in_octets;
		g_last_bytes_out = out_octets;
		g_last_sample_ms = now_ms;
		if (dl_bps >= 0.f || ul_bps >= 0.f) {
			PushBandwidthSample(std::max(0.f, dl_bps), std::max(0.f, ul_bps));
		}
		const uint64_t now_cache = GetTickCount64();
		if (g_last_cache_save_ms == 0) {
			g_last_cache_save_ms = now_cache;
		}
		else if (now_cache - g_last_cache_save_ms >= 45000) {
			g_last_cache_save_ms = now_cache;
			SaveNetworkCacheInternal();
		}
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_snapshot;
	}

	bool IsScanning()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_snapshot.scanning;
	}

	const char* GetLastActionMessage()
	{
		return g_last_action;
	}

	bool FlushArpCache()
	{
		const bool ok = RunHiddenCommand(L"netsh.exe interface ip delete arpcache");
		SetLastAction(ok ? I18N(u8"已清除 ARP 快取") : I18N(u8"ARP 快取清除失敗"));
		return ok;
	}

	bool RunQuickNetworkRepair()
	{
		bool ok = true;
		ok = OptimizeScan::FlushDnsCache() && ok;
		ok = OptimizeScan::RegisterDnsCache() && ok;
		ok = OptimizeScan::RenewIpAddresses() && ok;
		SetLastAction(ok ? I18N(u8"已完成快速網路修復（DNS + IP）") : I18N(u8"快速修復部分步驟失敗"));
		RequestRefresh();
		return ok;
	}

	bool ResetTcpIpStack()
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"重設 TCP/IP 堆疊需管理員權限");
			return false;
		}
		const bool ok = RunHiddenCommand(L"netsh.exe int ip reset");
		SetLastAction(ok ? I18N(u8"已重設 TCP/IP 堆疊（建議重新開機）") : I18N(u8"TCP/IP 重設失敗"));
		return ok;
	}

	bool ReleaseAndRenewIp()
	{
		bool ok = RunHiddenCommand(L"ipconfig.exe /release");
		ok = RunHiddenCommand(L"ipconfig.exe /renew") && ok;
		SetLastAction(ok ? I18N(u8"已釋放並更新 IP 位址") : I18N(u8"IP 釋放／更新失敗"));
		RequestRefresh();
		return ok;
	}

	bool SetAdapterDnsDhcp()
	{
		const std::wstring name = PrimaryAdapterWideName();
		if (name.empty()) {
			SetLastAction(u8"找不到作用中的網路介面卡");
			return false;
		}
		wchar_t cmd[512] = {};
		_snwprintf_s(cmd, _TRUNCATE,
			L"netsh.exe interface ipv4 set dns name=\"%s\" source=dhcp", name.c_str());
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok ? I18N(u8"已還原為自動取得 DNS") : I18N(u8"DNS 設定失敗"));
		RequestRefresh();
		return ok;
	}

	bool SetAdapterDnsPublic(int provider)
	{
		const std::wstring name = PrimaryAdapterWideName();
		if (name.empty()) {
			SetLastAction(u8"找不到作用中的網路介面卡");
			return false;
		}
		const wchar_t* primary = L"1.1.1.1";
		const wchar_t* secondary = L"8.8.8.8";
		if (provider == 2) {
			primary = L"8.8.8.8";
			secondary = L"8.8.4.4";
		}
		else if (provider == 3) {
			primary = L"223.5.5.5";
			secondary = L"223.6.6.6";
		}
		else if (provider == 4) {
			primary = L"114.114.114.114";
			secondary = L"114.114.115.115";
		}
		else if (provider == 5) {
			primary = L"119.29.29.29";
			secondary = L"119.28.28.28";
		}
		else if (provider == 6) {
			primary = L"9.9.9.9";
			secondary = L"149.112.112.112";
		}
		else if (provider == 7) {
			primary = L"208.67.222.222";
			secondary = L"208.67.220.220";
		}
		else if (provider == 8) {
			primary = L"94.140.14.14";
			secondary = L"94.140.15.15";
		}
		wchar_t cmd1[512] = {};
		wchar_t cmd2[512] = {};
		_snwprintf_s(cmd1, _TRUNCATE,
			L"netsh.exe interface ipv4 set dns name=\"%s\" static %s primary", name.c_str(), primary);
		_snwprintf_s(cmd2, _TRUNCATE,
			L"netsh.exe interface ipv4 add dns name=\"%s\" %s index=2", name.c_str(), secondary);
		bool ok = RunHiddenCommand(cmd1);
		ok = RunHiddenCommand(cmd2) && ok;
		SetLastAction(ok ? I18N(u8"已套用公共 DNS") : I18N(u8"公共 DNS 設定失敗"));
		RequestRefresh();
		return ok;
	}

	bool PingDiagnostics()
	{
		RequestRefresh();
		const Snapshot snap = GetSnapshot();
		char body[256] = {};
		snprintf(body, sizeof(body), I18N(u8"路由器 %s：%s · 上網：%s"),
			snap.gateway[0] ? snap.gateway : "—",
			snap.gateway_ping_ms >= 0 ? I18N(u8"正常") : I18N(u8"無回應"),
			snap.internet_reachable ? I18N(u8"正常") : I18N(u8"無法連線"));
		SetLastAction(body);
		return snap.internet_reachable;
	}

	void RequestDnsBenchmark()
	{
		{
			std::lock_guard<std::mutex> lock(g_dns_mutex);
			if (g_dns_bench.running) {
				return;
			}
		}
		if (g_dns_worker.joinable()) {
			g_dns_worker.join();
		}
		g_dns_worker = std::thread([] { RunDnsBenchmarkWorker(); });
	}

	DnsBenchSnapshot GetDnsBenchmark()
	{
		std::lock_guard<std::mutex> lock(g_dns_mutex);
		return g_dns_bench;
	}

	bool IsDnsBenchmarkRunning()
	{
		std::lock_guard<std::mutex> lock(g_dns_mutex);
		return g_dns_bench.running;
	}

	bool SetAdapterDnsCustom(const char* primary, const char* secondary)
	{
		if (!IsValidIPv4(primary)) {
			SetLastAction(u8"DNS 位址格式不正確（需 IPv4，例如 1.1.1.1）");
			return false;
		}
		if (secondary != nullptr && secondary[0] != '\0' && !IsValidIPv4(secondary)) {
			SetLastAction(u8"備用 DNS 位址格式不正確");
			return false;
		}
		const std::wstring name = PrimaryAdapterWideName();
		if (name.empty()) {
			SetLastAction(u8"找不到作用中的網路介面卡");
			return false;
		}
		wchar_t wprimary[48] = {};
		wchar_t wsecondary[48] = {};
		MultiByteToWideChar(CP_UTF8, 0, primary, -1, wprimary, 48);
		wchar_t cmd1[512] = {};
		_snwprintf_s(cmd1, _TRUNCATE,
			L"netsh.exe interface ipv4 set dns name=\"%s\" static %s primary", name.c_str(), wprimary);
		bool ok = RunHiddenCommand(cmd1);
		if (secondary != nullptr && secondary[0] != '\0') {
			MultiByteToWideChar(CP_UTF8, 0, secondary, -1, wsecondary, 48);
			wchar_t cmd2[512] = {};
			_snwprintf_s(cmd2, _TRUNCATE,
				L"netsh.exe interface ipv4 add dns name=\"%s\" %s index=2", name.c_str(), wsecondary);
			ok = RunHiddenCommand(cmd2) && ok;
		}
		SetLastAction(ok ? I18N(u8"已套用手動 DNS") : I18N(u8"手動 DNS 設定失敗"));
		RequestRefresh();
		return ok;
	}

	bool AddCustomDnsEntry(const char* label, const char* primary, const char* secondary)
	{
		if (!IsValidIPv4(primary)) {
			SetLastAction(u8"請輸入有效的 IPv4 DNS 位址");
			return false;
		}
		if (secondary != nullptr && secondary[0] != '\0' && !IsValidIPv4(secondary)) {
			SetLastAction(u8"備用 DNS 位址格式不正確");
			return false;
		}
		{
			std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
			if (g_custom_dns.size() >= 8) {
				SetLastAction(u8"自訂 DNS 已達上限（8 筆）");
				return false;
			}
			for (const auto& e : g_custom_dns) {
				if (strcmp(e.primary, primary) == 0) {
					SetLastAction(u8"此 DNS 已在清單中");
					return false;
				}
			}
			CustomDnsEntry entry = {};
			if (label != nullptr && label[0] != '\0') {
				strncpy_s(entry.label, label, _TRUNCATE);
			}
			else {
				strncpy_s(entry.label, u8"自訂 DNS", _TRUNCATE);
			}
			strncpy_s(entry.primary, primary, _TRUNCATE);
			if (secondary != nullptr && secondary[0] != '\0') {
				strncpy_s(entry.secondary, secondary, _TRUNCATE);
			}
			g_custom_dns.push_back(entry);
			SetLastAction(u8"已加入自訂 DNS");
		}
		SaveNetworkCacheInternal();
		return true;
	}

	bool RemoveCustomDnsEntry(int index)
	{
		{
			std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
			if (index < 0 || index >= static_cast<int>(g_custom_dns.size())) {
				SetLastAction(u8"找不到要刪除的 DNS");
				return false;
			}
			g_custom_dns.erase(g_custom_dns.begin() + index);
			SetLastAction(u8"已刪除自訂 DNS");
		}
		SaveNetworkCacheInternal();
		return true;
	}

	std::vector<CustomDnsEntry> GetCustomDnsEntries()
	{
		std::lock_guard<std::mutex> lock(g_custom_dns_mutex);
		return g_custom_dns;
	}

	static void RunLinkTestWorker()
	{
		LinkTestSnapshot result = {};
		result.running = true;
		strncpy_s(result.status_text, u8"正在測試連線…", _TRUNCATE);
		{
			std::lock_guard<std::mutex> lock(g_link_mutex);
			g_link_test = result;
		}

		const Snapshot snap = GetSnapshot();
		struct TargetDef {
			const char* name;
			const char* target;
			bool hostname;
		};
		TargetDef targets[] = {
			{ I18N(u8"路由器"), snap.gateway, false },
			{ "Cloudflare", "1.1.1.1", false },
			{ "Google DNS", "8.8.8.8", false },
			{ I18N(u8"阿里 DNS"), "223.5.5.5", false },
			{ "114 DNS", "114.114.114.114", false },
			{ I18N(u8"騰訊 DNS"), "119.29.29.29", false },
			{ "Quad9", "9.9.9.9", false },
			{ I18N(u8"微軟測試"), "www.msftconnecttest.com", true },
			{ I18N(u8"百度"), "www.baidu.com", true },
			{ "GitHub", "github.com", true },
			{ "Steam", "store.steampowered.com", true },
			{ "Apple", "www.apple.com", true },
		};

		const int total = static_cast<int>(sizeof(targets) / sizeof(targets[0]));
		const int start_idx = (snap.gateway[0] != '\0') ? 0 : 1;
		const int run_count = total - start_idx;
		for (int i = start_idx; i < total; ++i) {
			LinkTestRow row = {};
			strncpy_s(row.name, targets[i].name, _TRUNCATE);
			strncpy_s(row.target, targets[i].target, _TRUNCATE);
			PingWithJitter(targets[i].target, targets[i].hostname,
				row.ping_ms, row.jitter_ms);
			row.reachable = (row.ping_ms >= 0);
			result.rows.push_back(row);
			result.progress = static_cast<float>(result.rows.size())
				/ static_cast<float>(run_count);
			{
				std::lock_guard<std::mutex> lock(g_link_mutex);
				g_link_test.progress = result.progress;
			}
		}

		if (snap.dns_primary[0] != '\0') {
			result.dns_resolve_ms = DnsResolveMs(snap.dns_primary, "www.msftconnecttest.com", 2000);
		}
		else {
			result.dns_resolve_ms = DnsResolveMs("1.1.1.1", "www.msftconnecttest.com", 2000);
		}
		result.dns_ok = (result.dns_resolve_ms >= 0);

		const uint64_t http_t0 = GetTickCount64();
		HINTERNET session = WinHttpOpen(L"HP_CLEANER++/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, 0);
		if (session != nullptr) {
			HINTERNET connect = WinHttpConnect(session, L"www.msftconnecttest.com",
				INTERNET_DEFAULT_HTTPS_PORT, 0);
			if (connect != nullptr) {
				HINTERNET request = WinHttpOpenRequest(connect, L"GET",
					L"/connecttest.txt", nullptr, WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (request != nullptr) {
					if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
						WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
						&& WinHttpReceiveResponse(request, nullptr)) {
						char buf[64] = {};
						DWORD read = 0;
						if (WinHttpReadData(request, buf, sizeof(buf), &read) && read > 0) {
							result.http_ok = true;
							result.http_ms = static_cast<int>(GetTickCount64() - http_t0);
						}
					}
					WinHttpCloseHandle(request);
				}
				WinHttpCloseHandle(connect);
			}
			WinHttpCloseHandle(session);
		}

		result.valid = true;
		result.running = false;
		result.progress = 1.f;
		snprintf(result.status_text, sizeof(result.status_text),
			I18N(u8"連線測試完成 · DNS %s · 網頁 %s"),
			result.dns_ok ? I18N(u8"正常") : I18N(u8"異常"),
			result.http_ok ? I18N(u8"正常") : I18N(u8"異常"));
		{
			std::lock_guard<std::mutex> lock(g_link_mutex);
			g_link_test = std::move(result);
		}
		SaveNetworkCacheInternal();
	}

	void RequestLinkTest()
	{
		{
			std::lock_guard<std::mutex> lock(g_link_mutex);
			if (g_link_test.running) {
				return;
			}
		}
		if (g_link_worker.joinable()) {
			g_link_worker.join();
		}
		g_link_worker = std::thread([] { RunLinkTestWorker(); });
	}

	LinkTestSnapshot GetLinkTest()
	{
		std::lock_guard<std::mutex> lock(g_link_mutex);
		return g_link_test;
	}

	bool IsLinkTestRunning()
	{
		std::lock_guard<std::mutex> lock(g_link_mutex);
		return g_link_test.running;
	}

	static void RunSpeedTestWorker(bool upload_mode)
	{
		SpeedTestSnapshot result = {};
		result.running = true;
		result.upload_mode = upload_mode;
		strncpy_s(result.status_text, upload_mode ? I18N(u8"正在測試上傳速度…") : I18N(u8"正在測試下載速度…"),
			_TRUNCATE);
		{
			std::lock_guard<std::mutex> lock(g_speed_mutex);
			g_speed_test = result;
		}

		const wchar_t* host = L"speed.cloudflare.com";
		const wchar_t* path = upload_mode
			? L"/__up?bytes=5000000" : L"/__down?bytes=10000000";
		const uint64_t t0 = GetTickCount64();
		uint64_t total_bytes = 0;
		float peak_mbps = 0.f;
		const uint64_t expected = upload_mode ? 5000000ull : 10000000ull;

		HINTERNET session = WinHttpOpen(L"HP_CLEANER++/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, 0);
		if (session != nullptr) {
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
			WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
			HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
			if (connect != nullptr) {
				HINTERNET request = WinHttpOpenRequest(connect,
					upload_mode ? L"POST" : L"GET", path, nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (request != nullptr) {
					bool ok = false;
					if (upload_mode) {
						const DWORD total_len = static_cast<DWORD>(expected);
						ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
							WINHTTP_NO_REQUEST_DATA, 0, total_len, 0) != FALSE;
						if (ok) {
							char buffer[65536] = {};
							DWORD remaining = total_len;
							while (remaining > 0) {
								const DWORD chunk = std::min(remaining, static_cast<DWORD>(sizeof(buffer)));
								DWORD written = 0;
								if (!WinHttpWriteData(request, buffer, chunk, &written) || written == 0) {
									ok = false;
									break;
								}
								total_bytes += written;
								remaining -= written;
								const uint64_t elapsed = GetTickCount64() - t0;
								if (elapsed > 0) {
									const float instant = static_cast<float>(
										(total_bytes * 8.0) / (static_cast<double>(elapsed) / 1000.0) / 1000000.0);
									peak_mbps = std::max(peak_mbps, instant);
									result.progress = std::min(1.f, static_cast<float>(total_bytes)
										/ static_cast<float>(expected));
									std::lock_guard<std::mutex> lock(g_speed_mutex);
									g_speed_test.progress = result.progress;
								}
							}
						}
						if (ok) {
							ok = WinHttpReceiveResponse(request, nullptr) != FALSE;
						}
					}
					else if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
						WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
						&& WinHttpReceiveResponse(request, nullptr)) {
						ok = true;
						DWORD chunk = 0;
						char buffer[65536] = {};
						while (WinHttpReadData(request, buffer, sizeof(buffer), &chunk) && chunk > 0) {
							total_bytes += chunk;
							const uint64_t elapsed = GetTickCount64() - t0;
							if (elapsed > 0) {
								const float instant = static_cast<float>(
									(total_bytes * 8.0) / (static_cast<double>(elapsed) / 1000.0) / 1000000.0);
								peak_mbps = std::max(peak_mbps, instant);
								result.progress = std::min(1.f, static_cast<float>(total_bytes)
									/ static_cast<float>(expected));
								std::lock_guard<std::mutex> lock(g_speed_mutex);
								g_speed_test.progress = result.progress;
							}
						}
					}
					(void)ok;
					WinHttpCloseHandle(request);
				}
				WinHttpCloseHandle(connect);
			}
			WinHttpCloseHandle(session);
		}

		const int duration_ms = static_cast<int>(GetTickCount64() - t0);
		result.duration_ms = duration_ms;
		result.bytes_transferred = total_bytes;
		result.peak_mbps = peak_mbps;
		if (total_bytes > 0 && duration_ms > 0) {
			const float mbps = static_cast<float>(
				(total_bytes * 8.0) / (static_cast<double>(duration_ms) / 1000.0) / 1000000.0);
			if (upload_mode) {
				result.upload_mbps = mbps;
			}
			else {
				result.download_mbps = mbps;
			}
		}
		result.valid = (total_bytes > 0);
		result.running = false;
		result.progress = 1.f;
		if (result.valid) {
			if (upload_mode) {
				snprintf(result.status_text, sizeof(result.status_text),
					I18N(u8"上傳約 %.1f Mbps（峰值 %.1f Mbps）"),
					result.upload_mbps, result.peak_mbps);
			}
			else {
				snprintf(result.status_text, sizeof(result.status_text),
					I18N(u8"下載約 %.1f Mbps（峰值 %.1f Mbps）"),
					result.download_mbps, result.peak_mbps);
			}
		}
		else {
			strncpy_s(result.status_text, upload_mode
				? I18N(u8"上傳測速失敗，請檢查網路或代理")
				: I18N(u8"下載測速失敗，請檢查網路或代理"), _TRUNCATE);
		}
		{
			std::lock_guard<std::mutex> lock(g_speed_mutex);
			if (!upload_mode) {
				g_speed_test.download_mbps = result.download_mbps;
			}
			else {
				g_speed_test.upload_mbps = result.upload_mbps;
			}
			g_speed_test.peak_mbps = result.peak_mbps;
			g_speed_test.bytes_transferred = result.bytes_transferred;
			g_speed_test.duration_ms = result.duration_ms;
			g_speed_test.valid = result.valid;
			g_speed_test.running = false;
			g_speed_test.upload_mode = false;
			g_speed_test.progress = 1.f;
			strncpy_s(g_speed_test.status_text, result.status_text, _TRUNCATE);
			if (g_speed_test.valid) {
				SpeedHistoryEntry entry = {};
				entry.download_mbps = g_speed_test.download_mbps;
				entry.upload_mbps = g_speed_test.upload_mbps;
				entry.tested_at_ms = GetTickCount64();
				g_speed_history.push_back(entry);
				if (g_speed_history.size() > 24) {
					g_speed_history.erase(g_speed_history.begin());
				}
			}
		}
		SaveNetworkCacheInternal();
	}

	static void StartSpeedWorker(bool upload_mode)
	{
		{
			std::lock_guard<std::mutex> lock(g_speed_mutex);
			if (g_speed_test.running) {
				return;
			}
		}
		if (g_speed_worker.joinable()) {
			g_speed_worker.join();
		}
		g_speed_worker = std::thread([upload_mode] { RunSpeedTestWorker(upload_mode); });
	}

	void RequestSpeedTest()
	{
		StartSpeedWorker(false);
	}

	void RequestUploadSpeedTest()
	{
		StartSpeedWorker(true);
	}

	SpeedTestSnapshot GetSpeedTest()
	{
		std::lock_guard<std::mutex> lock(g_speed_mutex);
		return g_speed_test;
	}

	bool IsSpeedTestRunning()
	{
		std::lock_guard<std::mutex> lock(g_speed_mutex);
		return g_speed_test.running;
	}

	BandwidthHistory GetBandwidthHistory()
	{
		std::lock_guard<std::mutex> lock(g_bw_hist_mutex);
		BandwidthHistory ordered = {};
		ordered.count = g_bw_history.count;
		if (g_bw_history.count <= 0) {
			return ordered;
		}
		const int start = (g_bw_history.count < BandwidthHistory::kMaxSamples)
			? 0
			: g_bw_write_head;
		for (int i = 0; i < g_bw_history.count; ++i) {
			const int src = (start + i) % BandwidthHistory::kMaxSamples;
			ordered.samples[i] = g_bw_history.samples[src];
		}
		return ordered;
	}

	std::vector<SpeedHistoryEntry> GetSpeedTestHistory()
	{
		std::lock_guard<std::mutex> lock(g_speed_mutex);
		return g_speed_history;
	}

	std::string BuildNetworkReportText()
	{
		const Snapshot snap = GetSnapshot();
		const DnsBenchSnapshot dns = GetDnsBenchmark();
		const LinkTestSnapshot link = GetLinkTest();
		const SpeedTestSnapshot speed = GetSpeedTest();
		char buf[4096] = {};
		snprintf(buf, sizeof(buf),
			I18N(u8"HP CLEANER++ 網路報告\r\n"
				"================\r\n"
				u8"連線：%s\r\n"
				"IPv4：%s\r\n"
				"IPv6：%s\r\n"
				"DNS：%s / %s\r\n"
				u8"路由器：%s\r\n"
				u8"上網延遲：%d ms\r\n"
				u8"下載即時：%.0f B/s\r\n"
				u8"上傳即時：%.0f B/s\r\n"
				u8"TCP 連線：%d · UDP：%d · 活躍程式：%d\r\n"
				u8"代理：%s\r\n"
				u8"DNS 測速：%s\r\n"
				u8"連線測試：%s\r\n"
				u8"下載測速：%s\r\n"),
			snap.adapter_name[0] ? snap.adapter_name : "—",
			snap.ipv4[0] ? snap.ipv4 : "—",
			snap.ipv6[0] ? snap.ipv6 : "—",
			snap.dns_primary[0] ? snap.dns_primary : u8"自動",
			snap.dns_secondary[0] ? snap.dns_secondary : "—",
			snap.gateway[0] ? snap.gateway : "—",
			snap.internet_ping_ms,
			snap.download_bps >= 0.f ? snap.download_bps : 0.f,
			snap.upload_bps >= 0.f ? snap.upload_bps : 0.f,
			snap.total_tcp_connections, snap.total_udp_connections, snap.active_process_count,
			snap.proxy_enabled ? (snap.proxy_server[0] ? snap.proxy_server : u8"已啟用") : u8"未使用",
			dns.status_text[0] ? dns.status_text : "—",
			link.status_text[0] ? link.status_text : "—",
			speed.status_text[0] ? speed.status_text : "—");
		return std::string(buf);
	}

	bool OpenWindowsNetworkSettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"ms-settings:network-status",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟 Windows 網路設定") : I18N(u8"無法開啟網路設定"));
		return ok;
	}

	bool OpenNetworkAdapterSettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"ncpa.cpl",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟網路介面卡設定") : I18N(u8"無法開啟介面卡設定"));
		return ok;
	}

	bool OpenProxySettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"ms-settings:network-proxy",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟 Proxy 設定") : I18N(u8"無法開啟 Proxy 設定"));
		return ok;
	}

	bool OpenResourceMonitorNetwork()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"resmon.exe", nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟資源監視器") : I18N(u8"無法開啟資源監視器"));
		return ok;
	}

	bool OpenFirewallSettings()
	{
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"firewall.cpl",
			nullptr, nullptr, SW_SHOWNORMAL);
		const bool ok = reinterpret_cast<intptr_t>(r) > 32;
		SetLastAction(ok ? I18N(u8"已開啟防火牆設定") : I18N(u8"無法開啟防火牆設定"));
		return ok;
	}

	bool GetFirewallStatus(FirewallStatus& out)
	{
		const Snapshot snap = GetSnapshot();
		out = snap.firewall;
		return snap.valid;
	}

	bool SetFirewallProfileEnabled(int profile, bool enabled)
	{
		const wchar_t* names[] = { L"domainprofile", L"privateprofile", L"publicprofile" };
		if (profile < 0 || profile > 2) {
			SetLastAction(u8"未知的防火牆設定檔");
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"變更防火牆需管理員權限");
			return false;
		}
		wchar_t cmd[160] = {};
		_snwprintf_s(cmd, _TRUNCATE, L"netsh.exe advfirewall set %s state %s",
			names[profile], enabled ? L"on" : L"off");
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok ? (enabled ? I18N(u8"已開啟防火牆設定檔") : I18N(u8"已關閉防火牆設定檔"))
			: I18N(u8"防火牆設定失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	bool SetAllFirewallEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"變更防火牆需管理員權限");
			return false;
		}
		bool ok = SetFirewallProfileEnabled(0, enabled);
		ok = SetFirewallProfileEnabled(1, enabled) && ok;
		ok = SetFirewallProfileEnabled(2, enabled) && ok;
		return ok;
	}

	bool ResetFirewallDefaults()
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"重設防火牆需管理員權限");
			return false;
		}
		const bool ok = RunHiddenCommand(L"netsh.exe advfirewall reset");
		SetLastAction(ok ? I18N(u8"已重設防火牆為預設值") : I18N(u8"防火牆重設失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	bool SetMeteredConnection(bool metered)
	{
		const Snapshot snap = GetSnapshot();
		if (snap.primary_if_index == 0) {
			SetLastAction(u8"找不到作用中的網路介面");
			return false;
		}
		wchar_t cmd[320] = {};
		_snwprintf_s(cmd, _TRUNCATE,
			L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"Set-NetConnectionProfile -InterfaceIndex %u -NetworkCost %s\"",
			snap.primary_if_index, metered ? L"Variable" : L"Unrestricted");
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok ? (metered ? I18N(u8"已設為計量連線") : I18N(u8"已取消計量連線")) : I18N(u8"計量連線設定失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	bool SetDnsOverHttpsEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"DNS over HTTPS 設定需管理員權限");
			return false;
		}
		HKEY key = nullptr;
		const LONG open_err = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
			0, KEY_SET_VALUE, &key);
		if (open_err != ERROR_SUCCESS) {
			SetLastAction(u8"無法開啟 DNS 快取參數登錄");
			return false;
		}
		const DWORD val = enabled ? 1u : 0u;
		const LONG err = RegSetValueExW(key, L"EnableAutoDoh", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		const bool ok = (err == ERROR_SUCCESS);
		SetLastAction(ok ? (enabled ? I18N(u8"已開啟自動 DoH") : I18N(u8"已關閉自動 DoH")) : I18N(u8"DoH 設定失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	bool SetPrimaryIpv6Enabled(bool enabled)
	{
		const std::wstring name = PrimaryAdapterWideName();
		if (name.empty()) {
			SetLastAction(u8"找不到作用中的網路介面卡");
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"IPv6 設定需管理員權限");
			return false;
		}
		wchar_t cmd[256] = {};
		_snwprintf_s(cmd, _TRUNCATE,
			L"netsh.exe interface ipv6 set interface \"%s\" %s",
			name.c_str(), enabled ? L"enabled" : L"disabled");
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok ? (enabled ? I18N(u8"已啟用 IPv6") : I18N(u8"已停用 IPv6")) : I18N(u8"IPv6 設定失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	bool SetNicPowerSaveEnabled(bool enabled)
	{
		const Snapshot snap = GetSnapshot();
		if (snap.primary_if_index == 0) {
			SetLastAction(u8"找不到作用中的網路介面卡");
			return false;
		}
		wchar_t cmd[360] = {};
		_snwprintf_s(cmd, _TRUNCATE,
			L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"Set-NetAdapterPowerManagement -InterfaceIndex %u -AllowComputerToTurnOffDevice %s\"",
			snap.primary_if_index, enabled ? L"Enabled" : L"Disabled");
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok ? (enabled ? I18N(u8"已允許網卡省電") : I18N(u8"已關閉網卡省電")) : I18N(u8"網卡電源設定失敗"));
		if (ok) {
			RequestRefresh();
		}
		return ok;
	}

	void RequestTraceroute()
	{
		if (g_trace_running.load()) {
			return;
		}
		if (g_trace_worker.joinable()) {
			g_trace_worker.join();
		}
		g_trace_worker = std::thread([] { RunTracerouteWorker(); });
	}

	TracerouteSnapshot GetTraceroute()
	{
		std::lock_guard<std::mutex> lock(g_trace_mutex);
		TracerouteSnapshot snap = g_traceroute;
		snap.running = g_trace_running.load();
		return snap;
	}

	bool IsTracerouteRunning()
	{
		return g_trace_running.load();
	}

	std::string BuildTracerouteReportText()
	{
		const TracerouteSnapshot tr = GetTraceroute();
		std::ostringstream ss;
		ss << I18N(u8"HP CLEANER++ 路由追蹤報告\n");
		ss << I18N(u8"目標: ") << (tr.target[0] ? tr.target : "—") << "\n";
		if (tr.status_text[0] != '\0') {
			ss << I18N(u8"狀態: ") << tr.status_text << "\n";
		}
		if (tr.tested_at_ms > 0) {
			ss << I18N(u8"測試時間 (ms): ") << tr.tested_at_ms << "\n";
		}
		ss << I18N(u8"跳數: ") << tr.hops.size() << "\n";
		ss << "----------------------------------------\n";
		for (const auto& hop : tr.hops) {
			ss << hop.hop << "\t"
				<< (hop.addr[0] ? hop.addr : "*") << "\t";
			if (hop.rtt_ms >= 0) {
				ss << hop.rtt_ms << " ms";
			}
			else {
				ss << "*";
			}
			if (hop.status[0] != '\0') {
				ss << "\t" << hop.status;
			}
			if (hop.is_destination) {
				ss << I18N(u8" [目的地]");
			}
			ss << "\n";
		}
		return ss.str();
	}

	void RequestNetworkDiagnostics()
	{
		if (g_diag_running.load()) {
			return;
		}
		if (g_diag_worker.joinable()) {
			g_diag_worker.join();
		}
		g_diag_worker = std::thread([] { RunNetworkDiagnosticsWorker(); });
	}

	bool IsNetworkDiagnosticsRunning()
	{
		return g_diag_running.load();
	}

	static bool SetDnsCacheRegistryDword(const wchar_t* value_name, DWORD val)
	{
		if (!HCleanIsRunningAsAdmin()) {
			return false;
		}
		HKEY key = nullptr;
		const LONG open_err = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters",
			0, KEY_SET_VALUE, &key);
		if (open_err != ERROR_SUCCESS) {
			return false;
		}
		const LONG err = RegSetValueExW(key, value_name, 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&val), sizeof(val));
		RegCloseKey(key);
		return err == ERROR_SUCCESS;
	}

	bool SetLlmnrEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"LLMNR 設定需管理員權限");
			return false;
		}
		const bool ok = SetDnsCacheRegistryDword(L"EnableMulticast", enabled ? 1u : 0u);
		SetLastAction(ok ? (enabled ? I18N(u8"已啟用 LLMNR") : I18N(u8"已停用 LLMNR")) : I18N(u8"LLMNR 設定失敗"));
		if (ok) {
			RequestNetworkDiagnostics();
		}
		return ok;
	}

	bool SetNetbiosOverTcpEnabled(bool enabled)
	{
		const Snapshot snap = GetSnapshot();
		if (snap.primary_if_index == 0) {
			SetLastAction(u8"找不到作用中的網路介面");
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"NetBIOS 設定需管理員權限");
			return false;
		}
		wchar_t cmd[420] = {};
		_snwprintf_s(cmd, _TRUNCATE,
			L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"%s-NetAdapterBinding -InterfaceIndex %u -ComponentID ms_netbios -Confirm:$false\"",
			enabled ? L"Enable" : L"Disable",
			snap.primary_if_index);
		const bool ok = RunHiddenCommand(cmd);
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用 NetBIOS over TCP/IP") : I18N(u8"已停用 NetBIOS over TCP/IP"))
			: I18N(u8"NetBIOS 設定失敗"));
		if (ok) {
			RequestNetworkDiagnostics();
		}
		return ok;
	}

	static void RunFullDnsDiagnosticsWorker()
	{
		g_full_dns_running = true;
		RequestRefresh();
		if (g_dns_worker.joinable()) {
			g_dns_worker.join();
		}
		RunDnsBenchmarkWorker();
		RequestLinkTest();
		if (g_link_worker.joinable()) {
			g_link_worker.join();
		}
		RequestNetworkDiagnostics();
		if (g_diag_worker.joinable()) {
			g_diag_worker.join();
		}
		g_full_dns_running = false;
		SetLastAction(u8"全面 DNS 診斷完成");
	}

	void SetDnsBenchmarkDomain(const char* domain)
	{
		if (domain == nullptr || domain[0] == '\0') {
			strncpy_s(g_dns_benchmark_domain, "www.msftconnecttest.com", _TRUNCATE);
		}
		else {
			strncpy_s(g_dns_benchmark_domain, domain, _TRUNCATE);
		}
		SaveNetworkCacheInternal();
	}

	const char* GetDnsBenchmarkDomain()
	{
		return g_dns_benchmark_domain;
	}

	void RequestFullDnsDiagnostics()
	{
		if (g_full_dns_running.load()) {
			return;
		}
		if (g_full_dns_worker.joinable()) {
			g_full_dns_worker.join();
		}
		g_full_dns_worker = std::thread([] { RunFullDnsDiagnosticsWorker(); });
	}

	bool IsFullDnsDiagnosticsRunning()
	{
		return g_full_dns_running.load();
	}

	static void RunFullNetworkTestWorker()
	{
		g_full_net_running = true;
		RequestRefresh();
		if (g_worker.joinable()) {
			g_worker.join();
		}
		RequestLinkTest();
		if (g_link_worker.joinable()) {
			g_link_worker.join();
		}
		if (g_dns_worker.joinable()) {
			g_dns_worker.join();
		}
		RunDnsBenchmarkWorker();
		RequestSpeedTest();
		if (g_speed_worker.joinable()) {
			g_speed_worker.join();
		}
		RequestUploadSpeedTest();
		if (g_speed_worker.joinable()) {
			g_speed_worker.join();
		}
		RequestNetworkDiagnostics();
		if (g_diag_worker.joinable()) {
			g_diag_worker.join();
		}
		g_full_net_running = false;
		SetLastAction(u8"全面網路測試完成");
	}

	void RequestFullNetworkTest()
	{
		if (g_full_net_running.load()) {
			return;
		}
		if (g_full_net_worker.joinable()) {
			g_full_net_worker.join();
		}
		g_full_net_worker = std::thread([] { RunFullNetworkTestWorker(); });
	}

	bool IsFullNetworkTestRunning()
	{
		return g_full_net_running.load();
	}

	bool SetParallelDnsQueriesEnabled(bool enabled)
	{
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"平行 DNS 查詢設定需管理員權限");
			return false;
		}
		const bool ok = SetDnsCacheRegistryDword(L"DisableParallelAorAAAA", enabled ? 0u : 1u);
		SetLastAction(ok
			? (enabled ? I18N(u8"已啟用平行 A/AAAA 查詢") : I18N(u8"已停用平行 A/AAAA 查詢"))
			: I18N(u8"平行 DNS 查詢設定失敗"));
		if (ok) {
			RequestNetworkDiagnostics();
		}
		return ok;
	}

	bool SetDohPolicy(int policy)
	{
		if (policy < 0 || policy > 2) {
			SetLastAction(u8"DoH 策略值無效");
			return false;
		}
		if (!HCleanIsRunningAsAdmin()) {
			SetLastAction(u8"DoH 策略設定需管理員權限");
			return false;
		}
		const bool ok = SetDnsCacheRegistryDword(L"DohPolicy", static_cast<DWORD>(policy));
		static const char* labels[] = { I18N(u8"允許一般 DNS"), I18N(u8"強制 DoH"), I18N(u8"停用 DoH") };
		if (ok) {
			char msg[96] = {};
			snprintf(msg, sizeof(msg), I18N(u8"DoH 策略已設為：%s"), labels[policy]);
			SetLastAction(msg);
			RequestNetworkDiagnostics();
		}
		else {
			SetLastAction(u8"DoH 策略設定失敗");
		}
		return ok;
	}

	int CheckHostsFileExtra()
	{
		return CheckHostsFileExtraInternal();
	}

	void SaveNetworkCache()
	{
		SaveNetworkCacheInternal();
	}

	uint64_t GetCacheSavedAtMs()
	{
		return g_cache_saved_at_ms;
	}

}