"""Replace an already-enabled update service, preserving and restoring its exact drop-in on failure."""
import importlib.machinery
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import os
import sqlite3

binary, destination, evidence = map(Path, sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=False)
contacts = "--contacts" in sys.argv[4:]
if contacts:
    # Online SQLite backup includes WAL transactions and leaves the live database untouched.
    backup_path = evidence / "server-before-contacts.db"
    descriptor = os.open(backup_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600); os.close(descriptor)
    with sqlite3.connect("file:/var/lib/cipherchat/server.db?mode=ro", uri=True) as source, sqlite3.connect(backup_path) as backup:
        source.backup(backup)
destination.parent.mkdir(parents=True, exist_ok=False)
shutil.copyfile(binary, destination); destination.chmod(0o755)
drop = Path("/etc/systemd/system/cipherchat.service.d/50-updates.conf")
previous = drop.read_bytes(); (evidence / "previous-drop-in.conf").write_bytes(previous)
updated = "[Service]\nEnvironment=CIPHERCHAT_UPDATE_ROOT=/opt/cipherchat/updates\nExecStart=\nExecStart=" + str(destination) + " 55220 server.db cert.pem key.pem ::\n"
def restart():
    subprocess.run(["systemctl", "daemon-reload"], check=True)
    subprocess.run(["systemctl", "restart", "cipherchat"], check=True)
try:
    temporary = drop.with_suffix(".tmp"); temporary.write_text(updated); temporary.replace(drop)
    restart()
    probe = importlib.machinery.SourceFileLoader("probe", str(Path(__file__).with_name("verify-update-service.py"))).load_module()
    for attempt in range(20):
        try:
            result = probe.probe("::1", 55220, Path("/var/lib/cipherchat/cert.pem"), "2603:1040:f02:d::21d"); break
        except ConnectionRefusedError:
            if attempt == 19: raise
            time.sleep(.1)
    (evidence / "result.json").write_text(json.dumps(result)); print(json.dumps(result))
    if contacts:
        contact_probe = importlib.machinery.SourceFileLoader("contacts", str(Path(__file__).with_name("verify-contacts-service.py"))).load_module()
        contact_result = contact_probe.probe("::1", 55220, Path("/var/lib/cipherchat/cert.pem"), "2603:1040:f02:d::21d")
        (evidence / "contacts.json").write_text(json.dumps(contact_result)); print(json.dumps(contact_result))
except BaseException:
    drop.write_bytes(previous); restart(); raise
