"""Read timestamped LRC lyrics from the user's local music library."""
from __future__ import annotations

import os
import re
from pathlib import Path


DEFAULT_LYRICS_ROOT = Path(os.environ.get('FFV_LYRICS_ROOT', str(Path(__file__).resolve().parent/'media-work'/'lyrics')))
TIME_TAG = re.compile(r"\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\]")
OFFSET_TAG = re.compile(r"\[offset:([+-]?\d+)\]", re.IGNORECASE)


def parse_lrc_text(text: str) -> list[dict]:
    """Return sorted lyric rows as {time, text}; metadata-only rows are ignored."""
    offset_match = OFFSET_TAG.search(text)
    offset_seconds = int(offset_match.group(1)) / 1000 if offset_match else 0.0
    rows: list[dict] = []
    for line in text.splitlines():
        matches = list(TIME_TAG.finditer(line))
        if not matches:
            continue
        lyric = TIME_TAG.sub("", line).strip()
        if not lyric:
            continue
        for match in matches:
            fraction = match.group(3) or "0"
            fraction_seconds = int(fraction) / (10 ** len(fraction))
            seconds = int(match.group(1)) * 60 + int(match.group(2)) + fraction_seconds
            rows.append({"time": round(max(0.0, seconds + offset_seconds), 3), "text": lyric})
    rows.sort(key=lambda row: row["time"])
    return rows


def _read_text(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", errors="replace")


def find_lrc(display_name: str, root: Path | None = None) -> Path | None:
    music_root = Path(root or os.environ.get("FFV_LYRICS_ROOT") or DEFAULT_LYRICS_ROOT)
    if not music_root.is_dir():
        return None
    stem = Path(display_name).stem
    direct = music_root / f"{stem}.lrc"
    if direct.is_file():
        return direct
    folded = f"{stem}.lrc".casefold()
    return next((path for path in music_root.iterdir()
                 if path.is_file() and path.name.casefold() == folded), None)


def load_lyrics(display_name: str, root: Path | None = None) -> tuple[list[dict], str]:
    path = find_lrc(display_name, root)
    if path is None:
        return [], ""
    return parse_lrc_text(_read_text(path)), path.name
