"""Windows TLS upload diagnostic; no file/database writes on the relay, no global network changes."""
import argparse, base64, concurrent.futures, ctypes, json, os, socket, ssl, struct, threading, time
from pathlib import Path

class TcpInfo(ctypes.Structure):
    _fields_ = [("State",ctypes.c_ulong),("Mss",ctypes.c_ulong),("ConnectionTimeMs",ctypes.c_ulonglong),
        ("TimestampsEnabled",ctypes.c_ubyte)] + [(n,ctypes.c_ulong) for n in
        ("RttUs","MinRttUs","BytesInFlight","Cwnd","SndWnd","RcvWnd","RcvBuf")] + [
        ("BytesOut",ctypes.c_ulonglong),("BytesIn",ctypes.c_ulonglong)] + [(n,ctypes.c_ulong) for n in
        ("BytesReordered","BytesRetrans","FastRetrans","DupAcksIn","TimeoutEpisodes")] + [("SynRetrans",ctypes.c_ubyte)]

ws2=ctypes.WinDLL("ws2_32",use_last_error=True)
ws2.WSAIoctl.argtypes=[ctypes.c_size_t,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
def tcp_info(channel):
    version=ctypes.c_ulong(0); info=TcpInfo(); returned=ctypes.c_ulong()
    result=ws2.WSAIoctl(channel.fileno(),0xD8000027,ctypes.byref(version),4,ctypes.byref(info),ctypes.sizeof(info),ctypes.byref(returned),None,None)
    if result:return {"error":ws2.WSAGetLastError()}
    return {n:getattr(info,n) for n,_ in info._fields_}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--host",default="2603:1040:f02:d::21d")
    p.add_argument("--port",type=int,default=55220)
    p.add_argument("--cert",default="artifacts/server-migration-20260915/server-cert.pem")
    p.add_argument("--bytes",type=int,default=1048576)
    p.add_argument("--connections",type=int,default=1)
    p.add_argument("--nodelay",action="store_true")
    p.add_argument("--write-size",type=int,default=0)
    p.add_argument("--send-buffer",type=int,default=0)
    p.add_argument("--mtu",type=int,default=0)
    p.add_argument("--interface-index",type=int,default=0)
    p.add_argument("--source-ip",default="")
    p.add_argument("--output",required=True,type=Path)
    args=p.parse_args()
    assert not args.output.exists()
    padding=base64.b64encode(os.urandom((args.bytes+2)//3*3))[:args.bytes].decode("ascii")
    payload=json.dumps({"type":"hello","version":2,"padding":padding},separators=(",",":")).encode()
    assert len(payload)<8*1024*1024
    frame=struct.pack("!I",len(payload))+payload
    context=ssl.create_default_context(cafile=args.cert)
    context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_3
    barrier=threading.Barrier(args.connections,timeout=15)
    def worker(index):
        result={"connection":index,"wire_bytes":len(frame)}
        channel=None; start=None
        try:
            raw=socket.socket(socket.AF_INET6,socket.SOCK_STREAM)
            raw.settimeout(10)
            if args.interface_index:raw.setsockopt(socket.IPPROTO_IPV6,31,args.interface_index) # IPV6_UNICAST_IF, this socket only.
            if args.source_ip:raw.bind((args.source_ip,0))
            if args.mtu:raw.setsockopt(socket.IPPROTO_IPV6,76,args.mtu) # IPV6_USER_MTU, this socket only.
            raw.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,int(args.nodelay))
            if args.send_buffer:raw.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,args.send_buffer)
            raw.connect((args.host,args.port))
            result["send_buffer"]=raw.getsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF)
            channel=context.wrap_socket(raw,server_hostname=args.host)
            result["local_port"]=channel.getsockname()[1]
            result["before"]=tcp_info(channel)
            barrier.wait()
            channel.settimeout(30)
            start=time.monotonic();result["started_monotonic"]=start
            if args.write_size:
                for i in range(0,len(frame),args.write_size):
                    channel.settimeout(max(0.01,30-(time.monotonic()-start)))
                    channel.sendall(frame[i:i+args.write_size])
            else:channel.sendall(frame)
            result["send_seconds"]=round(time.monotonic()-start,4)
            def receive(n):
                data=b""
                while len(data)<n:
                    channel.settimeout(max(0.01,32-(time.monotonic()-start)))
                    part=channel.recv(n-len(data))
                    if not part:raise RuntimeError("early eof")
                    data+=part
                return data
            length,=struct.unpack("!I",receive(4))
            assert length<8192
            reply=json.loads(receive(length))
            assert reply.get("type")=="hello_result" and reply.get("ok") is True
            result.update({"passed":True,"seconds":round(time.monotonic()-start,4),"server_confirmed_complete_frame":True})
            result["mbps"]=round(len(frame)*8/result["seconds"]/1e6,3)
        except Exception as error:
            result.update({"passed":False,"error":str(error),"seconds":round(time.monotonic()-start,4) if start else None})
            try:barrier.abort()
            except Exception:pass
        finally:
            if channel:
                result["after"]=tcp_info(channel)
                if "before" in result and "error" not in result["before"] and "error" not in result["after"]:
                    result["delta"]={k:result["after"][k]-result["before"][k] for k in ("BytesOut","BytesIn","BytesRetrans","FastRetrans","DupAcksIn","TimeoutEpisodes")}
                channel.close()
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.connections) as pool:
        results=list(pool.map(worker,range(args.connections)))
    report={"method":"TLS1.3 hello with random padding, existing port, no authentication/database/file path", "bytes_per_connection":args.bytes,
        "host":args.host,"interface_index":args.interface_index,"source_ip":args.source_ip,
        "connections":args.connections,"tcp_nodelay":args.nodelay,"write_size":args.write_size,"socket_mtu":args.mtu,"results":results}
    if all(r["passed"] for r in results):
        start=min(r["started_monotonic"] for r in results)
        end=max(r["started_monotonic"]+r["seconds"] for r in results)
        report["aggregate_mbps"]=round(sum(r["wire_bytes"] for r in results)*8/(end-start)/1e6,3)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2),flush=True)
if __name__=="__main__":main()
