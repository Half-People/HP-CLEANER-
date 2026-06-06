<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# optimize — System optimization

The **Optimize** tab: startup programs, services, power plans, one-click presets, and network utilities.

---

## Structure

| File | Role |
|------|------|
| `OptimizeScan.*` | Master snapshot: startups, services, power, disk presets |
| `OptimizeNetworkScan.*` | Adapters, DNS, speed test, connections, ARP |
| `OptimizePage.cpp` | Page registration |
| `OptimizePageUI.*` | Large multi-subtab ImGui UI (~6000 lines) |
| `OptimizeStartupIcon.*` | Extract exe icons for startup list |

---

## OptimizePageUI sub-tabs

| Sub-tab | Data source | User actions |
|---------|-------------|--------------|
| Overview | `OptimizeScan` snapshot | Quick stats, apply preset |
| Startup | `snap.startups` | Enable/disable, tooltips |
| Services | `snap.services` | Start type hints, tooltips |
| System | Power, visual effects | Toggle performance options |
| Network | `OptimizeNetworkScan` | DNS bench, trace, flush |
| Storage | Disk optimization | Analyze / trim volumes |

Display helper: `UiTxt(zh_key)` wraps `I18N()` for snapshot strings.

---

## Background scan flow

```
OptimizeScan::RequestScan()
        │
        ▼ worker thread
Enumerate registry Run keys + Startup folders
Query services · read power plan GUID
        │
        ▼
Snapshot (mutex) ──► OptimizePageUI reads each frame
```

---

## See also

- Network-heavy logic: `OptimizeNetworkScan.cpp`
- Module map: [../README.md](../README.md)
- 中文: [README.zh-TW.md](README.zh-TW.md)
