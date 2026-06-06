#!/usr/bin/env python3
"""Apply manual JA overrides for strings Google returned unchanged."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from batch_translate_ja import load_existing, save_phrases

# zh-TW key -> ja-JP (preserve printf spec order/count)
MANUAL: dict[str, str] = {
    "%.1f 秒": "%.1f 秒",
    "%c: 結果": "%c: 結果",
    "%d%% 分散": "%d%% 分散",
    "%s · %zu 行": "%s · %zu 行",
    "HP CLEANER++": "HP CLEANER++",
    "~%d 秒": "~%d 秒",
    "使用中": "使用中",
    "分散": "分散",
    "失敗": "失敗",
    "容量": "容量",
    "強制 DoH": "DoH を強制",
    "成功 %d/%d": "成功 %d/%d",
    "時間": "時間",
    "普通": "標準",
    "更新中…": "更新中…",
    "未使用": "未使用",
    "未知": "不明",
    "準備中…": "準備中…",
    "百度": "バイドゥ",
    "統計": "統計",
    "自動": "自動",
    "自動 DoH": "自動 DoH",
    "自動取得": "自動取得",
    "自動目的地": "自動宛先",
    "色": "色",
    "設定": "設定",
    "詳細": "詳細",
    "速度曲線": "スピード曲線",
    "進行中": "進行中",
    "電源": "電源",
    "非計量": "非従量制",
}


def main() -> int:
    phrases = load_existing()
    updated = 0
    for k, v in MANUAL.items():
        if phrases.get(k) != v:
            phrases[k] = v
            updated += 1
    save_phrases(phrases)
    print(f"manual overrides applied: {updated} (total cache {len(phrases)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
