"""Unpack the client ZIP, check removed routes, and perform isolated TLS upgrade/rollback."""
import argparse, ctypes, hashlib, importlib.machinery, json, os, shutil, subprocess, time, zipfile
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('formal_zip',type=Path);parser.add_argument('--tls-fixture',type=Path,required=True);parser.add_argument('work',type=Path);args=parser.parse_args()
    project=Path(__file__).resolve().parent.parent;work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
    for archive in [args.formal_zip]:
        with zipfile.ZipFile(archive)as z:
            for name in z.namelist():
                target=(work/name).resolve()
                if not target.is_relative_to(work):raise RuntimeError('ZIP traversal')
            z.extractall(work)
    formal=work/args.formal_zip.stem
    system=Path(os.environ['SYSTEMROOT']);env=dict(os.environ,PATH=str(formal)+os.pathsep+str(system/'System32')+os.pathsep+str(system))
    for name in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QML2_IMPORT_PATH']:env.pop(name,None)
    probe=importlib.machinery.SourceFileLoader('package_probe',str(project/'scripts/test-update-package.py')).load_module()
    probe.u.GetWindowTextLengthW.argtypes=[ctypes.c_void_p];probe.u.GetWindowTextW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_int]
    def titled(exe, contains='CipherChat', exclude=''):
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            for hwnd,pid in probe.find(exe):
                length=probe.u.GetWindowTextLengthW(hwnd);title=ctypes.create_unicode_buffer(length+1);probe.u.GetWindowTextW(hwnd,title,length+1)
                if contains in title.value and (not exclude or exclude not in title.value):
                    response=ctypes.c_size_t()
                    if probe.u.SendMessageTimeoutW(hwnd,0,0,0,2,1000,ctypes.byref(response)):return hwnd,pid
            time.sleep(.1)
        raise TimeoutError('Expected application surface: '+contains)
    windows=[];owned_paths=[];server=None;running=None
    for package in [formal]:
        manifest=json.loads((package/'manifest.json').read_bytes())
        for item in manifest['files']:
            payload=package/item['path'];assert payload.is_file() and payload.stat().st_size==item['bytes']
            assert hashlib.sha256(payload.read_bytes()).hexdigest()==item['sha256'],item['path']
        assert (package/'README.md').read_bytes()==(package/'README.txt').read_bytes()
    release=json.loads((formal/'release.json').read_bytes());version=release['version'];sequence=release['sequence']
    assert (formal/'start-remote.cmd').is_file()
    assert not any(p.name.startswith(('cipherchat-demo-', 'start-demo')) for p in formal.rglob('*'))
    help_result=subprocess.run([str(formal/'cipherchat.exe'),'--help'],cwd=formal,env=env,capture_output=True,timeout=20)
    assert help_result.returncode==0 and b'--start-demo' not in help_result.stdout+help_result.stderr
    rejected=subprocess.run([str(formal/'cipherchat.exe'),'--start-demo'],cwd=formal,env=env,capture_output=True,timeout=20)
    assert rejected.returncode!=0, 'Removed command-line route must be rejected'
    (work/'removed-route.log').write_bytes(rejected.stdout+rejected.stderr)
    try:
        for ui in ['codex','rhine']:
            owned_paths.append(formal/'cipherchat.exe')
            with (work/('ui-'+ui+'-start.log')).open('wb') as startup_log:
                client=subprocess.Popen([str(system/'System32/cmd.exe'),'/d','/c',str(formal/'start-remote.cmd'),'--ui',ui,'--data-dir',str(work/('formal-profile-'+ui))],cwd=formal,env=env,stdout=startup_log,stderr=startup_log)
            window=titled(formal/'cipherchat.exe');windows.append((window,client))
            probe.close_window(*window);client.wait(timeout=15);windows.pop();assert client.returncode==0
        # Real local service uses the exact production update route and isolated certificate.
        server_run=work/'update-service';server_run.mkdir();feed_dir=work/'local-feed'
        (server_run/'CipherChatDemo-v1').write_text('Package acceptance',encoding='utf8')
        server=subprocess.Popen([str(args.tls_fixture.resolve()),str(server_run)],cwd=args.tls_fixture.resolve().parent,env={**env,'PATH':str(args.tls_fixture.resolve().parent)+os.pathsep+str(system/'System32')+os.pathsep+str(system),'CIPHERCHAT_UPDATE_ROOT':str(feed_dir)},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        deadline=time.monotonic()+15
        while not (server_run/'server-ready.json').exists():
            if server.poll()is not None or time.monotonic()>deadline:raise RuntimeError('Local update server failed')
            time.sleep(.1)
        port=json.loads((server_run/'server-ready.json').read_bytes())['port'];new=work/'local-release';shutil.copytree(formal,new)
        (new/'server-cert.pem').write_bytes((server_run/'server-cert.pem').read_bytes());(new/'update-config.json').write_text(json.dumps({'host':'127.0.0.1','port':port,'server_name':'localhost'}),encoding='utf8')
        feed=importlib.machinery.SourceFileLoader('local_feed',str(project/'scripts/build-update-feed.py')).load_module();feed.build(new,feed_dir,version,sequence,'Independent package acceptance')
        installed=work/'upgrade with spaces';shutil.copytree(new,installed)
        shutil.copy2(project/'artifacts/CipherChat-Glass-20261005/cipherchat.exe',installed/'cipherchat.exe')
        (installed/'acceptance-marker.txt').write_text('before upgrade',encoding='utf8')
        feed.build(installed,work/'prior-feed','2026.10.5.5',2026100505,'Local-only prior release fixture')
        marker=work/'upgrade-profile';marker.mkdir();(marker/'keep.txt').write_text('account data remains',encoding='utf8')
        helper=installed/'cipherchat-updater.exe';local_env={**env,'PATH':str(installed)+os.pathsep+str(system/'System32')+os.pathsep+str(system)}
        stage=subprocess.run([str(helper),'--stage',str(installed)],cwd=installed,env=local_env,capture_output=True,timeout=240);(work/'stage.log').write_bytes(stage.stdout+stage.stderr);assert stage.returncode==0,stage.stdout[-1000:]
        staged=json.loads(stage.stdout.splitlines()[-1]);assert staged['event']=='staged'
        params=['--data-dir',str(marker),'--host','127.0.0.1','--port',str(port),'--server-name','localhost','--ca',str(installed/'server-cert.pem'),'--ui','codex']
        subprocess.run([str(installed/'cipherchat-launcher.exe'),*params],cwd=installed,env=local_env,check=True,timeout=10)
        running=titled(installed/'cipherchat.exe');active=installed/'.updates/versions'/staged['version']
        for mode in ['--activate','--rollback']:
            window,pid=running;(installed/'.updates'/('restart-'+str(pid)+'.json')).write_text(json.dumps(params),encoding='utf8')
            command=[str(helper),mode,str(installed)]+([staged['version']]if mode=='--activate'else[])+['--wait-pid',str(pid)]
            updater=subprocess.Popen(command,cwd=installed,env=local_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);time.sleep(.2);assert updater.poll()is None
            probe.close_window(window,pid);out,err=updater.communicate(timeout=45);(work/(mode[2:]+'.log')).write_bytes(out+err);assert updater.returncode==0,out
            running=titled((active if mode=='--activate'else installed)/'cipherchat.exe')
        probe.close_window(*running);running=None;assert (installed/'current.txt').read_text()=='.';assert (marker/'keep.txt').read_text()=='account data remains'
        for item in json.loads((new/'release.json').read_bytes())['files']:assert (active/item['path']).read_bytes()==(new/item['path']).read_bytes()
        report={'passed':True,'version':version,'source':'independently unpacked final client ZIP','package_manifests_verified':True,'start_scripts_tested':True,'both_uis_started':True,'demonstration_files_absent':True,'removed_start_option_rejected':True,'local_TLS_stage':staged,'activated_relaunched_and_rolled_back':True,'isolated_data_preserved':True,'payload_bytes_equal':True,'public_server_modified':False}
        (work/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report))
    finally:
        if running is not None:
            try:probe.close_window(*running)
            except Exception:pass
        for window,client in windows:
            try:probe.close_window(*window);client.wait(timeout=15)
            except Exception:client.terminate();client.wait(timeout=5)
        # Detached starter children belong to this fresh extraction only. Close a
        # application surface if a startup assertion interrupted its tracking.
        for exe in set(owned_paths):
            for _ in range(2):
                for hwnd,pid in probe.find(exe):
                    try:probe.close_window(hwnd,pid)
                    except Exception:pass
        if server is not None:server.terminate();server.wait(timeout=15)

if __name__=='__main__':main()
