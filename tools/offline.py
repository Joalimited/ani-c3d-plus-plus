"""Download a compressed episode or play one from the offline library."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlsplit

LIBRARY = Path.home() / "Videos" / "ani-c3d"


def media_input(url, referer):
    if urlsplit(url).scheme not in {"http", "https"} or any(c in referer for c in "\r\n"):
        raise ValueError("Invalid media source")
    hls = ["-f", "hls", "-allowed_extensions", "ALL", "-allowed_segment_extensions", "ALL", "-extension_picky", "0"] if "m3u8" in url.lower() else []
    return [*hls, "-protocol_whitelist", "http,https,tcp,tls,crypto",
            "-referer", referer, "-i", url]


def destination(name, ep, mode):
    title = re.sub(r'[^\w .()-]', '_', name).strip(". ")[:100] or "Anime"
    return LIBRARY / f"{title} - Episode {ep} ({mode}).mkv"


def save(stream, ep, mode):
    LIBRARY.mkdir(parents=True, exist_ok=True)
    dest = destination(stream["title"], ep, mode)
    if dest.exists():
        print(f"Already downloaded: {dest}")
        return
    args = media_input(stream["url"], stream["referer"])
    if stream["subtitle"]:
        args += media_input(stream["subtitle"], stream["referer"])
    args += ["-map", "0:v:0", "-map", "0:a:0?", "-map",
             "1:s:0" if stream["subtitle"] else "0:s:0?",
             "-c:v", "libx264", "-preset", "veryfast", "-crf", "25",
             "-c:a", "aac", "-b:a", "112k", "-c:s", "copy"]
    with tempfile.TemporaryDirectory(dir=LIBRARY) as temp:
        partial = Path(temp) / "episode.mkv"
        cmd = ["ffmpeg", "-nostdin", "-hide_banner", "-nostats", "-progress", "pipe:1",
               "-xerror", *args, str(partial)]
        duration = None
        first_input = False
        percent = -1
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace") as proc:
            for line in proc.stdout:
                if line.startswith("Input #"):
                    first_input = line.startswith("Input #0,")
                if first_input and line.lstrip().startswith("Duration:"):
                    match = re.search(r"Duration: (\d+):(\d+):([\d.]+)", line)
                    if match:
                        h, m, sec = map(float, match.groups())
                        duration = 3600*h + 60*m + sec
                if duration and line.startswith("out_time_us="):
                    value = line.partition("=")[2].strip()
                    if value.isdigit():
                        value = min(99, int(int(value) / (10000 * duration)))
                        if value > percent:
                            percent = value
                            print(f"PROGRESS {percent}", flush=True)
            if proc.wait():
                raise ValueError(f"Encoding episode {ep} failed")
        os.link(partial, dest)
    print(f"Saved {dest}")


def main():
    parser = argparse.ArgumentParser(prog="ani-c3d")
    commands = parser.add_subparsers(dest="action", required=True)
    dl = commands.add_parser("download")
    dl.add_argument("--id", help=argparse.SUPPRESS)
    dl.add_argument("title")
    dl.add_argument("episode", type=lambda s: s if re.fullmatch(r"\d+(?:\.\d+)?", s)
                    else parser.error("Use a numeric episode number"))
    dl.add_argument("--audio", choices=["sub", "dub"], default="sub")
    commands.add_parser("offline")
    args = parser.parse_args()
    if args.action == "offline":
        files = sorted(LIBRARY.glob("*.mkv"))
        if not files:
            print("No downloads yet. Use: ani-c3d download \"Show Title\" 1")
            return
        result = subprocess.run(["zenity", "--list", "--title=Offline anime",
                                 "--width=650", "--height=450", "--column=Episode",
                                 *[p.name for p in files]], capture_output=True, text=True)
        if result.returncode == 0:
            choice = result.stdout.rstrip("\n")
            path = next(p for p in files if p.name == choice)
            subprocess.run(["mpv", "--", str(path)], check=True)
        return
    binary = Path(__file__).resolve().parents[1] / "build/native/ani-c3d"
    if destination(args.title, args.episode, args.audio).is_file():
        print("Already downloaded")
        return
    result = subprocess.check_output([binary, "--download-source", args.title,
                                      args.episode, args.audio, *([args.id] if args.id else [])], text=True)
    save(json.loads(result), args.episode, args.audio)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"Offline video: {exc}")
