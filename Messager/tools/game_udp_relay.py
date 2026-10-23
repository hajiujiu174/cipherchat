"""Movement-only UDP relay. No accounts, DB, signatures or game simulation.

Run independently of the TLS chat server. Clients discover peer capability via
their existing TLS game channel; unavailable UDP automatically falls back there.
"""
import argparse
import asyncio
import json
import socket
import sys
import time


class Relay(asyncio.DatagramProtocol):
    def __init__(self):
        self.clients = {}

    def connection_made(self, transport):
        self.transport = transport
        print(json.dumps({'ready': True, 'port': transport.get_extra_info('sockname')[1]}), flush=True)

    def datagram_received(self, raw, addr):
        if not 1 <= len(raw) <= 1200:
            return
        try:
            data = json.loads(raw)
            if not isinstance(data, dict) or data.get('v') != 1:
                return
            user, op = data.get('from'), data.get('op')
            if not isinstance(user, str) or not 1 <= len(user) <= 128:
                return
            now = time.monotonic()
            if op == 'bind':
                self.clients = {k: v for k, v in self.clients.items() if now - v[1] < 3}
                if user not in self.clients and len(self.clients) >= 2048:
                    return
                self.clients[user] = (addr, now)
                self.transport.sendto(b'{"v":1,"op":"bound"}', addr)
                return
            if op not in ('data', 'probe', 'pong') or user not in self.clients:
                return
            # This is endpoint routing, deliberately not user authentication.
            to = data.get('to')
            if not isinstance(to, str):
                return
            target = self.clients.get(to)
            if target and now - target[1] < 3:
                self.transport.sendto(raw, target[0])
        except (ValueError, TypeError, OSError):
            return


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=55221)
    args = parser.parse_args()
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET6 if ':' in args.bind else socket.AF_INET, socket.SOCK_DGRAM)
    # Windows can turn an ICMP response from an exited client into WSAECONNRESET
    # on the shared UDP receive socket. Keep receiving for the other clients.
    if sys.platform == 'win32' and hasattr(socket, 'SIO_UDP_CONNRESET'):
        sock.ioctl(socket.SIO_UDP_CONNRESET, False)
    sock.setblocking(False)
    sock.bind((args.bind, args.port))
    transport, _ = await loop.create_datagram_endpoint(Relay, sock=sock)
    try:
        await asyncio.Future()
    finally:
        transport.close()


if __name__ == '__main__':
    asyncio.run(main())
