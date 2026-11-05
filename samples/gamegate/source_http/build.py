"""Build and run the standalone GameGate source HTTP tests."""

import base64
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def visual_studio_environment(output, arch="x64"):
    env = {key.upper(): value for key, value in os.environ.items()}
    devcmd = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
        "Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat"
    )
    if not devcmd.is_file():
        raise RuntimeError(f"VsDevCmd.bat not found: {devcmd}")
    setup = subprocess.run(
        f'cmd.exe /d /s /c ""{devcmd}" -no_logo -arch={arch} && set"',
        cwd=output, env=env, capture_output=True, text=True,
        errors="replace", timeout=45,
    )
    if setup.returncode:
        raise RuntimeError(f"VsDevCmd exit {setup.returncode}")
    for line in setup.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            env[key.upper()] = value
    compiler = shutil.which("cl.exe", path=env.get("PATH", ""))
    if not compiler:
        raise RuntimeError("cl.exe not found in VsDevCmd PATH")
    return env, str(Path(compiler).resolve())


def display_command(command):
    shown = []
    redact = False
    for item in command:
        text = str(item)
        if redact:
            shown.append("<redacted>")
            redact = False
        elif text in ("-storepass", "-keypass"):
            shown.append(text)
            redact = True
        elif text.startswith("-Djavax.net.ssl.trustStorePassword="):
            shown.append("-Djavax.net.ssl.trustStorePassword=<redacted>")
        else:
            shown.append(text)
    return subprocess.list2cmdline(shown)


def run(command, env, output, log_path, timeout):
    command_text = display_command(command)
    print(command_text, flush=True)
    result = subprocess.run(
        command, cwd=output, env=env, capture_output=True, text=True,
        errors="replace", timeout=timeout,
    )
    with log_path.open("a", encoding="utf-8") as log:
        log.write(command_text + "\n")
        log.write(result.stdout)
        log.write(result.stderr)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    return result.returncode


