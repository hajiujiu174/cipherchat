"""Package only blocks not referenced by an already-published release (or all blocks for first publish)."""
import argparse
import json
from pathlib import Path
import tarfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("feed", type=Path); p.add_argument("archive", type=Path); p.add_argument("--prior", type=Path)
a = p.parse_args()
old = json.loads(a.prior.read_bytes()) if a.prior else {"files": []}
known = {b["sha256"] for f in old["files"] + old.get('archives', []) for b in f["blocks"]}
latest = json.loads((a.feed / "latest.json").read_bytes())
needed = {b["sha256"] for f in latest["files"] + latest.get('archives', []) for b in f["blocks"]} - known
if a.archive.exists(): raise RuntimeError("Archive already exists")
with tarfile.open(a.archive, "w:gz") as archive:
    archive.add(a.feed / "latest.json", arcname="latest.json")
    info = tarfile.TarInfo("blobs"); info.type = tarfile.DIRTYPE; info.mode = 0o755; archive.addfile(info)
    for value in sorted(needed): archive.add(a.feed / "blobs" / value, arcname="blobs/" + value)
print(json.dumps({"blocks": len(needed), "archive_bytes": a.archive.stat().st_size, "version": latest["version"]}))
