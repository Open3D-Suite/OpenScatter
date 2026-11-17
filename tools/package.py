# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the release package of OpenScatter: the bundle 3ds Max loads, with the plugin in it.

    python tools/package.py [--max-sdk <maxsdk folder>] [--zip]

Runs build.ps1 -Plugin (it needs the Visual Studio 2022 Build Tools and the 3ds Max 2027 SDK, and
runs the engine's tests) and writes

- dist/OpenScatter.bundle                              PackageContents.xml, the plugin, LICENSE, NOTICE
- dist/openscatter-<version>-windows-x64-3dsmax.zip    that folder, zipped (only with --zip)

The SDK's "maxsdk" folder comes from --max-sdk, else from where build.ps1 looks for it: $MaxSdk in
build.local.ps1, then the ADSK_3DSMAX_SDK_2027 environment variable. Plain Python 3, nothing to
install.

The Open3D launcher's publishing script runs this file in a clean export of a release tag and zips
dist/OpenScatter.bundle itself.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

# abspath, not resolve(): resolve() turns a mapped network drive into its \\server\share path, and a
# build folder configured from one of the two spellings is rebuilt from scratch when given the other.
ROOT = Path(os.path.abspath(__file__)).parent.parent
DIST = ROOT / "dist"
BUNDLE = "OpenScatter.bundle"
PLUGIN = Path("Contents") / "plugins" / "2027" / "OpenScatter.dlo"


def build(max_sdk):
    """Runs build.ps1 -Plugin and returns (version, the bundle folder it put together)."""
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "build.ps1"), "-Plugin"]
    if max_sdk:
        cmd += ["-MaxSdk", max_sdk]
    # S603: running the build is what this script is for. The command is this repository's own build.ps1
    # with a fixed argument list and no shell; the SDK folder is the one given to this script.
    result = subprocess.run(  # noqa: S603
        cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace"
    )
    found = re.search(r"^Bundle \(([0-9.]+)\): (.+)$", result.stdout, re.MULTILINE)
    if result.returncode != 0 or not found:
        sys.stderr.write(result.stdout)
        raise SystemExit("error: build.ps1 -Plugin failed (exit code %d)" % result.returncode)
    return found.group(1), Path(found.group(2).strip())


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the release package of OpenScatter.")
    parser.add_argument("--max-sdk", default="", help="the 3ds Max 2027 SDK's maxsdk folder")
    parser.add_argument("--zip", action="store_true", help="also write the zip a release carries")
    args = parser.parse_args(argv)

    version, built = build(args.max_sdk)
    if not (built / PLUGIN).is_file():
        raise SystemExit("error: no %s in %s" % (PLUGIN, built))

    bundle = DIST / BUNDLE
    if bundle.exists():
        shutil.rmtree(bundle)
    DIST.mkdir(exist_ok=True)
    shutil.copytree(built, bundle)
    # The licence travels with the binary, and so do the credits.
    for name in ("LICENSE", "NOTICE"):
        shutil.copyfile(ROOT / name, bundle / name)
    plugin = bundle / PLUGIN
    print("OpenScatter %s: %s" % (version, bundle))
    print("  %s: %d bytes, sha256 %s" % (PLUGIN.name, plugin.stat().st_size, sha256(plugin)))

    if args.zip:
        zip_path = DIST / ("openscatter-%s-windows-x64-3dsmax.zip" % version)
        if zip_path.exists():
            zip_path.unlink()
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for f in sorted(p for p in bundle.rglob("*") if p.is_file()):
                z.write(f, BUNDLE + "/" + f.relative_to(bundle).as_posix())
        print("  %s: %d bytes, sha256 %s" % (zip_path.name, zip_path.stat().st_size, sha256(zip_path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
