"""Read an undamaged exported QR PNG and compare its public keys to the UI fingerprint.

This test-only reader supports byte-mode QR, without camera detection/error repair.
Standard ECC block sizes are read from the pinned Nayuki source; no encoder is called.
"""
import argparse, base64, hashlib, json, re
from pathlib import Path
from PIL import Image, ImageChops

def decode(path):
    im=Image.open(path).convert('L');box=ImageChops.invert(im).getbbox();assert box
    scale=box[0]//4;assert scale>0 and im.width==im.height and im.width%scale==0
    n=im.width//scale-8;version=(n-17)//4;assert n==version*4+17 and 1<=version<=40
    m=[[im.getpixel(((x+4)*scale+scale//2,(y+4)*scale+scale//2))<128 for x in range(n)]for y in range(n)]
    f=[[False]*n for _ in range(n)]
    def mark(x,y):
        if 0<=x<n and 0<=y<n:f[y][x]=True
    for i in range(n):mark(6,i);mark(i,6)
    for cx,cy in [(3,3),(n-4,3),(3,n-4)]:
        for dy in range(-4,5):
            for dx in range(-4,5):mark(cx+dx,cy+dy)
    positions=[]
    if version>1:
        count=version//7+2;step=26 if version==32 else (version*4+count*2+1)//(count*2-2)*2
        positions=[6]+[n-7-step*i for i in reversed(range(count-1))]
    for i,x in enumerate(positions):
        for j,y in enumerate(positions):
            if (i,j) in [(0,0),(0,len(positions)-1),(len(positions)-1,0)]:continue
            for dy in range(-2,3):
                for dx in range(-2,3):mark(x+dx,y+dy)
    coords=[(8,i)for i in range(6)]+[(8,7),(8,8),(7,8)]+[(14-i,8)for i in range(9,15)]
    raw=sum(int(m[y][x])<<i for i,(x,y)in enumerate(coords))^0x5412
    data=raw>>10;rem=data
    for _ in range(10):rem=(rem<<1)^((rem>>9)*0x537)
    assert raw==(data<<10|rem),'QR format checksum'
    ec={1:0,0:1,3:2,2:3}[data>>3];mask=data&7
    for x,y in coords:mark(x,y)
    for i in range(8):mark(n-1-i,8)
    for i in range(8,15):mark(8,n-15+i)
    mark(8,n-8)
    if version>=7:
        for i in range(18):mark(n-11+i%3,i//3);mark(i//3,n-11+i%3)
    masks=[lambda x,y:(x+y)%2==0,lambda x,y:y%2==0,lambda x,y:x%3==0,lambda x,y:(x+y)%3==0,
           lambda x,y:(x//3+y//2)%2==0,lambda x,y:x*y%2+x*y%3==0,
           lambda x,y:(x*y%2+x*y%3)%2==0,lambda x,y:((x+y)%2+x*y%3)%2==0]
    bits=[];right=n-1
    while right>=1:
        if right==6:right=5
        for k in range(n):
            y=n-1-k if ((right+1)&2)==0 else k
            for x in [right,right-1]:
                if not f[y][x]:bits.append(int(m[y][x])^int(masks[mask](x,y)))
        right-=2
    raw_words=len(bits)//8;words=[sum(bits[k+i]<<(7-i)for i in range(8))for k in range(0,raw_words*8,8)]
    source=(Path(__file__).resolve().parent.parent/'third_party/qrcodegen/qrcodegen.cpp').read_text()
    def table(name):
        part=source.split('QrCode::'+name+'[4][41] = {')[1].split('};')[0]
        return [list(map(int,re.findall(r'-?\d+',row)))for row in re.findall(r'\{([^{}]+)\}',part)]
    block_count=table('NUM_ERROR_CORRECTION_BLOCKS')[ec][version];ecc=table('ECC_CODEWORDS_PER_BLOCK')[ec][version]
    short=raw_words//block_count-ecc;short_count=block_count-raw_words%block_count
    blocks=[[]for _ in range(block_count)];cursor=0
    for i in range(short+1):
        for j in range(block_count):
            if i==short and j<short_count:continue
            blocks[j].append(words[cursor]);cursor+=1
    data_bits=''.join(f'{v:08b}'for block in blocks for v in block);offset=0
    def read(count):
        nonlocal offset
        value=int(data_bits[offset:offset+count],2);offset+=count;return value
    assert read(4)==4,'Expected identity byte mode'
    length=read(8 if version<10 else 16);payload=bytes(read(8)for _ in range(length)).decode('utf8')
    return payload,version,mask

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);a=p.parse_args()
    payload,version,mask=decode(a.evidence/'identity-qr.png');assert payload.startswith('CipherChatIdentity:1\n')
    card=json.loads(payload.split('\n',1)[1]);assert set(card)=={'version','server','username','signing','dh'} and card['version']==1
    signing=base64.b64decode(card['signing'],validate=True);dh=base64.b64decode(card['dh'],validate=True);assert len(signing)==len(dh)==32
    digest=hashlib.sha256(signing+dh).hexdigest().upper();shown=''.join((a.evidence/'identity-fingerprint.txt').read_text().split()).upper();assert digest==shown
    result={'passed':True,'QR_version':version,'mask':mask,'payload_marker':True,'complete_public_keys':True,'fingerprint_matches':True,'scope_present':bool(card['server']),'reader':'test-only undamaged PNG, no camera/error repair'}
    (a.evidence/'identity-qr-result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result))
if __name__=='__main__':main()