def run_local_tls_test(root, output, env, test_exe, log_path):
    java_home = Path(r"C:\Program Files\Java\jdk-21.0.4+7")
    javac = java_home / "bin" / "javac.exe"
    java = java_home / "bin" / "java.exe"
    keytool = java_home / "bin" / "keytool.exe"
    for executable in (javac, java, keytool):
        if not executable.is_file():
            raise RuntimeError(f"JDK tool not found: {executable}")
    certificate = output / "local-server.p12"
    if certificate.exists():
        certificate.unlink()
    if run([
        str(keytool), "-genkeypair", "-noprompt", "-alias", "local",
        "-keyalg", "RSA", "-keysize", "2048", "-validity", "2",
        "-dname", "CN=localhost", "-storetype", "PKCS12",
        "-keystore", str(certificate), "-storepass", "changeit",
        "-keypass", "changeit", "-ext", "SAN=ip:127.0.0.1",
        "-ext", "KeyUsage=digitalSignature,keyEncipherment",
        "-ext", "EKU=serverAuth",
    ], env, output, log_path, 45):
        return 1
    classes = output / "local-server-classes"
    classes.mkdir(parents=True, exist_ok=True)
    server_source = (
        Path(__file__).with_name("java") / "com" / "retroarch" /
        "browser" / "retroactivity" / "LocalHttpsServer.java"
    )
    if run([
        str(javac), "--release", "8", "-Xlint:-options",
        "-d", str(classes), str(server_source),
    ], env, output, log_path, 45):
        return 1
    port_file = output / "local-server-port.txt"
    if port_file.exists():
        port_file.unlink()
    process = subprocess.Popen([
        str(java), "--add-modules", "jdk.httpserver",
        "-cp", str(classes),
        "com.retroarch.browser.retroactivity.LocalHttpsServer",
        str(certificate), str(port_file),
    ], cwd=output, env=env, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True, errors="replace")
    try:
        deadline = time.monotonic() + 10
        port = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(
                    "local HTTPS server exited: " +
                    (process.stderr.read() if process.stderr else ""))
            if port_file.exists():
                text = port_file.read_text(encoding="ascii").strip()
                if text:
                    port = int(text)
                    break
            time.sleep(0.05)
        if port is None:
            raise RuntimeError("local HTTPS server did not publish a port")
        local_env = env.copy()
        local_env["GG_TEST_REFUSED_URL"] = "https://127.0.0.1:1/"
        local_env["GG_TEST_TLS_URL"] = f"https://127.0.0.1:{port}/"
        return run([str(test_exe)], local_env, output, log_path, 45)
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    offline = "--offline" in sys.argv[1:]
    root = Path(__file__).resolve().parents[3]
    output = root / "artifacts" / "static-sources" / "http"
    output.mkdir(parents=True, exist_ok=True)
    log_path = output / ("offline-test.log" if offline else "network-test.log")
    log_path.write_text("", encoding="utf-8")
    env, compiler = visual_studio_environment(output)
    for key in list(env):
        if key.startswith("GG_TEST_"):
            del env[key]
    env["GG_TEST_REDIRECT_VECTORS"] = str(
        Path(__file__).with_name("redirect_vectors.tsv"))
    common = [
        compiler, "/nologo", "/TC", "/W4", "/WX", "/Od", "/RTC1", "/MTd",
        "/D_CRT_SECURE_NO_WARNINGS",
        f"/I{root}", f"/I{root / 'libretro-common/include'}",
    ]
    sources = [
        root / "gamegate" / "source_http.c",
        root / "gamegate" / "source_http_win32.c",
        root / "gamegate" / "catalog.c",
        Path(__file__).with_name("source_http_test.c"),
    ]
    cleanup_exe = output / "source_http_owner_test.exe"
    if run([
        *common, "/DHAVE_THREADS", "/DHAVE_NETWORKING",
        str(root / "gamegate" / "source_http.c"),
        str(root / "gamegate" / "catalog.c"),
        str(Path(__file__).with_name("owner_cleanup_test.c")),
        f"/Fe:{cleanup_exe}",
    ], env, output, log_path, 90):
        return 1
    if run([str(cleanup_exe)], env, output, log_path, 30):
        return 1
    normal_exe = output / "source_http_test.exe"
    command = [
        *common, "/DHAVE_THREADS", "/DHAVE_NETWORKING",
        *map(str, sources), f"/Fe:{normal_exe}",
    ]
    if run(command, env, output, log_path, 120):
        return 1
    if run_local_tls_test(root, output, env, normal_exe, log_path):
        return 1
    network_env = env.copy()
    network_env["GG_TEST_REFUSED_URL"] = "https://127.0.0.1:1/"
    if offline:
        print("OFFLINE: resolver/lifecycle, loopback refusal and untrusted TLS only; "
              "public HTTPS/body/redirect network cases are skipped", flush=True)
    else:
        catalog = (b'{"schema_version":1,"source":{"id":"synthetic",'
                   b'"name":"Synthetic"},"games":[]}')
        fixture_url = "https://httpbingo.org/base64/" + base64.b64encode(
            catalog).decode("ascii")
        network_env.update({
            "GG_TEST_URL": fixture_url,
            "GG_TEST_DELAY_URL": "https://httpbingo.org/delay/5",
            "GG_TEST_MAX_URL":
                "https://speed.cloudflare.com/__down?bytes=4194304",
            "GG_TEST_LARGE_URL":
                "https://speed.cloudflare.com/__down?bytes=4194305",
            "GG_TEST_GZIP_URL": "https://httpbingo.org/gzip",
            "GG_TEST_DEFLATE_URL": "https://httpbingo.org/deflate",
            "GG_TEST_REDIRECT_OK_URL": "https://httpbingo.org/redirect/5",
            "GG_TEST_REDIRECT_URL": "https://httpbingo.org/redirect/6",
            "GG_TEST_TLS_URL": "https://self-signed.badssl.com/",
            "GG_TEST_304_URL": "https://httpbingo.org/status/304",
            "GG_TEST_305_URL": "https://httpbingo.org/status/305",
            "GG_TEST_306_URL": "https://httpbingo.org/status/306",
        })
        print("Windows deflate case is the httpbingo response representation only; "
              "not a statement about all deflate streams", flush=True)
    if run([str(normal_exe)], network_env, output, log_path, 150):
        return 1
    unsupported_exe = output / "source_http_unsupported_test.exe"
    command = [
        *common, "/DHAVE_THREADS=0", "/DHAVE_NETWORKING",
        "/DGG_TEST_UNSUPPORTED",
        str(root / "gamegate" / "source_http.c"),
        str(root / "gamegate" / "source_http_win32.c"),
        str(root / "gamegate" / "source_http_android.c"),
        str(root / "gamegate" / "catalog.c"),
        str(Path(__file__).with_name("source_http_test.c")),
        f"/Fe:{unsupported_exe}",
    ]
    if run(command, env, output, log_path, 90):
        return 1
    return run([str(unsupported_exe)], env, output, log_path, 30)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
