"""Real rthreads/VFS service tests; MSVC or CC, no installation or global changes."""
import base64
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.dont_write_bytecode = True
    root = Path(__file__).resolve().parents[3]
    output = root / "artifacts/static-sources/service"
    output.mkdir(parents=True, exist_ok=True)
    windows = os.name == "nt" and not os.environ.get("CC")
    env = os.environ.copy()
    if windows:
        spec = importlib.util.spec_from_file_location(
            "http_build", root / "samples/gamegate/source_http/build.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        env, compiler = helper.visual_studio_environment(output)
        flags = [compiler, "/nologo", "/TC", "/utf-8", "/Od", "/RTC1", "/MTd",
                 "/D_CRT_SECURE_NO_WARNINGS", f"/I{root}",
                 f"/I{root / 'libretro-common/include'}"]
        strict = ["/W4", "/WX"]
        define = "/D"
    else:
        flags = [os.environ.get("CC", "cc"), "-O0", "-g",
                 "-D_POSIX_C_SOURCE=200809L", f"-I{root}",
                 f"-I{root / 'libretro-common/include'}"]
        strict = ["-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
                  "-Wstack-usage=4096"]
        define = "-D"
    log = (output / "build-test.log").open("w", encoding="utf-8")

    def run(command, timeout=120, run_env=None):
        line = subprocess.list2cmdline(list(map(str, command)))
        print(line, flush=True)
        result = subprocess.run(command, cwd=output, env=run_env or env,
                                capture_output=True, text=True, errors="replace",
                                timeout=timeout)
        text = result.stdout + result.stderr
        print(text, end="", flush=True)
        log.write(line + "\n" + text + f"\nexit={result.returncode}\n")
        log.flush()
        result.check_returncode()

    deps = ["formats/json/rjson.c", "streams/file_stream.c", "vfs/vfs_implementation.c",
            "file/file_path.c", "file/file_path_io.c", "string/stdstring.c",
            "string/rstrtod.c", "encodings/encoding_utf.c", "compat/compat_strl.c",
            "compat/compat_posix_string.c", "compat/fopen_utf8.c", "time/rtime.c",
            "rthreads/rthreads.c", "rthreads/retro_eventcount.c", "features/features_cpu.c"]
    own = ["gamegate/source_service.c", "gamegate/source_store.c",
           "gamegate/source_store_io.c", "gamegate/catalog.c", "gamegate/catalog_parse.c",
           "gamegate/source_http.c"]

    def build(name, sources, threaded=True, extra=()):
        objects = []
        for index, source in enumerate(sources):
            obj = output / (f"{name}-{index}" + (".obj" if windows else ".o"))
            cmd = flags + [define + "HAVE_NETWORKING", define +
                           ("HAVE_THREADS=1" if threaded else "HAVE_THREADS=0")]
            cmd += [define + item for item in extra]
            if name == "source_service_test" and source == "gamegate/catalog.c":
                cmd += [define + "gg_catalog_free=gg_test_catalog_free_impl"]
            cmd += (strict if not source.startswith("libretro-common/") else
                    (["/W3"] if windows else ["-std=gnu99"]))
            cmd += [str(root / source)]
            cmd += ["/c", f"/Fo:{obj}"] if windows else ["-c", "-o", str(obj)]
            run(cmd)
            objects.append(str(obj))
        exe = output / (name + (".exe" if os.name == "nt" else ""))
        if windows:
            run([flags[0], "/nologo"] + objects +
                [f"/Fe:{exe}", "/link", "winmm.lib"])
        else:
            run(flags + objects + ["-o", str(exe), "-lm", "-pthread"] +
                (["-lwinmm"] if os.name == "nt" else []))
        return exe

    test = "samples/gamegate/source_service/source_service_test.c"
    unsupported = build("source_service_unsupported_test",
                        ["gamegate/source_service.c", test], False,
                        ["GG_SERVICE_TEST_UNSUPPORTED"])
    run([str(unsupported)])
    unity = output / "service-unity.c"
    unity.write_text("\n".join('#include "' + name + '"' for name in [
        "gamegate/source_service.h", "gamegate/source_http.c",
        "gamegate/source_http_win32.c", "gamegate/source_http_android.c",
        "gamegate/source_service.c"]) + "\n", encoding="ascii")
    unity_obj = output / ("service-unity.obj" if windows else "service-unity.o")
    run(flags + strict + [define + "HAVE_THREADS=1", define + "HAVE_NETWORKING",
        str(unity)] + (["/c", f"/Fo:{unity_obj}"] if windows else
                      ["-c", "-o", str(unity_obj)]))
    common = own + ["libretro-common/" + item for item in deps]
    mock = build("source_service_test", common +
                 ["samples/gamegate/source_service/mock_http.c", test],
                 extra=["GG_SOURCE_HTTP_TEST_BACKEND"])
    run([str(mock), tempfile.mkdtemp(prefix="mock-", dir=output)])
    real = build("source_service_network_test", common +
                 ["gamegate/source_http_win32.c", "gamegate/source_http_android.c", test],
                 extra=["GG_SERVICE_TEST_NETWORK"])
    network_env = env.copy()
    if "--network" in sys.argv:
        catalog = b'{"schema_version":1,"source":{"id":"synthetic","name":"Synthetic"},"games":[]}'
        network_env["GG_SERVICE_TEST_URL"] = "https://httpbingo.org/base64/" + base64.b64encode(catalog).decode("ascii")
    run([str(real), tempfile.mkdtemp(prefix="real-", dir=output)], run_env=network_env)
    print("PASS: service builds and requested tests; inspect explicit network SKIP if offline")
    log.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
