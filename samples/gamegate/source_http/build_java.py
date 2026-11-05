"""Build and run the pure-Java GameGate source request tests."""

import os
from pathlib import Path
import subprocess
import sys


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


def run(command, cwd, timeout, log_path):
    command_text = display_command(command)
    print(command_text, flush=True)
    result = subprocess.run(
        [str(item) for item in command], cwd=cwd, capture_output=True,
        text=True, errors="replace", timeout=timeout,
    )
    with log_path.open("a", encoding="utf-8") as log:
        log.write(command_text + "\n")
        log.write(result.stdout)
        log.write(result.stderr)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode:
        raise RuntimeError(f"command exited {result.returncode}")


def generate_certificate(keytool, output, alias, log_path):
    store = output / f"{alias}.p12"
    if store.exists():
        store.unlink()
    run([
        keytool, "-genkeypair", "-noprompt", "-alias", alias,
        "-keyalg", "RSA", "-keysize", "2048", "-validity", "2",
        "-dname", "CN=localhost", "-storetype", "PKCS12",
        "-keystore", store, "-storepass", "changeit",
        "-keypass", "changeit",
        "-ext", "SAN=dns:localhost,ip:127.0.0.1",
        "-ext", "KeyUsage=digitalSignature,keyEncipherment",
        "-ext", "EKU=serverAuth",
    ], output, 45, log_path)
    return store


def main():
    root = Path(__file__).resolve().parents[3]
    output = root / "artifacts" / "static-sources" / "http" / "java"
    classes = output / "classes"
    classes.mkdir(parents=True, exist_ok=True)
    log_path = output / "test.log"
    log_path.write_text("", encoding="utf-8")
    java_home = Path(r"C:\Program Files\Java\jdk-21.0.4+7")
    javac = java_home / "bin" / "javac.exe"
    java = java_home / "bin" / "java.exe"
    keytool = java_home / "bin" / "keytool.exe"
    for executable in (javac, java, keytool):
        if not executable.is_file():
            raise RuntimeError(f"JDK tool not found: {executable}")
    generate_certificate(keytool, output, "server", log_path)
    generate_certificate(keytool, output, "wrong", log_path)
    source = (
        root / "pkg" / "android" / "phoenix-common" / "src" / "com" /
        "retroarch" / "browser" / "retroactivity" /
        "GameGateSourceRequest.java"
    )
    test = (
        Path(__file__).with_name("java") / "com" / "retroarch" /
        "browser" / "retroactivity" / "GameGateSourceRequestTest.java"
    )
    run([
        javac, "--release", "8", "-Xlint:all", "-Xlint:-options",
        "-Werror",
        "-d", classes,
        source, test,
    ], output, 60, log_path)
    trust_store = output / "server.p12"
    run([
        java, "--add-modules", "jdk.httpserver",
        f"-Djavax.net.ssl.trustStore={trust_store}",
        "-Djavax.net.ssl.trustStoreType=PKCS12",
        "-Djavax.net.ssl.trustStorePassword=changeit",
        "-cp", classes,
        "com.retroarch.browser.retroactivity.GameGateSourceRequestTest",
        output,
        Path(__file__).with_name("redirect_vectors.tsv"),
    ], output, 90, log_path)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
