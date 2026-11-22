"""TLS-ticket authenticated movement relay. Never logs keys or payloads."""
import argparse
import asyncio
import json
import socket
import sys
import time
import os
import base64
import hashlib
import hmac


def canonical(data):
    return json.dumps(data, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode()


class Relay(asyncio.DatagramProtocol):
    def __init__(self, secret=None):
        self.clients = {}
        self.replays = {}
        self.secret = secret or os.environ.get('NEXTALK_GAME_UDP_SECRET', '').encode()
        if len(self.secret) < 32:
            raise ValueError('NEXTALK_GAME_UDP_SECRET is required (32 bytes minimum)')
        self.tx = time.time_ns()

    def key(self, user, expires, session):
        return hmac.new(self.secret, f'{user}\n{expires}\n{session}'.encode(), hashlib.sha256).digest()

    def send(self, packet, target):
        addr, _, session, expires, key = target
        self.tx += 1
        packet = dict(packet, v=2, session=session, expires=expires, nonce=str(self.tx))
        packet.pop('mac', None)
        packet['mac'] = hmac.new(key, canonical(packet), hashlib.sha256).hexdigest()
        raw = canonical(packet)
        if len(raw) <= 1200:
            self.transport.sendto(raw, addr)

    def connection_made(self, transport):
        self.transport = transport
        print(json.dumps({'ready': True, 'port': transport.get_extra_info('sockname')[1]}), flush=True)

    def datagram_received(self, raw, addr):
        if not 1 <= len(raw) <= 1200:
            return
        try:
            data = json.loads(raw)
            if not isinstance(data, dict) or data.get('v') != 2:
                return
            user, op = data.get('from'), data.get('op')
            if not isinstance(user, str) or not 1 <= len(user) <= 128:
                return
            expires, session = data.get('expires'), data.get('session')
            if type(expires) is not int or not time.time() < expires <= time.time() + 330 or not isinstance(session, str) or len(session) > 64:
                return
            signature = data.pop('mac', '')
            key = self.key(user, expires, session)
            if not isinstance(signature, str) or not hmac.compare_digest(signature, hmac.new(key, canonical(data), hashlib.sha256).hexdigest()):
                return
            nonce = int(data.get('nonce', '0'))
            replay = (user, session, expires)
            if nonce <= self.replays.get(replay, 0):
                return
            self.replays = {k: v for k, v in self.replays.items() if k[2] > time.time()}
            if replay not in self.replays and len(self.replays) >= 4096:
                return
            self.replays[replay] = nonce
            now = time.monotonic()
            if op == 'bind':
                self.clients = {k: v for k, v in self.clients.items() if now - v[1] < 3}
                if user not in self.clients and len(self.clients) >= 2048:
                    return
                self.clients[user] = (addr, now, session, expires, key)
                self.send({'op': 'bound'}, self.clients[user])
                return
            if op not in ('data', 'probe', 'pong') or user not in self.clients:
                return
            if self.clients[user][0] != addr or self.clients[user][2] != session:
                return
            to = data.get('to')
            if not isinstance(to, str):
                return
            target = self.clients.get(to)
            if target and now - target[1] < 3:
                data.pop('session', None)
                self.send(data, target)
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
