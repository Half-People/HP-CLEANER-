#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Batch-translate zh-TW strings to en-US for embedding in Hi18nBuiltinPages."""
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_i18n_pages import to_en  # noqa: E402

OUT_PATH = ROOT / "tools" / "en_phrases_batch.json"
EXTRACTED = ROOT / "tools" / "i18n_extracted.json"
CHUNK = 50
SLEEP_SEC = 0.15
BATCH_PAUSE_SEC = 1.0


def needs_translation(s: str) -> bool:
    if not re.search(r"[\u4e00-\u9fff]", s):
        return False
    en = to_en(s)
    return bool(re.search(r"[\u4e00-\u9fff]", en)) or en == s


def load_existing() -> dict[str, str]:
    if OUT_PATH.exists():
        try:
            data = json.loads(OUT_PATH.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return {k: v for k, v in data.items() if isinstance(v, str)}
        except Exception:
            pass
    return {}


def save_phrases(phrases: dict[str, str]) -> None:
    OUT_PATH.write_text(
        json.dumps(phrases, ensure_ascii=False, indent=2, sort_keys=True),
        encoding="utf-8",
    )


def translate_batch(texts: list[str]) -> list[str]:
    from deep_translator import GoogleTranslator

    translator = GoogleTranslator(source="zh-TW", target="en")
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
    strings = json.loads(EXTRACTED.read_text(encoding="utf-8"))
    phrases = load_existing()
    pending = [s for s in strings if needs_translation(s) and s not in phrases]
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
        and not re.search(r"[\u4e00-\u9fff]", phrases.get(s, to_en(s)))
    )
    print(f"Done. phrases={len(phrases)} projected_full_en~{full}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
