"""Build/run real-VFS C tests; output stays in artifacts/static-sources/store.

Windows uses installed MSVC BuildTools. Linux uses CC (default cc); on Windows
CC may select an installed MinGW-w64 GCC. No packages are installed. Each run
uses a new fixture directory; prior fixtures remain available for inspection.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    sys.stdout.reconfigure(errors="backslashreplace")
    root = Path(__file__).resolve().parents[3]
    output = root / "artifacts/static-sources/store"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    windows = os.name == "nt" and not os.environ.get("CC")
    with (output / "build-test.log").open("w", encoding="utf-8") as log:
        def run(command, timeout=90):
            line = subprocess.list2cmdline(list(map(str, command)))
            print(line, flush=True)
            log.write(line + "\n")
            log.flush()
            result = subprocess.run(command, cwd=output, env=env,
                                    capture_output=True, text=True,
                                    encoding="mbcs" if windows else "utf-8",
                                    errors="replace", timeout=timeout)
            text = result.stdout + result.stderr
            print(text, end="", flush=True)
            log.write(text + f"\nexit={result.returncode}\n")
            log.flush()
            result.check_returncode()
            return result

        if windows:
            env = {key.upper(): value for key, value in env.items()}
            devcmd = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
                "Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat"
            )
            setup = subprocess.run(
                f'cmd.exe /d /s /c ""{devcmd}" -no_logo -arch=x64 && set"',
                cwd=output, env=env, capture_output=True, text=True,
                errors="replace", timeout=45)
            setup.check_returncode()
            for line in setup.stdout.splitlines():
                key, sep, value = line.partition("=")
                if sep and key:
                    env[key.upper()] = value
            compiler = shutil.which("cl.exe", path=env.get("PATH", ""))
            if not compiler:
                raise RuntimeError("cl.exe not found")
            flags = [str(Path(compiler).resolve()), "/nologo", "/TC", "/utf-8", "/Od", "/RTC1",
                     "/MTd", "/D_CRT_SECURE_NO_WARNINGS", f"/I{root}",
                     f"/I{root / 'libretro-common/include'}"]
        else:
            flags = [os.environ.get("CC", "cc"), "-O0", "-g", "-D_POSIX_C_SOURCE=200809L",
                     f"-I{root}", f"-I{root / 'libretro-common/include'}"]
        own = ["gamegate/source_store.c", "gamegate/source_store_io.c",
               "gamegate/catalog.c", "gamegate/catalog_parse.c",
               "samples/gamegate/source_store/source_store_test.c"]
        deps = ["formats/json/rjson.c", "streams/file_stream.c", "vfs/vfs_implementation.c",
                "file/file_path.c", "file/file_path_io.c", "string/stdstring.c",
                "string/rstrtod.c", "encodings/encoding_utf.c", "compat/compat_strl.c",
                "compat/compat_posix_string.c", "compat/fopen_utf8.c", "time/rtime.c"]
        objects = []
        for source in own + ["libretro-common/" + name for name in deps]:
            obj = output / (Path(source).stem + (".obj" if windows else ".o"))
            strict = (["/W4", "/WX"] if windows else
                      ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
                       "-Wstack-usage=4096"])
            command = flags + (strict if source in own else
                               (["/W3"] if windows else ["-std=gnu99"]))
            command += [str(root / source)]
            command += (["/c", f"/Fo:{obj}"] if windows else ["-c", "-o", str(obj)])
            run(command)
            objects.append(str(obj))
        exe = output / ("source_store_test.exe" if os.name == "nt" else "source_store_test")
        if windows:
            run([flags[0], "/nologo"] + objects + [f"/Fe:{exe}", "/link", "winmm.lib"])
        else:
            run(flags + objects + ["-o", str(exe), "-lm"] +
                (["-lwinmm"] if os.name == "nt" else []))
        fixture = tempfile.mkdtemp(prefix="run-", dir=output)
        run([str(exe), fixture], timeout=60)
        print("PASS: source store real-VFS and fault-injection tests", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
