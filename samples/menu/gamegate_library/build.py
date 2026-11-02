"""Build the real-menu harness (MSVC, or a built non-Qt Make frontend).

Run from any directory with --msbuild PATH on Windows, or --make make
after ./configure --disable-qt && make on Linux/MinGW. --run executes
headless fixtures; all generated files stay in artifacts/native-library.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / "artifacts/native-library/harness"


def run(command):
    print("+", subprocess.list2cmdline([str(x) for x in command]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def msvc(msbuild):
    wrapper = OUT / "griffin_test.c"
    wrapper.write_text(
        '#define main gamegate_unused_main\n#include "griffin/griffin.c"\n'
        '#undef main\n#include "samples/menu/gamegate_library/'
        'gamegate_library_test.c"\n', encoding="ascii")
    targets = OUT / "harness.targets"
    targets.write_text(
        '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
        '<ItemGroup><ClCompile Remove="..\\..\\..\\griffin\\griffin.c" />'
        '<ClCompile Include="' + escape(str(wrapper)) + '">'
        '<CompileAs>CompileAsC</CompileAs></ClCompile></ItemGroup></Project>',
        encoding="utf-8")
    run([msbuild, str(ROOT / "pkg/msvc/msvc-2022/RetroArch-msvc2022.vcxproj"),
         "/p:Configuration=Release", "/p:Platform=x64",
         "/p:TargetName=gamegate_library_test",
         "/p:OutDir=" + str(OUT / "bin") + os.sep,
         "/p:IntDir=" + str(OUT / "obj") + os.sep,
         "/p:ForceImportBeforeCppTargets=" + str(targets),
         "-nodeReuse:false", "/v:minimal"])
    return OUT / "bin/gamegate_library_test.exe"


def make_build(make, objdir):
    text = subprocess.check_output(
        [make, "-n", "OBJDIR=" + objdir, "-W", "retroarch.c"],
        cwd=ROOT, text=True)
    commands = [shlex.split(line) for line in text.splitlines()
                if line and not line.startswith("make[")]
    compiles = [c for c in commands if "-c" in c and "retroarch.c" in c]
    links = [c for c in commands if "-c" not in c and "-o" in c
             and c[c.index("-o") + 1] in {"retroarch", "retroarch.exe"}]
    if len(compiles) != 1 or len(links) != 1:
        raise RuntimeError("Build a non-Qt frontend first; expected one compile/link")
    cc = compiles[0]
    original = cc[cc.index("-o") + 1]
    cc[cc.index("-o") + 1] = str(OUT / "retroarch_nomain.o")
    run(cc + ["-Umain", "-Dmain=gamegate_unused_main"])
    cc[cc.index("-o") + 1] = str(OUT / "harness.o")
    cc[cc.index("retroarch.c")] = str(HERE / "gamegate_library_test.c")
    run(cc)
    link = links[0]
    exe = OUT / ("gamegate_library_test.exe" if os.name == "nt"
                 else "gamegate_library_test")
    link[link.index("-o") + 1] = str(exe)
    i = link.index(original)
    link[i:i + 1] = [str(OUT / "retroarch_nomain.o"), str(OUT / "harness.o")]
    if os.name == "nt":
        link.append("-mconsole")
    run(link)
    return exe


def fixtures(exe):
    fixture = OUT / "fixtures"
    fixture.mkdir(exist_ok=True)
    playlists = fixture / "playlists"
    playlists.mkdir(exist_ok=True)
    for name in ("Alpha.lpl", "Beta.lpl", "content_history.lpl",
                 "content_favorites.lpl", "not_a_playlist.txt"):
        (playlists / name).write_text(json.dumps({"version": "1.5", "items": [
            {"path": "missing-game.bin", "label": name, "core_path": "DETECT",
             "core_name": "DETECT", "crc32": "00000000|crc", "db_name": name}
        ]}), encoding="utf-8")
    config = fixture / "harness.cfg"
    values = {
        "video_driver": "null", "audio_driver": "null", "input_driver": "null",
        "input_joypad_driver": "null", "menu_driver": "rgui",
        "video_threaded": "false", "threaded_data_runloop_enable": "false",
        "config_save_on_exit": "false", "menu_show_online_updater": "false",
        "content_show_add_entry": "0", "content_show_playlists": "true",
        "content_show_history": "true", "content_show_favorites": "true",
        "history_list_enable": "true",
        "playlist_directory": playlists.as_posix(),
        "content_history_path": (playlists / "content_history.lpl").as_posix(),
        "content_favorites_path": (playlists / "content_favorites.lpl").as_posix(),
    }
    for key in ("savefile_directory", "savestate_directory", "system_directory",
                "screenshot_directory", "log_dir", "libretro_directory",
                "libretro_info_path", "assets_directory", "cache_directory"):
        values[key] = fixture.as_posix()
    config.write_text("".join(key + ' = "' + value + '"\n'
                              for key, value in values.items()), encoding="utf-8")
    run([str(exe), str(config), str(playlists)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msbuild")
    parser.add_argument("--make", default="make")
    parser.add_argument("--objdir", default="obj-unix/release")
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    exe = msvc(args.msbuild) if args.msbuild else make_build(args.make, args.objdir)
    if args.run:
        fixtures(exe)


if __name__ == "__main__":
    main()
