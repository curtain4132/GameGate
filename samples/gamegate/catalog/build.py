"""Build and run standalone C catalog tests; all output stays in artifacts."""

import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[3]
    output = root / "artifacts" / "static-sources" / "catalog"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if os.name == "nt":
        env = {key.upper(): value for key, value in env.items()}
        devcmd = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
            "Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat"
        )
        if not devcmd.is_file():
            raise RuntimeError(f"VsDevCmd.bat not found: {devcmd}")
        setup = subprocess.run(
            f'cmd.exe /d /s /c ""{devcmd}" -no_logo -arch=x64 && set"',
            cwd=output, env=env, capture_output=True, text=True, errors="replace", timeout=45,
        )
        if setup.returncode:
            raise RuntimeError(f"VsDevCmd exit {setup.returncode}: {setup.stdout}{setup.stderr}")
        for line in setup.stdout.splitlines():
            key, sep, value = line.partition("=")
            if sep and key:
                env[key.upper()] = value
        compiler = shutil.which("cl.exe", path=env.get("PATH", ""))
        if not compiler:
            raise RuntimeError("cl.exe not found in VsDevCmd PATH")
        flags = [str(Path(compiler).resolve()), "/nologo", "/TC", "/W4", "/WX", "/Od", "/RTC1", "/MTd",
                 f"/I{root}", f"/I{root / 'libretro-common/include'}"]
    else:
        flags = [os.environ.get("CC", "cc"), "-std=c89", "-pedantic", "-Wall",
                 "-Wextra", "-Werror", "-Wstack-usage=4096", f"-I{root}",
                 f"-I{root / 'libretro-common/include'}"]
    for test in ("model_test", "parser_test"):
        sources = [root / "gamegate" / "catalog.c", Path(__file__).with_name(test + ".c")]
        objects = []
        if test == "parser_test":
            sources += [root / "gamegate" / "catalog_parse.c"]
            dependencies = [root / "libretro-common" / "formats" / "json" / "rjson.c",
                            root / "libretro-common" / "string" / "rstrtod.c"]
            for dependency in dependencies:
                obj = output / (dependency.stem + (".obj" if os.name == "nt" else ".o"))
                command = [*flags, str(dependency)]
                # Existing rjson no-op callbacks intentionally leave parameters unused.
                if dependency.stem == "rjson":
                    command += (["/wd4100"] if os.name == "nt" else
                                ["-Wno-unused-parameter", "-D_POSIX_C_SOURCE=200112L"])
                if dependency.stem == "rstrtod" and os.name != "nt":
                    command += ["-Wno-long-long"]
                command += (["/c", f"/Fo:{obj}"] if os.name == "nt"
                            else ["-c", "-o", str(obj)])
                print(subprocess.list2cmdline(command), flush=True)
                subprocess.run(command, cwd=output, env=env, check=True, timeout=90)
                objects += [obj]
        executable = output / (test + (".exe" if os.name == "nt" else ""))
        command = [*flags, *map(str, sources)]
        command += [f"/Fe:{executable}"] if os.name == "nt" else ["-o", str(executable)]
        if objects:
            command += (["/link"] if os.name == "nt" else []) + list(map(str, objects))
            if os.name != "nt":
                command += ["-lm"]
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, cwd=output, env=env, check=True, timeout=90)
        subprocess.run([str(executable)], cwd=output, env=env, check=True, timeout=60)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
