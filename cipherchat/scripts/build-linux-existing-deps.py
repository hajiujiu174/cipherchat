"""Build against existing target compiler/runtime and isolated existing headers; no package install."""
import argparse, json, os, subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("headers", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--server-only", action="store_true")
a=p.parse_args()
root=a.source.resolve(); headers=a.headers.resolve(); out=a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
common=["common/auth/login_proof.cpp","common/base64.cpp","common/crypto/crypto.cpp","common/net/tls_context.cpp",
        "common/protocol/frame.cpp","common/protocol/messages.cpp","common/ratchet/x3dh.cpp","common/ratchet/double_ratchet.cpp"]
server=["server/storage/storage.cpp","server/auth/auth.cpp","framework/src/server_services.cpp","server/router/router.cpp","server/net/tls_server.cpp"]
client=["client/core/net_client.cpp","client/core/client.cpp","client/core/files.cpp"]
unit=["tests/test_main.cpp","tests/test_base64.cpp","tests/test_frame.cpp","tests/test_messages.cpp","tests/test_crypto.cpp","tests/test_ratchet.cpp","tests/test_server.cpp","tests/test_integration.cpp"]
sources=common+server+["server/main.cpp"] if a.server_only else common+server+client+unit+["server/main.cpp","tests/test_network_addresses.cpp"]
objects={}
for source in sources:
    obj=out/(source.replace("/","_")+".o")
    cmd=["g++","-std=c++20","-O2","-DNDEBUG","-g0","-pthread","-I"+str(headers),"-I"+str(root),"-I"+str(root/"third_party"),"-I"+str(root/"framework/include"),"-c",str(root/source),"-o",str(obj)]
    print("Compiling "+source,flush=True); subprocess.run(cmd,check=True)
    objects[source]=str(obj)
for name,parts in [("cipherchat-server",common+server+["server/main.cpp"]),("cipherchat-tests",common+server+client+unit),
                   ("cipherchat-network-tests",common+server+client+["tests/test_network_addresses.cpp"])]:
    if a.server_only and name != "cipherchat-server": continue
    subprocess.run(["g++","-pthread",*[objects[s] for s in parts],"-l:libssl.so.3","-l:libcrypto.so.3","-l:libsqlite3.so.0","-o",str(out/name)],check=True)
    subprocess.run(["ldd",str(out/name)],check=True)
print(json.dumps({"built":True,"output":str(out)}),flush=True)
