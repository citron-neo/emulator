# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual VI create/destroy/getter and Module Close; partial-effect owner protocol."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--revision')
a=p.parse_args()
if a.repeat<1 or (a.revision and not a.baseline):p.error('positive repeat; revision only with baseline')
root=Path(__file__).resolve().parents[2]
out=(a.output_dir or root/'.diagnostics/m4-vi-session-owner').resolve();out.mkdir(parents=True,exist_ok=True)
cpp='src/core/hle/service/vi/shared_buffer_manager.cpp';hdr='src/core/hle/service/vi/shared_buffer_manager.h'
def read(path):
    return (subprocess.check_output(['git','show',a.revision+':'+path],cwd=root).decode('utf-8')
            if a.revision else (root/path).read_text(encoding='utf-8')).replace('\r\n','\n')
def method(text,marker):
    start=text.index(marker);pos=text.index('{',start);end=pos+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
production=read(cpp);header=read(hdr)
methods='\n'.join(method(production,marker) for marker in ['Result SharedBufferManager::CreateSession(',
    'void SharedBufferManager::DestroySession(','Result SharedBufferManager::GetSharedBufferMemoryHandleId('])
structs=''
if 'struct SharedBufferOwners {' in header:structs=method(header,'struct SharedBufferOwners {')+';\n'
structs+=method(header,'struct SharedBufferSession {')+';'
close=method((root/'src/core/hle/service/nvdrv/nvdrv.cpp').read_text(encoding='utf-8'),'NvResult Module::Close(').replace('Module::','')
fixture='tools/physical_coherency_tests/vi_session_fixture.cpp.in'
source=(root/fixture).read_text(encoding='utf-8').replace('// @CLOSE_METHOD@',close).replace('// @SESSION_STRUCTS@',structs).replace('// @METHODS@',methods)
allocate=method(production,'Result AllocateSharedBufferMemory(').replace('Core::System&','System&')
source=source.replace('// @ALLOCATE_HELPER@',allocate)
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-I',str(root/'src')]
def build(name,text):
    path=out/(name+'.cpp');path.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(flags+[str(path),'-o',str(exe)],check=True);return exe
def run(exe,name,case):
    child=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=15);log=child.stdout+child.stderr
    (out/(name+'.log')).write_text(log,encoding='utf-8');return child.returncode,log
exe=build('production',source)
if a.baseline:
    witnesses={}
    for case,expected in [('layer','failed create published partial session'),('close-race','refused Close released session owner')]:
        code,log=run(exe,'baseline-'+case,case);assert code==91 and 'FAIL '+expected in log,(case,code,log)
        witnesses[case]=log.strip();print('REPRODUCED '+expected,flush=True)
    (out/'baseline.json').write_text(json.dumps({'revision':a.revision,'witnesses':witnesses,
        'source_sha256_lf':{cpp:hashlib.sha256(production.encode()).hexdigest(),hdr:hashlib.sha256(header.encode()).hexdigest()}},indent=2),encoding='utf-8');raise SystemExit(0)
cases=['normal','prepare','healthy-drop','closed-create','create-race','closed','close-race','allocate','fill','map','session','open','handle','layer','blend','producer','slot0','slot1','throw-map','throw-session','throw-open','throw-layer','throw-slot1',
       'destroy-layer-destroy','destroy-handle-free','destroy-throw-layer-destroy','destroy-throw-handle-free','destroy-throw-device-close','destroy-throw-session-close']
for i in range(a.repeat):
    for case in cases:
        code,log=run(exe,f'{case}-{i}',case);assert code==0 and 'PASS VI session '+case in log,(case,code,log)
print(f'PASS VI session ownership: {len(cases)} paths ({a.repeat} repetitions)',flush=True)
controls=[
 ('publish-partial',source.replace('    R_UNLESS(m_sessions.at(applet_resource_user_id).status == SharedBufferSession::Status::Ready,\n             VI::ResultNotFound);',''),
  'layer','failed create published partial session'),
 ('ignore-Close-refusal',source.replace('if (m_nvdrv->Close(session.nvmap_fd) != Nvidia::NvResult::Success) return;',
  '(void)m_nvdrv->Close(session.nvmap_fd);'),'close-race','refused Close released session owner'),
 ('ignore-closed-retirement',source.replace('if (!dispatch || !session.owners->container) return;',
  'if (!session.owners->container) return;'),'closed','closed destroy performed prefix effects'),
 ('omit-create-quarantine',source.replace('if (session.status != SharedBufferSession::Status::Ready)',
  'if (false)'),'layer','failed prefix released owners or reopened admission'),
 ('omit-VI-anchor',source.replace('owners->container = container_owner;',
  'owners->container.reset();'),'layer','failed prefix lost VI/provider/pool owners'),
 ('release-outside-count',source.replace('    completed.retention.reset();',
  '    dispatch.reset();\n    completed.retention.reset();'),'normal','final release escaped counted destroy'),
 ('release-under-manager-lock',source.replace('    lk.unlock();',''),
  'normal','final Process owner release held manager lock'),
 ('healthy-VI-cycle',source.replace('    session.owners->container.reset(); // No self-cycle for a healthy session.',''),
  'healthy-drop','healthy committed session kept VI self cycle'),
 ('omit-slot-owner',source.replace('    session.owners->producer = producer;',
  '    session.owners->producer.reset();'),'slot1','unknown slot prefix lost producer owner'),
]
late=allocate.replace('*out_page_group = std::make_unique','auto pg = std::make_unique').replace('    auto& pg = *out_page_group;','')
late=late.replace('    R_SUCCEED();','    *out_page_group = std::move(pg);\n    R_SUCCEED();')
controls.append(('late-pool-owner',source.replace(allocate,late),'fill','failed prefix lost VI/provider/pool owners'))
for name,changed,case,expected in controls:
    assert changed!=source,name
    mutant=build(name,changed)
    for i in range(a.repeat):
        code,log=run(mutant,f'{name}-{i}',case);assert code==91 and 'FAIL '+expected in log,(name,code,log)
    print('PASS VI session control: '+name,flush=True)
inputs=[cpp,hdr,'src/core/hle/service/vi/container.h','src/core/hle/service/nvdrv/nvdrv.cpp',
        'src/core/hle/service/nvdrv/nvdrv.h','src/common/owned_retirement.h','src/common/producer_drain.h',fixture,
        'tools/physical_coherency_tests/run_vi_session_owner_tests.py']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in inputs},
    'repetitions':a.repeat,'cases':cases,'controls':[c[0] for c in controls],'actual':['VI CreateSession/DestroySession/getter, pool allocation helper and session owner structures','Module::Close','OwnedRetirementQueue/ProducerDrain/ScopeGuard'],
    'fixtures':['Process/backing retainer, allocator/mapping, nvmap/layer/slot effects and providers'],
    'boundary':'Partial-owner protocol fixture and production compilation, not real KProcess/page-table/native GPU acceptance.'},indent=2),encoding='utf-8')
