"""Create immutable content blocks and a TLS update manifest from a clean release package."""
import argparse
import hashlib
import json
import os
import re
import zipfile
from pathlib import Path

BLOCK = 262144
EXCLUDED = {"release.json", "manifest.json", "cipherchat-launcher.exe", "current.txt", "previous.txt", "rollback-update.cmd"}

def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)

def build(package, feed, version, sequence, notes, compressed=False, prior=None):
    if not re.fullmatch(r"[0-9][0-9.-]{0,47}", version) or ".." in version or version.endswith(".") or sequence < 1:
        raise ValueError("Invalid version or sequence")
    files = []
    for path in sorted(package.rglob("*")):
        if path.is_symlink():
            raise ValueError("Links are not allowed in releases")
        if not path.is_file():
            continue
        name = path.relative_to(package).as_posix()
        if name in EXCLUDED or name.startswith(".updates/"):
            continue
        if not re.fullmatch(r"[A-Za-z0-9._/+@-]+", name):
            raise ValueError("Payload paths must be portable ASCII: " + name)
        blocks = []
        with path.open("rb") as stream:
            while data := stream.read(BLOCK):
                digest = hashlib.sha256(data).hexdigest()
                target = feed / "blobs" / digest
                if target.exists():
                    if target.read_bytes() != data:
                        raise ValueError("Corrupt published block " + digest)
                else:
                    atomic(target, data)
                blocks.append({"size": len(data), "sha256": digest})
        files.append({"path": name, "size": path.stat().st_size, "blocks": blocks})
    required = {"cipherchat.exe", "cipherchat-updater.exe", "update-config.json", "server-cert.pem"}
    if not required.issubset({f["path"] for f in files}):
        raise ValueError("Missing required update payload")
    manifest = dict(format=1, platform="windows-x64", block_size=BLOCK, version=version, sequence=sequence, notes=notes, files=files)
    if compressed:
        selections = [(0, files)]
        if prior:
            previous = json.loads(Path(prior).read_bytes())
            old_files = {f['path']: f for f in previous['files']}
            changed = [f for f in files if f != old_files.get(f['path'])]
            if changed: selections.append((previous['sequence'], changed))
        manifest['archives'] = []
        for base_sequence, selected in selections:
            archive = feed / 'archives' / (version + '-' + str(base_sequence) + '.zip')
            archive.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as stream:
                for item in selected:
                    info = zipfile.ZipInfo(item['path'], (1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    stream.writestr(info, (package / item['path']).read_bytes(), compresslevel=6)
            blocks = []
            with archive.open('rb') as stream:
                while data := stream.read(BLOCK):
                    digest = hashlib.sha256(data).hexdigest()
                    target = feed / 'blobs' / digest
                    if target.exists():
                        if target.read_bytes() != data: raise ValueError('Corrupt archive block')
                    else: atomic(target, data)
                    blocks.append({'size': len(data), 'sha256': digest})
            manifest['archives'].append(dict(base_sequence=base_sequence, size=archive.stat().st_size,
                blocks=blocks, files=[f['path'] for f in selected]))
    data = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8")
    latest = feed / "latest.json"
    if latest.exists():
        old = json.loads(latest.read_bytes())
        if sequence <= old["sequence"] and old != manifest:
            raise ValueError("Release sequence must increase; published versions are immutable")
    historic = feed / "releases" / (version + ".json")
    if historic.exists() and historic.read_bytes() != data:
        raise ValueError("Published version already exists with different contents")
    atomic(package / "release.json", data)
    atomic(historic, data)
    atomic(latest, data)
    print(json.dumps({"version": version, "files": len(files), "bytes": sum(f["size"] for f in files), "feed": str(feed)}))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("feed", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--notes", default="")
    parser.add_argument('--compressed', action='store_true')
    parser.add_argument('--prior', type=Path)
    args = parser.parse_args()
    build(args.package.resolve(), args.feed.resolve(), args.version, args.sequence, args.notes, args.compressed, args.prior)
