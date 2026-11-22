"""Read-only, bounded throughput comparison of the existing TLS update route."""
import argparse, base64, hashlib, json, socket, ssl, struct, time, threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
output_lock = threading.Lock()
def report(value):
    with output_lock: print(json.dumps(value),flush=True)

def read(tls, count):
    value = b""
    while len(value) < count:
        part = tls.recv(count-len(value))
        if not part: raise EOFError("update connection closed")
        value += part
    return value

def get(tls, path):
    wire = json.dumps(dict(type="update_get", msg_id="throughput-probe", path=path)).encode()
    tls.sendall(struct.pack("!I",len(wire))+wire)
    size, = struct.unpack("!I",read(tls,4))
    if size > 2*1048576: raise ValueError("response too large")
    reply = json.loads(read(tls,size))
    assert reply["ok"] and reply["type"] == "update_result"
    return base64.b64decode(reply["data"],validate=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("certificate",type=Path)
    parser.add_argument("--host",default="2603:1040:f02:d::21d")
    parser.add_argument("--port",default=55220,type=int)
    parser.add_argument("--blocks",default=16,type=int,choices=range(1,33))
    args = parser.parse_args()
    context = ssl.create_default_context(cafile=str(args.certificate)); context.minimum_version = ssl.TLSVersion.TLSv1_3
    def connect(): return context.wrap_socket(socket.create_connection((args.host,args.port),timeout=12),server_hostname=args.host)
    with connect() as tls:
        before = time.monotonic(); manifest = json.loads(get(tls,"latest.json")); manifest_ms = (time.monotonic()-before)*1000
        report({"phase":"manifest","version":manifest["version"],"ms":manifest_ms})
        blocks = [block for file in manifest["files"] for block in file["blocks"] if block["size"] == 262144][:args.blocks]
        def retrieve(channel,block):
            report({"phase":"block","hash":block["sha256"]})
            value = get(channel,"blobs/"+block["sha256"])
            assert len(value) == block["size"] and hashlib.sha256(value).hexdigest() == block["sha256"]
            return len(value)
        before = time.monotonic(); serial_bytes = sum(retrieve(tls,block) for block in blocks); serial_seconds = time.monotonic()-before
    with ThreadPoolExecutor(max_workers=4) as pool:
        setup = time.monotonic(); channels = list(pool.map(lambda _:connect(),range(4))); setup_seconds=time.monotonic()-setup
        def lane(index): return sum(retrieve(channels[index],block) for block in blocks[index::4])
        try:
            before = time.monotonic(); parallel_bytes = sum(pool.map(lane,range(4))); parallel_seconds = time.monotonic()-before
        finally:
            for channel in channels: channel.close()
    report(dict(version=manifest["version"],blocks=len(blocks),manifest_ms=round(manifest_ms,1),parallel_setup_seconds=round(setup_seconds,3),
        serial_seconds=round(serial_seconds,3),serial_mib_s=round(serial_bytes/1048576/serial_seconds,3),
        parallel_seconds=round(parallel_seconds,3),parallel_mib_s=round(parallel_bytes/1048576/parallel_seconds,3),
        hashes_verified=True,scope="current machine and current network; not the remote user computer"))

if __name__ == "__main__": main()
