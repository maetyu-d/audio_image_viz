#!/usr/bin/env python3
"""Fetch the pinned, official macOS YouTube helpers used by the app bundle."""
import hashlib
import json
import pathlib
import subprocess
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1] / "vendor" / "youtube"
root.mkdir(parents=True, exist_ok=True)
versions = {"yt-dlp": "2026.10.21", "quickjs": "v0.17.0"}
def fetch(url, name):
    path = root / name
    request = urllib.request.Request(url, headers={"User-Agent": "AudioImage-build"})
    with urllib.request.urlopen(request, timeout=90) as response:
        path.write_bytes(response.read())
    print(name, path.stat().st_size, flush=True)
    return path

base = "https://github.com/yt-dlp/yt-dlp/releases/download/" + versions["yt-dlp"] + "/"
checksums = fetch(base + "SHA2-256SUMS", "yt-dlp-SHA2-256SUMS").read_text()
tool = fetch(base + "yt-dlp_macos", "yt-dlp")
expected = next(line.split()[0] for line in checksums.splitlines() if line.split()[-1] == "yt-dlp_macos")
if hashlib.sha256(tool.read_bytes()).hexdigest() != expected:
    raise SystemExit("yt-dlp checksum mismatch")
tool.chmod(0o755)
base = "https://github.com/quickjs-ng/quickjs/releases/download/" + versions["quickjs"] + "/"
parts = [fetch(base + "qjs-darwin-" + arch, "qjs-" + arch) for arch in ("arm64", "x86_64")]
subprocess.run(["/usr/bin/lipo", "-create", *map(str, parts), "-output", str(root / "qjs")], check=True)
(root / "qjs").chmod(0o755)
subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(root / "qjs")], check=True)
fetch("https://raw.githubusercontent.com/quickjs-ng/quickjs/" + versions["quickjs"] + "/LICENSE", "QuickJS-LICENSE")
fetch("https://raw.githubusercontent.com/yt-dlp/yt-dlp/" + versions["yt-dlp"] + "/THIRD_PARTY_LICENSES.txt", "yt-dlp-THIRD_PARTY_LICENSES.txt")
fetch("https://raw.githubusercontent.com/yt-dlp/yt-dlp/" + versions["yt-dlp"] + "/LICENSE", "yt-dlp-LICENSE")
fetch("https://www.gnu.org/licenses/gpl-3.0.txt", "GPL-3.0.txt")
(root / "versions.json").write_text(json.dumps({"versions": versions, "sha256": {
    name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in ("yt-dlp", "qjs")},
    "sources": ["https://github.com/yt-dlp/yt-dlp/tree/" + versions["yt-dlp"],
                "https://github.com/quickjs-ng/quickjs/tree/" + versions["quickjs"]]}, indent=2) + "\n")
