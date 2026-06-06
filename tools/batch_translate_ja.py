#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Batch-translate zh-TW strings to ja-JP for embedding in Hi18nBuiltinPages."""
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_i18n_pages import to_ja  # noqa: E402

OUT_PATH = ROOT / "tools" / "ja_phrases_batch.json"
BACKUP_PATH = Path.home() / "ja_phrases_batch.backup.json"
EXTRACTED = ROOT / "tools" / "i18n_extracted.json"
CHUNK = 40
SLEEP_SEC = 0.2
BATCH_PAUSE_SEC = 1.2


def needs_translation(s: str) -> bool:
    if not re.search(r"[\u4e00-\u9fff]", s):
        return False
    ja = to_ja(s)
    return bool(re.search(r"[\u4e00-\u9fff]", ja)) or ja == s


def load_existing() -> dict[str, str]:
    for path in (BACKUP_PATH, OUT_PATH):
        if not path.exists() or path.stat().st_size == 0:
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return {k: v for k, v in data.items() if isinstance(v, str)}
        except Exception:
            continue
    return {}


def save_phrases(phrases: dict[str, str]) -> None:
    payload = json.dumps(phrases, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8")

    def write_path(p: Path) -> None:
        tmp = p.with_suffix(p.suffix + ".tmp")
        with open(tmp, "wb") as f:
            for i in range(0, len(payload), 8192):
                f.write(payload[i : i + 8192])
        if p.exists():
            p.unlink(missing_ok=True)
        tmp.replace(p)

    write_path(BACKUP_PATH)
    write_path(OUT_PATH)


def translate_batch(texts: list[str]) -> list[str]:
    from deep_translator import GoogleTranslator

    translator = GoogleTranslator(source="zh-TW", target="ja")
    try:
        results = translator.translate_batch(texts)
        if results and len(results) == len(texts):
            return [r if r else t for r, t in zip(results, texts)]
    except Exception as ex:
        print(f"  batch failed ({ex!s}), falling back to single")
    out: list[str] = []
    for t in texts:
        try:
            r = translator.translate(t)
            out.append(r if r else t)
        except Exception as ex:
            print(f"  warn: {ex!s} :: {t[:48]}")
            out.append(t)
        time.sleep(SLEEP_SEC)
    return out


def main() -> int:
    if not EXTRACTED.exists():
        print("Run gen_i18n_pages.py first to create i18n_extracted.json")
        return 1

    strings = json.loads(EXTRACTED.read_text(encoding="utf-8"))
    phrases = load_existing()
    pending = [
        s
        for s in strings
        if needs_translation(s) and (s not in phrases or phrases.get(s) == s)
    ]
    print(f"total={len(strings)} cached={len(phrases)} pending={len(pending)}", flush=True)
    if not pending:
        print("Nothing to translate.", flush=True)
        return 0

    try:
        import deep_translator  # noqa: F401
    except ImportError:
        print("Installing deep-translator...")
        import subprocess

        subprocess.check_call([sys.executable, "-m", "pip", "install", "deep-translator", "-q"])
        import deep_translator  # noqa: F401

    done = 0
    for i in range(0, len(pending), CHUNK):
        chunk = pending[i : i + CHUNK]
        print(f"translate {i + 1}-{i + len(chunk)} / {len(pending)}", flush=True)
        results = translate_batch(chunk)
        for src, dst in zip(chunk, results):
            phrases[src] = dst
        done += len(chunk)
        save_phrases(phrases)
        print(f"  saved ({done}/{len(pending)})", flush=True)
        time.sleep(BATCH_PAUSE_SEC)

    full = sum(
        1
        for s in strings
        if re.search(r"[\u4e00-\u9fff]", s)
        and not re.search(r"[\u4e00-\u9fff]", phrases.get(s, to_ja(s)))
    )
    print(f"Done. phrases={len(phrases)} projected_untranslated_ja~{full}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
