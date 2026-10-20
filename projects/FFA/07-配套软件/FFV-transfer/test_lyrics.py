from pathlib import Path
from tempfile import TemporaryDirectory

from lyrics import load_lyrics, parse_lrc_text


def main():
    rows = parse_lrc_text("[ar:测试]\n[offset:100]\n[00:01.20][00:03.500]第一句\n[00:05]第二句")
    assert rows == [
        {"time": 1.3, "text": "第一句"},
        {"time": 3.6, "text": "第一句"},
        {"time": 5.1, "text": "第二句"},
    ]
    with TemporaryDirectory() as folder:
        root = Path(folder)
        (root / "歌名.LRC").write_text("[00:00.5]开始", encoding="utf-8-sig")
        loaded, source = load_lyrics("歌名.ncm", root)
        assert loaded == [{"time": 0.5, "text": "开始"}]
        assert source.casefold() == "歌名.lrc"
        assert load_lyrics("没有.mp3", root) == ([], "")
    print("lyrics parser and sibling lookup ok")


if __name__ == "__main__":
    main()
