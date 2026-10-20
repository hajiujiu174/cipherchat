"""Run with the service stopped. Back up, retain account/password/contact data, retire public keys and old queued ciphertext."""
import argparse,base64,datetime,json,sqlite3
from pathlib import Path

def ordinary(packet):
    try:
        message=json.loads(packet);body=message['body'] if 'body' in message else message
        raw=base64.b64decode(body['payload'],validate=True)
        if body['kind']=='file_chunk':return raw.startswith(b'CCP4')
        return json.loads(raw).get('chat_mode')=='ordinary-v1'
    except (ValueError,KeyError,TypeError):return False

def migrate(database,apply):
    with sqlite3.connect(database,timeout=3) as db:
        counts={t:db.execute('SELECT count(*) FROM '+t).fetchone()[0] for t in ['accounts','friends','groups','group_members']}
        retired=db.execute("SELECT count(*) FROM accounts WHERE sign_pub!='' OR dh_pub!='' OR signed_prekey!='' OR spk_sig!=''").fetchone()[0]
        table=db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='pending_deliveries_v2'").fetchone()
        old=[]
        if table:old=[(rowid,) for rowid,payload in db.execute('SELECT rowid,ciphertext_json FROM pending_deliveries_v2') if not ordinary(payload)]
        report=dict(accounts_and_contacts=counts,accounts_with_old_public_keys=retired,old_pending_deliveries=len(old),applied=False)
        if not apply:return report
        backup=database.with_name(database.name+'.before-ordinary-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'.bak')
        if backup.exists():raise RuntimeError('Backup already exists')
        with sqlite3.connect(backup) as target:db.backup(target)
        with db:
            db.execute("UPDATE accounts SET sign_pub='',dh_pub='',signed_prekey='',spk_sig=''")
            db.execute('DELETE FROM one_time_prekeys')
            if old:db.executemany('DELETE FROM pending_deliveries_v2 WHERE rowid=?',old)
        assert counts=={t:db.execute('SELECT count(*) FROM '+t).fetchone()[0] for t in counts}
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
        report.update(applied=True,backup=str(backup),integrity_check='ok')
        return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('database',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
    if not a.database.is_file():p.error('database must already exist')
    print(json.dumps(migrate(a.database,a.apply),ensure_ascii=False,indent=2))
