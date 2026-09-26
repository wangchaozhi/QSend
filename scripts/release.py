"""Version validation, license staging, archives and release artifact verification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("windows-x64", "macos-universal", "linux-x64")


def version():
    match = re.search(r"project\(QSend\s+VERSION\s+(\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text())
    if not match:
        raise ValueError("Cannot read QSend version from CMakeLists.txt")
    return match.group(1)


def commit():
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def metadata():
    value = version()
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/") and ref != f"refs/tags/v{value}":
        raise ValueError(f"Tag must be v{value}, matching CMakeLists.txt; received {ref}")
    values = {"version": value, "commit": commit()}
    if output := os.environ.get("GITHUB_OUTPUT"):
        with open(output, "a", encoding="utf-8") as stream:
            stream.writelines(f"{key}={val}\n" for key, val in values.items())
    print(json.dumps(values))


def prepare(args):
    stage = args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    for name in ("LICENSE", "README.md", "CHANGELOG.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / name, stage / name)
    shutil.copytree(ROOT / "packaging/licenses", stage / "licenses", dirs_exist_ok=True)
    if (args.qt_root / "sbom").is_dir():
        shutil.copytree(args.qt_root / "sbom", stage / "licenses/qt-sbom", dirs_exist_ok=True)
    demo = stage / "demo"
    demo.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "scripts/run-demo-server.py", demo)
    shutil.copy2(ROOT / "examples/local-demo.postman_collection.json", demo)
    # Qt's Linux deployer intentionally leaves system libraries to the distro.
    # Preserve the applicable copyright inventories alongside the release.
    if args.platform == "linux-x64":
        licenses = stage / "licenses/system"
        licenses.mkdir(exist_ok=True)
        for package in ("libc6", "libstdc++6", "libgcc-s1", "libssl3", "libxcb1", "libx11-6", "libfontconfig1", "libfreetype6"):
            source = Path("/usr/share/doc") / package / "copyright"
            if source.exists():
                shutil.copy2(source, licenses / f"{package}.txt")
    info = {"version": version(), "commit": commit(), "platform": args.platform,
            "qt": "6.8.3", "buildSystem": platform.platform()}
    (stage / "BUILD-INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")


def archive(args):
    stage = args.stage.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"QSend-{version()}-{args.platform}"
    if args.platform == "linux-x64":
        target = args.output / f"{name}.tar.gz"
        with tarfile.open(target, "w:gz") as bundle:
            bundle.add(stage, arcname=name)
    elif args.platform == "macos-universal":
        # ditto preserves framework symlinks, executable permissions and signatures.
        target = args.output / f"{name}.zip"
        subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(stage), str(target)], check=True)
    else:
        target = args.output / f"{name}.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    bundle.write(path, Path(name) / path.relative_to(stage))
        with zipfile.ZipFile(target) as bundle:
            assert bundle.testzip() is None
    info = json.loads((stage / "BUILD-INFO.json").read_text(encoding="utf-8"))
    info.update(file=target.name, sha256=digest(target), size=target.stat().st_size)
    (args.output / f"{name}.manifest.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(info))


def collect(args):
    manifests = sorted(args.output.glob("*.manifest.json"))
    assert len(manifests) == 3, "Exactly three platform manifests are required"
    records = [json.loads(path.read_text()) for path in manifests]
    assert {item["platform"] for item in records} == set(PLATFORMS)
    lines = []
    for item in records:
        assert item["version"] == version() and item["commit"] == commit()
        suffix = ".tar.gz" if item["platform"] == "linux-x64" else ".zip"
        assert item["file"] == f"QSend-{version()}-{item['platform']}{suffix}"
        path = args.output / item["file"]
        assert path.stat().st_size == item["size"] and digest(path) == item["sha256"]
        lines.append(f"{item['sha256']}  {item['file']}\n")
    (args.output / "SHA256SUMS").write_text("".join(sorted(lines)), encoding="utf-8")
    (args.output / "build-manifest.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print("Verified all three archives and their source commit")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("metadata")
    for action in ("prepare", "archive"):
        command = sub.add_parser(action)
        command.add_argument("--platform", choices=PLATFORMS, required=True)
        command.add_argument("--stage", type=Path, required=True)
        if action == "prepare":
            command.add_argument("--qt-root", type=Path, required=True)
        else:
            command.add_argument("--output", type=Path, required=True)
    sub.add_parser("collect").add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.action == "metadata":
        metadata()
    else:
        {"prepare": prepare, "archive": archive, "collect": collect}[args.action](args)
