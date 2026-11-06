#!/usr/bin/env python3
"""Check MaterialUI runtime assets in a glui directory or an Android APK."""
import argparse
from pathlib import Path
import re
import sys
import tempfile
import zipfile

APK_PREFIX = "assets/assets/glui/"
NAVIGATION = {
    "main_tab_passive.png", "playlists_tab_passive.png",
    "settings_tab_passive.png", "back_tab.png", "resume_tab.png",
}
DEFAULT_SOURCE = Path(__file__).resolve().parents[1] / "menu/drivers/materialui.c"


def required_assets(source):
    text = source.read_text(encoding="utf-8")
    match = re.search(
        r"static const char \*materialui_texture_path\(unsigned id\)"
        r"\s*\{(.*?)^\}", text, re.MULTILINE | re.DOTALL)
    if not match:
        raise ValueError("cannot locate materialui_texture_path in source")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.DOTALL)
    returns = re.findall(r"\breturn\s+([^;]+);", body)
    names = set()
    for value in returns:
        value = value.strip()
        if value == "NULL":
            continue
        name = re.fullmatch(r'"([A-Za-z0-9_-]+\.png)"', value)
        if not name:
            raise ValueError("unsupported texture return: " + value)
        names.add(name.group(1))
    if not NAVIGATION <= names:
        raise ValueError("source mapping lacks required navigation icons")
    return names | {"font.ttf"}


def check(path, required):
    errors = []
    if path.is_dir():
        for name in sorted(required):
            item = path / name
            if not item.is_file():
                errors.append("missing " + name)
            elif not item.stat().st_size:
                errors.append("empty " + name)
    else:
        with zipfile.ZipFile(path) as apk:
            entries = apk.namelist()
            for name in sorted(required):
                member = APK_PREFIX + name
                count = entries.count(member)
                if not count:
                    errors.append("missing " + member)
                elif count != 1:
                    errors.append("duplicate " + member)
                elif not apk.read(member):
                    errors.append("empty " + member)
    if errors:
        raise ValueError("; ".join(errors))


def selftest():
    names = NAVIGATION | {"pointer.png"}
    with tempfile.TemporaryDirectory(prefix="gamegate-assets-test-") as temp:
        root = Path(temp)
        source = root / "materialui.c"
        mapping = "\n".join('   return "' + name + '";' for name in sorted(names))
        source.write_text("static const char *materialui_texture_path(unsigned id)\n"
                          "{\n" + mapping + "\n   return NULL;\n}\n", encoding="utf-8")
        required = required_assets(source)
        assert required == names | {"font.ttf"}
        directory = root / "glui"
        directory.mkdir()
        archive = root / "fixture.apk"
        cases = 0

        def expect_failure(path, expected):
            nonlocal cases
            try:
                check(path, required)
            except ValueError as error:
                assert expected in str(error), str(error)
                cases += 1
                return
            raise AssertionError("invalid assets passed: " + str(path))

        expect_failure(directory, "missing ")
        with zipfile.ZipFile(archive, "w") as apk:
            apk.writestr("assets/assets/ozone/placeholder", b"fixture")
        expect_failure(archive, "missing " + APK_PREFIX)
        for name in required:
            (directory / name).write_bytes(b"fixture")
        check(directory, required)
        for victim in sorted(NAVIGATION | {"font.ttf"}):
            (directory / victim).unlink()
            expect_failure(directory, "missing " + victim)
            (directory / victim).write_bytes(b"")
            expect_failure(directory, "empty " + victim)
            (directory / victim).write_bytes(b"fixture")
            for missing in (True, False):
                with zipfile.ZipFile(archive, "w") as apk:
                    for name in required:
                        if name == victim and missing:
                            continue
                        apk.writestr(APK_PREFIX + name,
                                     b"" if name == victim else b"fixture")
                expect_failure(archive, ("missing " if missing else "empty ")
                               + APK_PREFIX + victim)
        with zipfile.ZipFile(archive, "w") as apk:
            for name in required:
                apk.writestr(APK_PREFIX + name, b"fixture")
        check(archive, required)
        with zipfile.ZipFile(archive, "w") as apk:
            for name in required:
                apk.writestr("assets/glui/" + name, b"fixture")
        expect_failure(archive, "missing " + APK_PREFIX)
        source.write_text("/* no mapping */", encoding="utf-8")
        try:
            required_assets(source)
        except ValueError:
            cases += 1
        else:
            raise AssertionError("invalid source mapping passed")
    print("MaterialUI assets selftest: PASS (directory/APK success; "
          + str(cases) + " rejection cases)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="materialui.c used to derive runtime PNG names")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("paths", nargs="*", type=Path,
                        help="glui directories (containing font.ttf) or APK files")
    args = parser.parse_args()
    if not args.selftest and not args.paths:
        parser.error("provide a glui directory or APK, or --selftest")
    if args.selftest:
        selftest()
    if args.paths:
        try:
            required = required_assets(args.source)
        except (OSError, ValueError) as error:
            print("FAIL source: " + str(error), file=sys.stderr)
            return 1
        failed = False
        for path in args.paths:
            try:
                check(path, required)
            except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
                print("FAIL " + str(path) + ": " + str(error), file=sys.stderr)
                failed = True
            else:
                print("PASS " + str(path) + ": " + str(len(required) - 1)
                      + " mapped PNGs + font.ttf (includes all 5 navigation icons)")
        return int(failed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
