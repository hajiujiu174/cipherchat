"""Back up and retire explicitly identified legacy file checkpoints, retaining chat and files.

Dry-run by default. Only use file IDs confirmed against the pre-upgrade backup.
No server data, downloaded files, chat history or feature tables are removed.
"""
from pathlib import Path
import argparse,datetime,json,sqlite3,struct,re
from contextlib import closing

PREFIX=b'CC-ORDINARY-1:'
class Cbor:
    def __init__(self,data):self.data=data;self.offset=0
    def take(self,n):
        value=self.data[self.offset:self.offset+n];self.offset+=n
        if len(value)!=n:raise ValueError('Truncated CBOR')
        return value
    def read(self):
        b=self.take(1)[0];major=b>>5;size=b&31
        if major==7:
            if size in (20,21,22):return {20:False,21:True,22:None}[size]
            if size in (25,26,27):return struct.unpack({25:'>e',26:'>f',27:'>d'}[size],self.take({25:2,26:4,27:8}[size]))[0]
            raise ValueError('Unsupported CBOR simple value')
        n=size if size<24 else int.from_bytes(self.take({24:1,25:2,26:4,27:8}[size]),'big')
        if major==0:return n
        if major==1:return -1-n
        if major==2:return self.take(n)
        if major==3:return self.take(n).decode('utf-8')
        if major==4:return [self.read() for _ in range(n)]
        if major==5:
            result={}
            for _ in range(n):key=self.read();result[key]=self.read()
            return result
        raise ValueError('Unsupported CBOR type')
def head(major,n):
    if n<24:return bytes([major*32+n])
    for tag,size in [(24,1),(25,2),(26,4),(27,8)]:
        if n<1<<(size*8):return bytes([major*32+tag])+n.to_bytes(size,'big')
    raise ValueError('CBOR integer overflow')
def encode(value):
    if value is None:return b'\xf6'
    if isinstance(value,bool):return b'\xf5' if value else b'\xf4'
    if isinstance(value,int):return head(0,value) if value>=0 else head(1,-1-value)
    if isinstance(value,float):return b'\xfb'+struct.pack('>d',value)
    if isinstance(value,bytes):return head(2,len(value))+value
    if isinstance(value,str):data=value.encode('utf-8');return head(3,len(data))+data
    if isinstance(value,list):return head(4,len(value))+b''.join(encode(v) for v in value)
    if isinstance(value,dict):return head(5,len(value))+b''.join(encode(k)+encode(v) for k,v in value.items())
    raise ValueError('Unsupported value')
def is_file_message(packet,ids,pending=False):
    mid=packet.get('id' if pending else 'messageId','')
    return packet.get('kind') in {'file_meta','file_ack','file_chunk'} and any(mid=='fmeta:'+fid or mid.startswith(('fack:'+fid+':','fchunk:'+fid+':')) for fid in ids)
def repair(path,ids,apply=False):
    path=path.resolve(strict=True);ids=set(ids)
    if not ids or any(not isinstance(v,str) or not re.fullmatch(r'[A-Za-z0-9+/=_-]{1,32}',v) for v in ids):raise ValueError('Explicit valid legacy file IDs required')
    db=sqlite3.connect(path.as_uri()+'?mode=rw' if apply else path.as_uri()+'?mode=ro',uri=True,timeout=5)
    backup=None
    try:
        db.execute('BEGIN IMMEDIATE' if apply else 'BEGIN')
        revision,blob=db.execute('SELECT revision,protected_snapshot FROM client_state WHERE singleton=1').fetchone()
        if not blob.startswith(PREFIX):raise ValueError('Only already-migrated ordinary snapshots are supported')
        decoder=Cbor(blob[len(PREFIX):]);snap=decoder.read()
        if decoder.offset!=len(blob)-len(PREFIX) or snap['revision']!=revision or snap['format']!=2:raise ValueError('Snapshot revision or format mismatch')
        before_history=encode(snap['history']);before_account=encode(snap['account'])
        known={t['id'] for t in snap['transfers']}
        if not ids<=known:raise ValueError('A requested file ID is absent; refusing partial repair')
        removed={};dirty=False
        for field,pending in [('outbox',False),('pending',True),('pending_sends',True)]:
            values=snap.get(field,[]);kept=[v for v in values if not is_file_message(v,ids,pending)]
            removed[field]=len(values)-len(kept);snap[field]=kept;dirty|=bool(removed[field])
        for t in snap['transfers']:
            if t['id'] not in ids:continue
            dirty|=t.get('transport_version')!=2 or t.get('last_ack') is not None
            t.update(transport_version=2,last_ack=None,pending_chunks=[],direct=None)
            if not t['complete']:t['cancelled']=True
        report=dict(dry_run=not apply,legacy_checkpoints=len(ids),retired_messages=removed,history_unchanged=True,account_unchanged=True,downloaded_files_untouched=True,changed=dirty)
        if apply and dirty:
            backup=path.with_name(path.name+'.before-legacy-file-retirement-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.bak')
            if backup.exists():raise FileExistsError(backup)
            # A separate read connection snapshots the committed preimage while this
            # connection holds the write reservation, including the original WAL.
            with closing(sqlite3.connect(path.as_uri()+'?mode=ro',uri=True)) as source,closing(sqlite3.connect(backup)) as target:source.backup(target)
            snap['revision']=revision+1
            assert encode(snap['history'])==before_history and encode(snap['account'])==before_account
            output=PREFIX+encode(snap)
            assert Cbor(output[len(PREFIX):]).read()==snap
            changed=db.execute('UPDATE client_state SET revision=?,protected_snapshot=? WHERE singleton=1 AND revision=? AND protected_snapshot=?',(revision+1,output,revision,blob))
            if changed.rowcount!=1:raise RuntimeError('Concurrent snapshot modification')
            db.commit();report.update(backup=str(backup),revision=revision+1)
        else:db.rollback()
        return report
    finally:
        if db.in_transaction:db.rollback()
        db.close()
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database',type=Path);parser.add_argument('--ids-json',type=Path,required=True);parser.add_argument('--apply',action='store_true');args=parser.parse_args()
    print(json.dumps(repair(args.database,json.loads(args.ids_json.read_text()),args.apply),ensure_ascii=False,indent=2))
