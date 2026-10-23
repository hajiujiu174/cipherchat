"""One-time center upgrade; run only after isolated verification of this build and feed."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
from importlib.machinery import SourceFileLoader

binary, evidence = Path(sys.argv[1]), Path(sys.argv[2])
evidence.mkdir(parents=True, exist_ok=False)
service = subprocess.check_output(["systemctl", "cat", "cipherchat.service"])
(evidence / "previous-service.txt").write_bytes(service)
drop = Path("/etc/systemd/system/cipherchat.service.d/50-updates.conf")
if drop.exists():
    raise RuntimeError("Update service drop-in already exists; review it before another server upgrade")
destination = Path("/opt/cipherchat/releases/20260919-updates/cipherchat-server")
destination.parent.mkdir(parents=True, exist_ok=False)
shutil.copyfile(binary, destination); destination.chmod(0o755)
drop.parent.mkdir(parents=True, exist_ok=True)
drop.write_text("[Service]\nEnvironment=CIPHERCHAT_UPDATE_ROOT=/opt/cipherchat/updates\nExecStart=\nExecStart=" + str(destination) + " 55220 server.db cert.pem key.pem ::\n")
def restart():
    subprocess.run(["systemctl", "daemon-reload"], check=True)
    subprocess.run(["systemctl", "restart", "cipherchat"], check=True)
try:
    restart()
    verify = SourceFileLoader("probe", str(Path(__file__).with_name("verify-update-service.py"))).load_module()
    import time
    last = None
    for _ in range(20):
        try:
            result = verify.probe("::1", 55220, Path("/var/lib/cipherchat/cert.pem"), "2603:1040:f02:d::21d")
            break
        except ConnectionRefusedError as error:
            last = error; time.sleep(.1)
    else:
        raise last
    subprocess.run(["systemctl", "is-active", "--quiet", "cipherchat"], check=True)
    (evidence / "deployed.json").write_text(json.dumps(result))
    print(json.dumps(result))
except BaseException:
    drop.rename(evidence / "rejected-drop-in.conf")
    restart()
    raise
