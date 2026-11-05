"""Build and exercise the shipping frontend using its serialized MENU protocol.

All generated configuration, fixtures, caches and transcripts stay under
artifacts/static-sources/menu. No ROMs, cores or user configuration are used.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import queue
import re
import hashlib
import shutil
import traceback
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "artifacts/static-sources/menu"
MSBUILD = (r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
           r"\MSBuild\Current\Bin\MSBuild.exe")


def build(msbuild):
    OUT.mkdir(parents=True, exist_ok=True)
    command = [msbuild, "pkg/msvc/msvc-2022/RetroArch-msvc2022.vcxproj",
               "/p:Configuration=Release", "/p:Platform=x64",
               "/p:OutDir=" + str(OUT / "bin") + os.sep,
               "/p:IntDir=" + str(OUT / "obj") + os.sep,
               "-nodeReuse:false", "/v:minimal"]
    with (OUT / ("build-" + str(time.time_ns()) + ".log")).open("wb") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log,
                                stderr=subprocess.STDOUT)
    print("Full frontend build exit:", result.returncode, flush=True)
    result.check_returncode()


def fixture_url(invalid=False, second=False):
    catalog = {"schema_version": 1,
               "source": {"id": "gg-fiction", "name": "GG Fiction Catalog"},
               "games": [{"id": "alpha", "platform": "nes",
                          "original_language": "en", "original_name": "Alpha Fiction",
                          "localized": {"zh-Hans": {"name": "\u661f\u5149\u793a\u4f8b"},
                                        "en": {"name": "Alpha Fiction",
                                               "description": "A fictional catalog entry, not a ROM."}},
                          "region": "world", "version": "1.0",
                          "external_ids": {"demo": "fiction-only"}},
                         {"id": "b" * 128, "platform": "fiction-machine",
                          "original_name": "Beta Fiction"}]}
    if second:
        catalog["source"]["name"] = "GG Second Catalog"
        catalog["games"] = [{"id": "alpha", "platform": "gb",
                             "original_name": "Alpha Second"}]
    raw = b"not json" if invalid else json.dumps(catalog, separators=(",", ":"),
                                                ensure_ascii=False).encode("utf-8")
    url = "https://httpbingo.org/base64/" + base64.urlsafe_b64encode(raw).decode("ascii")
    assert len(url) <= 2048
    return url


class Frontend:
    def __init__(self, exe, directory, driver, language=0, kiosk=False, assets=None):
        directory.mkdir(parents=True, exist_ok=True)
        self.directory = directory
        self.driver = driver
        self.config = directory / "test.cfg"
        values = {
            "stdin_cmd_enable": "true", "network_cmd_enable": "false",
            "quit_press_twice": "false", "pause_nonactive": "false",
            "config_save_on_exit": "true", "menu_show_start_screen": "false",
            "menu_driver": driver, "video_driver": "gl", "audio_driver": "null",
            "input_driver": "dinput", "video_fullscreen": "false",
            "video_windowed_fullscreen": "false", "video_window_save_positions": "false",
            "video_window_custom_size_enable": "true", "video_window_width": "960",
            "video_window_height": "720", "video_threaded": "false",
            "threaded_data_runloop_enable": "false", "user_language": str(language),
            "kiosk_mode_enable": "true" if kiosk else "false",
            "menu_show_online_updater": "false", "menu_enable_widgets": "false",
            "menu_content_show_playlists": "true", "content_show_playlists": "true",
        }
        for key in ("rgui_config_directory", "savefile_directory", "savestate_directory",
                    "system_directory", "screenshot_directory", "log_dir",
                    "libretro_directory", "libretro_info_path", "assets_directory",
                    "cache_directory", "playlist_directory", "content_history_directory"):
            values[key] = directory.as_posix()
        if assets:
            values["assets_directory"] = Path(assets).resolve().as_posix()
        self.config.write_text("".join(k + ' = "' + v + '"\n' for k, v in values.items()),
                               encoding="utf-8")
        stamp = str(time.time_ns())
        self.log = (directory / (stamp + ".stderr.log")).open("wb")
        self.transcript = (directory / (stamp + ".menu.txt")).open("w", encoding="utf-8")
        self.proc = subprocess.Popen([str(exe), "--menu", "--config", str(self.config)],
                                     cwd=directory, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.log)
        self.chunks = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        self.last = ""

    def _read(self):
        while True:
            data = os.read(self.proc.stdout.fileno(), 65536)
            self.chunks.put(data)
            if not data:
                break

    def command(self, text, timeout=15):
        self.transcript.write("> " + text + "\n")
        self.transcript.flush()
        self.proc.stdin.write((text + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        raw = b""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                data = self.chunks.get(timeout=0.1)
            except queue.Empty:
                continue
            if not data:
                raise AssertionError("Frontend exited: " + str(self.proc.poll()))
            raw += data
            if (re.search(rb"COUNT \d+\r?\n", raw) or raw.endswith(b"CLOSED\n")
                    or (text == "GET_STATUS" and b"CONTENTLESS" in raw)):
                self.last = raw.decode("utf-8", errors="replace")
                self.transcript.write(self.last)
                self.transcript.flush()
                return self.last
        self.transcript.write(raw.decode("utf-8", errors="replace"))
        self.transcript.flush()
        raise AssertionError("Protocol timeout: " + text)

    def show(self):
        return self.command("MENU show")

    def choose(self, visible):
        if self.driver == "glui":
            # MaterialUI inhibits OK until the selected row is laid out onscreen.
            time.sleep(0.25)
            self.show()
        rows = re.findall(r"^(\d+)\t([^\t]*)\t", self.last, re.M)
        matches = [int(i) for i, name in rows if visible == name]
        if not matches:
            matches = [int(i) for i, name in rows if name.startswith(visible)]
        assert len(matches) == 1, (visible, self.last)
        if self.driver == "glui":
            self.command("MENU select " + str(matches[0]))
            time.sleep(0.25)
        return self.command("MENU a " + str(matches[0]))

    def selected(self):
        index = re.search(r"SELECTED (\d+)", self.last)[1]
        return re.search(r"^" + index + r"\t([^\t]*)\t", self.last, re.M)[1]

    def wait(self, text, timeout=40):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if text in self.last:
                return self.last
            time.sleep(0.1)
            self.show()
        raise AssertionError(("Missing", text, self.last))

    def close(self):
        try:
            if self.proc.poll() is None:
                try:
                    self.proc.stdin.write(b"QUIT\n")
                    self.proc.stdin.flush()
                except BrokenPipeError:
                    pass
                try:
                    self.proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=10)
                    raise AssertionError("Frontend failed to exit without waiting")
            assert self.proc.returncode == 0, self.proc.returncode
            saved = self.config.read_text(encoding="utf-8")
            assert 'menu_driver = "' + self.driver + '"' in saved, self.driver
        finally:
            self.transcript.write("EXIT " + str(self.proc.returncode) + "\n")
            self.transcript.close()
            self.log.close()


def exercise(exe, directory, driver, assets):
    f = Frontend(exe, directory, driver, assets=assets)
    try:
        time.sleep(2)
        f.command("MENU open")
        assert "My Game Library" in f.last
        f.choose("Game Sources")
        f.wait("Ready / cached")
        assert "No sources yet" in f.last
        f.choose("Add HTTPS Source")
        assert "KEYBOARD" in f.last
        f.command("MENU type http://example.org/invalid")
        f.wait("Invalid URL")
        assert "GG Fiction Catalog" not in f.last
        f.choose("Add HTTPS Source")
        f.command("MENU type " + fixture_url(True))
        f.wait("Invalid catalog")
        assert "GG Fiction Catalog" not in f.last
        f.choose("Add HTTPS Source")
        f.command("MENU type " + fixture_url())
        f.wait("GG Fiction Catalog [Enabled]")
        assert (directory / "gamegate/sources/registry.json").is_file(), directory
        f.choose("GG Fiction Catalog [Enabled]")
        f.wait("Last Successful Update")
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        f.choose("Alpha Fiction [NES]")
        assert "Name: Alpha Fiction" in f.last
        assert "A fictional catalog entry" in f.last
        assert "No download address provided" in f.last
        assert "downloading is not available" in f.last
        assert "Name Language: en" in f.last
        f.command("MENU b")
        assert f.selected().startswith("Alpha Fiction"), f.last
        f.choose("Beta Fiction")
        assert "No description available" in f.last
        assert "Unrecognized platform" in f.last
        f.command("MENU b")
        assert f.selected().startswith("Beta Fiction"), f.last
        f.command("MENU b")
        f.choose("Refresh Catalog")
        f.wait("Operation completed")
        f.choose("Disable Source")
        f.wait("State: Disabled")
        assert "Browse Cached Games" not in f.last
        assert "Refresh Catalog" not in f.last
        f.choose("Enable Source")
        f.wait("State: Enabled")
        f.choose("Remove Source...")
        assert "Confirm Removal" in f.last
        f.choose("Cancel Removal")
        assert "Source Details" in f.last
        f.choose("Remove Source...")
        f.choose("Confirm Removal")
        f.wait("No sources yet")
        assert not json.loads((directory / "gamegate/sources/registry.json").read_text())["sources"]
        f.choose("Add HTTPS Source")
        f.command("MENU type " + fixture_url())
        f.wait("GG Fiction Catalog [Enabled]")
        failure_directory = directory / "refresh-failure"
        shutil.copytree(directory / "gamegate", failure_directory / "gamegate")
        f.choose("Add HTTPS Source")
        f.command("MENU type " + fixture_url(second=True))
        f.wait("GG Second Catalog [Enabled]")
        f.choose("GG Second Catalog [Enabled]")
        f.choose("Browse Cached Games")
        f.wait("Alpha Second [Game Boy]")
        assert "Alpha Fiction" not in f.last
        f.choose("Alpha Second")
        assert "Name: Alpha Second" in f.last
        f.command("MENU b")
        f.command("MENU b")
        f.choose("Remove Source...")
        f.choose("Confirm Removal")
        f.wait("GG Fiction Catalog [Enabled]")
        assert "GG Second Catalog" not in f.last
        f.choose("GG Fiction Catalog [Enabled]")
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        assert "Alpha Second" not in f.last
        f.command("MENU b")
        f.command("MENU b")
        f.command("MENU b")
        f.choose("My Game Library")
        assert "Import Content" in f.last
        f.command("MENU b")
        assert "CONTENTLESS" in f.command("GET_STATUS")
    finally:
        f.close()

    failed_refresh(exe, failure_directory, driver, assets)

    # Persisted cache is read through the real service after frontend restart.
    f = Frontend(exe, directory, driver, kiosk=True, assets=assets)
    try:
        time.sleep(2)
        f.command("MENU open")
        f.choose("Game Sources")
        f.wait("GG Fiction Catalog [Enabled]")
        assert "Add HTTPS Source" not in f.last
        f.choose("GG Fiction Catalog [Enabled]")
        assert "Refresh Catalog" not in f.last
        assert "Disable Source" not in f.last
        assert "Remove Source" not in f.last
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        f.choose("Alpha Fiction [NES]")
        assert "A fictional catalog entry" in f.last
    finally:
        f.close()

    f = Frontend(exe, directory, driver, language=4, assets=assets)
    try:
        time.sleep(2)
        f.command("MENU open")
        assert "Hauptmen" in f.last, f.last
        f.choose("Game Sources")
        f.wait("GG Fiction Catalog [Enabled]")
        f.choose("GG Fiction Catalog [Enabled]")
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        f.choose("Alpha Fiction [NES]")
        assert "Name Language: en" in f.last
        assert "Description Language: en" in f.last
    finally:
        f.close()

    f = Frontend(exe, directory, driver, language=12, assets=assets)
    try:
        time.sleep(2)
        f.command("MENU open")
        f.choose("\u6e38\u620f\u6e90")
        f.wait("GG Fiction Catalog")
        f.choose("GG Fiction Catalog")
        f.choose("\u6d4f\u89c8\u7f13\u5b58\u6e38\u620f")
        f.wait("\u661f\u5149\u793a\u4f8b")
        f.choose("\u661f\u5149\u793a\u4f8b")
        assert "zh-Hans" in f.last
        assert "A fictional catalog entry" in f.last
        assert "\u7b80\u4ecb\u8d44\u6599\u8bed\u8a00: en" in f.last
        f.command("MENU b")
        f.command("MENU b")
        f.choose("\u5237\u65b0\u76ee\u5f55")
        f.wait("\u64cd\u4f5c\u5df2\u5b8c\u6210")
        f.choose("\u505c\u7528\u6e38\u620f\u6e90")
        f.wait("\u72b6\u6001: \u5df2\u505c\u7528")
        assert "\u6d4f\u89c8\u7f13\u5b58\u6e38\u620f" not in f.last
        f.choose("\u542f\u7528\u6e38\u620f\u6e90")
        f.wait("\u72b6\u6001: \u5df2\u542f\u7528")
        f.choose("\u79fb\u9664\u6e38\u620f\u6e90")
        f.choose("\u53d6\u6d88\u79fb\u9664")
        assert "\u6e38\u620f\u6e90\u8be6\u60c5" in f.last
        f.choose("\u79fb\u9664\u6e38\u620f\u6e90")
        f.choose("\u786e\u8ba4\u79fb\u9664")
        f.wait("\u8fd8\u6ca1\u6709\u6e38\u620f\u6e90")
        assert "GG Fiction Catalog" not in f.last
        f.choose("\u6dfb\u52a0 HTTPS \u6e38\u620f\u6e90")
        f.command("MENU type http://example.org/invalid")
        f.wait("\u5730\u5740\u65e0\u6548")
        f.choose("\u6dfb\u52a0 HTTPS \u6e38\u620f\u6e90")
        f.command("MENU type " + fixture_url(True))
        f.wait("\u76ee\u5f55\u65e0\u6548")
        assert "GG Fiction Catalog" not in f.last
        f.choose("\u6dfb\u52a0 HTTPS \u6e38\u620f\u6e90")
        f.command("MENU type " + fixture_url())
        f.wait("GG Fiction Catalog [\u5df2\u542f\u7528]")
        f.choose("GG Fiction Catalog")
        f.choose("\u6d4f\u89c8\u7f13\u5b58\u6e38\u620f")
        f.wait("\u661f\u5149\u793a\u4f8b")
        f.choose("\u661f\u5149\u793a\u4f8b")
        assert "A fictional catalog entry" in f.last
        f.command("MENU b")
        assert f.selected().startswith("\u661f\u5149\u793a\u4f8b"), f.last
        f.command("MENU b")
        f.choose("\u79fb\u9664\u6e38\u620f\u6e90")
        f.choose("\u786e\u8ba4\u79fb\u9664")
        f.wait("\u8fd8\u6ca1\u6709\u6e38\u620f\u6e90")
        f.command("MENU b")
        f.choose("\u6211\u7684\u6e38\u620f\u5e93")
        assert "\u5bfc\u5165" in f.last
        assert "CONTENTLESS" in f.command("GET_STATUS")
    finally:
        f.close()
    print("PASS real MENU:", driver, directory, flush=True)


def lifecycle(exe, directory, driver, assets):
    for action in ("back", "quit"):
        f = Frontend(exe, directory / action, driver, assets=assets)
        try:
            time.sleep(2)
            f.command("MENU open")
            f.choose("Game Sources")
            assert "Initializing" in f.last, f.last
            assert "Cancel Operation" not in f.last
            if action == "back":
                f.command("MENU b")
                assert "My Game Library" in f.last
                f.choose("Game Sources")
                f.wait("Ready / cached")
        finally:
            f.close()
    print("PASS initialization back/quit:", driver, flush=True)


def failed_refresh(exe, directory, driver, assets):
    registry = directory / "gamegate/sources/registry.json"
    data = json.loads(registry.read_text())
    data["sources"][0]["url"] = "https://httpbingo.org/status/503"
    registry.write_text(json.dumps(data), encoding="utf-8")
    cache = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in registry.parent.glob("*.json") if p != registry}
    f = Frontend(exe, directory, driver, assets=assets)
    try:
        time.sleep(2)
        f.command("MENU open")
        f.choose("Game Sources")
        f.wait("GG Fiction Catalog [Enabled]")
        f.choose("GG Fiction Catalog [Enabled]")
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        f.command("MENU b")
        f.choose("Refresh Catalog")
        f.wait("Network or HTTP request failed")
        f.choose("Browse Cached Games")
        f.wait("Alpha Fiction [NES]")
        f.choose("Alpha Fiction")
        assert "A fictional catalog entry" in f.last
        assert cache == {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in registry.parent.glob("*.json") if p != registry}
        f.command("MENU b")
        f.command("MENU b")
        f.command("MENU b")
        f.choose("Add HTTPS Source")
        f.command("MENU type https://httpbingo.org/delay/10")
        f.wait("Updating; existing cache")
        f.choose("Cancel Operation")
        f.wait("Operation cancelled")
        assert len(json.loads(registry.read_text())["sources"]) == 1
    finally:
        f.close()
    print("PASS failed refresh/cache and fetch cancel:", driver, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--msbuild", default=MSBUILD)
    parser.add_argument("--exe", type=Path,
                        default=OUT / "bin/RetroArch-msvc2022.exe")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--driver", choices=("rgui", "ozone", "xmb", "glui"), default="rgui")
    parser.add_argument("--assets", type=Path)
    args = parser.parse_args()
    if args.build:
        build(args.msbuild)
    if args.run:
        if args.assets is None:
            parser.error("--assets is required: RGUI falls back to English without CJK fonts")
        fonts = args.assets / "rgui/font"
        if not all((fonts / ("bitmap10x10_" + lang + ".bin")).is_file()
                   for lang in ("eng", "chn", "jpn", "kor")):
            parser.error("Missing RGUI fonts under --assets/rgui/font")
        directory = OUT / (args.driver + "-" + str(time.time_ns()))
        directory.mkdir(parents=True)
        result = {"driver": args.driver, "exe": str(args.exe.resolve()),
                  "sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest()}
        try:
            exercise(args.exe.resolve(), directory, args.driver, args.assets)
            lifecycle(args.exe.resolve(), directory / "lifecycle", args.driver, args.assets)
            result["exit"] = 0
        except BaseException:
            result["exit"] = 1
            result["failure"] = traceback.format_exc()
            raise
        finally:
            (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
