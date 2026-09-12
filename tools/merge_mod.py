#!/usr/bin/env python3
"""Merge per-platform .dusk bundles into one multi-platform bundle."""
import argparse
import hashlib
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

MOD_LIB_NAMES = ("mod.so", "mod.dll")

def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)

def entry_names(archive: zipfile.ZipFile) -> list[str]:
    names = [info.filename for info in archive.infolist() if not info.is_dir()]
    for name in names:
        if name.startswith("/") or ".." in name.split("/"):
            fail(f"unsafe entry name in {archive.filename}: {name}")
    return names

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("--symgen", default="symgen")
    args = parser.parse_args()

    content_hashes: dict[str, tuple[str, Path]] = {}
    platform_sources: dict[str, Path] = {}
    archives: list[zipfile.ZipFile] = []

    for path in args.inputs:
        if not path.is_file():
            fail(f"input does not exist: {path}")
        archive = zipfile.ZipFile(path)
        archives.append(archive)
        platforms = set()

        for name in entry_names(archive):
            if name.startswith("lib/"):
                parts = name.split("/")
                if len(parts) < 3 or not parts[1]:
                    fail(f"unexpected lib entry in {path}: {name}")
                platforms.add(parts[1])
                continue

            digest = hashlib.sha256(archive.read(name)).hexdigest()
            seen = content_hashes.get(name)
            if seen is not None and seen[0] != digest:
                fail(f"'{name}' differs between {seen[1]} and {path}")
            content_hashes[name] = (digest, path)

        if not platforms:
            fail(f"{path} contains no native libraries")

        for platform in platforms:
            if platform in platform_sources:
                fail(f"platform '{platform}' provided by both {platform_sources[platform]} and {path}")
            platform_sources[platform] = path

    if "mod.json" not in content_hashes:
        fail("bundles contain no mod.json")

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp)

        for name in entry_names(archives[0]):
            if not name.startswith("lib/"):
                target = stage / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archives[0].read(name))

        for archive in archives:
            for name in entry_names(archive):
                if name.startswith("lib/"):
                    target = stage / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(name))

        mod_libs = []
        for platform in sorted(platform_sources):
            libs = [stage / "lib" / platform / name for name in MOD_LIB_NAMES
                    if (stage / "lib" / platform / name).is_file()]
            if len(libs) != 1:
                fail(f"expected exactly one mod library in lib/{platform}/")
            mod_libs.append(libs[0])

        command = [
            args.symgen, "modmeta", "--check", "--update-json",
            str(stage / "mod.json"), *map(str, mod_libs)
        ]
        if subprocess.run(command).returncode != 0:
            fail("symgen modmeta verification failed")

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as out:
            for file in sorted(p for p in stage.rglob("*") if p.is_file()):
                info = zipfile.ZipInfo(
                    file.relative_to(stage).as_posix(),
                    date_time=(1980, 1, 1, 0, 0, 0),
                )
                info.compress_type = zipfile.ZIP_DEFLATED
                out.writestr(info, file.read_bytes())

    print(f"wrote {args.output} with platforms: {', '.join(sorted(platform_sources))}")

if __name__ == "__main__":
    main()
