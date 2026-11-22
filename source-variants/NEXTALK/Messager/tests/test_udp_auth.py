import importlib.util
from pathlib import Path
import json, hmac, hashlib, time, unittest
spec=importlib.util.spec_from_file_location('relay',Path(__file__).parents[1]/'tools/game_udp_relay.py')
relay=importlib.util.module_from_spec(spec);spec.loader.exec_module(relay)
class Wire:
    def __init__(self):self.sent=[]
    def sendto(self,data,address):self.sent.append((json.loads(data),address))
class Auth(unittest.TestCase):
    def test_forgery_replay_and_expiry(self):
        service=relay.Relay(b'synthetic-test-key-must-be-long-32-bytes');service.transport=Wire()
        expires=int(time.time())+300
        def packet(user,nonce,op='bind',**extra):
            data=dict(v=2,op=op,**{'from':user},session='fixture',expires=expires,nonce=str(nonce),**extra)
            data['mac']=hmac.new(service.key(user,expires,'fixture'),relay.canonical(data),hashlib.sha256).hexdigest();return relay.canonical(data)
        valid=packet('alice',1);service.datagram_received(valid,('a',1));self.assertEqual(len(service.transport.sent),1)
        service.datagram_received(valid,('attacker',9));self.assertEqual(service.clients['alice'][0],('a',1));self.assertEqual(len(service.transport.sent),1)
        forged=json.loads(packet('alice',2));forged['from']='bob';service.datagram_received(relay.canonical(forged),('attacker',9));self.assertNotIn('bob',service.clients)
        service.datagram_received(packet('bob',1),('b',2));service.datagram_received(packet('alice',2,'probe',to='bob'),('a',1));self.assertEqual(service.transport.sent[-1][1],('b',2));self.assertEqual(service.transport.sent[-1][0]['from'],'alice')
        expired=json.loads(packet('alice',3));expired['expires']=int(time.time())-1;service.datagram_received(relay.canonical(expired),('attacker',9));self.assertEqual(service.clients['alice'][0],('a',1))
        service.datagram_received(b'{"v":1,"op":"bind","from":"alice"}',('attacker',9));self.assertEqual(service.clients['alice'][0],('a',1))
if __name__=='__main__':unittest.main()
