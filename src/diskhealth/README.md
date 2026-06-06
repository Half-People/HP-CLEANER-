<p align="center">
  <strong>English</strong> &nbsp;|&nbsp; <a href="README.zh-TW.md">繁體中文</a>
</p>
<p align="center">
  <img src="../../assets/head.png" alt="HalfPeople Studio · HP CLEANER++" width="560" />
</p>
<p align="center"><sub>Windows system management for gamers and developers</sub></p>

---

# diskhealth — Drive health & SMART

The **Disk health** tab: physical drive list, SMART attributes, charts, and optional diagnostics.

---

## Structure

| File | Role |
|------|------|
| `DiskHealthScan.*` | Worker scan: IOCTL SMART, NVMe health log, USB notes |
| `DiskHealthUI.*` | Gauges, attribute cards, ImPlot charts, test tabs |
| `DiskHealthPage.cpp` | Deferred scan on first UI frame |
| `DiskHealthTest.*` | Speed test & bad-sector matrix jobs |

---

## Scan pipeline

```
Init() ──no full scan──► UI first frame ──► RequestRescan()
                                              │
                                              ▼
                                    For each PhysicalDriveN
                                              │
                         ┌────────────────────┼────────────────────┐
                         ▼                    ▼                    ▼
                    ATA SMART            NVMe health log      USB status note
                         │                    │                    │
                         └──────────► DriveInfo + smart_attributes[]
                                              │
                                              ▼
                                    DiskHealthUI (I18N at display)
```

---

## SMART attribute names

| Storage | Format | Display |
|---------|--------|---------|
| Known ID | zh-TW key via `SmartIdName()` | `I18N(name_utf8)` in UI |
| NVMe synthetic | zh-TW key in `AppendNvmeSmartAttr` | same |
| Sensor 240–247 | Formatted at display | `SnprintfI18n("溫度感測器 %d")` |

---

## UI sections

| Section | Component |
|---------|-----------|
| Drive list | Left panel cards with health score |
| Detail hero | Model, bus type, volume letters |
| SMART grid | `DrawSmartAttributeCard` per attribute |
| Charts | ImPlot risk bars |
| Diagnostics | Speed / bad-sector workers |

---

## Admin note

Some SMART data requires **elevated** administrator rights; UI shows `needs_admin_hint` when partial data is returned.

---

## See also

- ImPlot: [../../third_party/implot/README.md](../../third_party/implot/README.md)
- 中文: [README.zh-TW.md](README.zh-TW.md)
