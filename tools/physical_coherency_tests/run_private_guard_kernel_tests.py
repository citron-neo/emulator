"""Real Linux mmap/mprotect store probes; lifecycle certificates injected only here."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
if not sys.platform.startswith('linux'):
    print('SKIP private guard kernel probes: Linux required; no positive proof')
    raise SystemExit(0)
root=Path(__file__).resolve().parents[2];out=root/'.diagnostics/m4-private-guard';out.mkdir(parents=True,exist_ok=True)
hp='src/video_core/renderer_vulkan/vk_private_producer_guard.h';tp='tools/physical_coherency_tests/private_guard_kernel_tests.cpp'
header=(root/hp).read_text().replace('#pragma once','');source=(root/tp).read_text()
inc='#include "video_core/renderer_vulkan/vk_private_producer_guard.h"'
command=[os.environ.get('CXX','g++'),'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Wshadow','-Werror','-I',str(root/'src')]
def run(name,text,positive=False):
    path=out/(name+'.cpp');path.write_text(text);exe=out/name
    subprocess.run(command+[str(path),'-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(name+'.txt')).write_text(r.stdout+r.stderr)
    if positive:
        assert r.returncode==0,r.stderr
        for repeat in range(1,4):
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            (out/f'kernel-repeat-{repeat}.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
        print(r.stdout,end='')
    else:
        assert r.returncode!=0 and 'Assertion' in r.stderr,(name,r.returncode,r.stderr)
        print('PASS expected private kernel mutation assertion:',name)
mutants=[
 ('no-readonly-protection','(writable?PROT_WRITE:0)','(static_cast<void>(writable),PROT_WRITE)'),
 ('double-admission','state->closed || state->active ||','state->closed ||'),
 ('drop-unlocks','~PrivateProducerGuard() {Quarantine();}','~PrivateProducerGuard() {(void)CompleteChecked();}'),
 ('quarantine-reopens','if(!done && state->active==this) state->CloseLocked();','if(!done && state->active==this) {(void)state->Protect(true);state->closed=true;}'),
 ('old-completion-unlocks-new','if(done) return true;','if(done) return state->Protect(true);'),
 ('worker-ignores-epoch','state->active==this && state->Live();','state->active==this;'),
]
# Red first: a page-permission-free implementation must fail an actual store probe.
run('baseline-unprotected-red',source.replace(inc,header.replace(mutants[0][1],mutants[0][2])))
run('kernel',source,True)
for name,guard,replacement in mutants:
    assert header.count(guard)==1,name
    run(name,source.replace(inc,header.replace(guard,replacement)))
paths=[hp,tp,'tools/physical_coherency_tests/run_private_guard_kernel_tests.py','src/video_core/buffer_cache/physical_write_identity.h']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'mutants':6,
 'boundary':'Real Linux OS store exclusion; completion transitions injected here, real checked GPU integration tested separately'},indent=2))
