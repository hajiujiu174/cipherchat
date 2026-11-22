"""Run on the Linux center after uploading a complete feed to a separate directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

def install(source, destination):
    manifest_bytes = (source / "latest.json").read_bytes()
    latest = json.loads(manifest_bytes)
    old_path = destination / "latest.json"
    if old_path.exists():
        old = json.loads(old_path.read_bytes())
        if latest["sequence"] <= old["sequence"] and latest != old:
            raise ValueError("Refusing non-increasing release sequence")
    # Verify everything before any change to the live manifest.
    for file in latest["files"] + latest.get('archives', []):
        for block in file["blocks"]:
            uploaded = source / "blobs" / block["sha256"]
            data = (uploaded if uploaded.exists() else destination / "blobs" / block["sha256"]).read_bytes()
            if len(data) != block["size"] or hashlib.sha256(data).hexdigest() != block["sha256"]:
                raise ValueError("Upload contains a damaged block")
    (destination / "blobs").mkdir(parents=True, exist_ok=True)
    for item in (source / "blobs").iterdir():
        if len(item.name) != 64 or hashlib.sha256(item.read_bytes()).hexdigest() != item.name:
            raise ValueError("Invalid content block")
        target = destination / "blobs" / item.name
        if target.exists():
            if target.read_bytes() != item.read_bytes():
                raise ValueError("Existing immutable block differs")
        else:
            shutil.copyfile(item, target)
            target.chmod(0o644)
    releases = destination / "releases"; releases.mkdir(exist_ok=True)
    historical = releases / (latest["version"] + ".json")
    if historical.exists() and historical.read_bytes() != manifest_bytes:
        raise ValueError("Existing version is immutable")
    historical.write_bytes(manifest_bytes)
    temporary = destination / "latest.json.tmp"
    with temporary.open("wb") as out:
        out.write(manifest_bytes); out.flush(); os.fsync(out.fileno())
    temporary.chmod(0o644); temporary.replace(old_path)
    print(json.dumps({"published": latest["version"], "files": len(latest["files"])}))

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("destination", type=Path)
    a = p.parse_args(); install(a.source, a.destination)
