# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual VI terminal/lazy publication methods and production stop callback."""
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
if a.repeat<1 or (a.revision and not a.baseline):p.error('positive repeat; revision requires baseline')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-vi-termination').resolve();out.mkdir(parents=True,exist_ok=True)
cpp='src/core/hle/service/vi/container.cpp';header='src/core/hle/service/vi/container.h';factory='src/core/hle/service/vi/vi.cpp'
def read(name):return (subprocess.check_output(['git','show',a.revision+':'+name],cwd=root).decode() if a.revision else (root/name).read_text(encoding='utf-8')).replace('\r\n','\n')
def method(text,marker):
    start=text.index(marker);pos=text.index('{',start);end=pos+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
production=read(cpp);sig=('bool' if 'bool Container::EnsureInitialized()' in production else 'void')+' Container::EnsureInitialized()'
markers=['void Container::OnTerminate()',sig,'SharedBufferManager* Container::GetSharedBufferManager()',
         'Result Container::GetBinderDriver(','bool Container::ComposeOnDisplay(']
methods='\n'.join(method(production,m) for m in markers)
declarations='\n'.join(method(production,m).split('{',1)[0].replace('Container::','')+';' for m in markers)
stop_line=next(line for line in read(factory).splitlines() if 'std::stop_callback cb(' in line)
fixture='tools/physical_coherency_tests/vi_termination_fixture.cpp.in'
source=(root/fixture).read_text(encoding='utf-8').replace('// @DECLARATIONS@',declarations).replace('// @METHODS@',methods).replace('// @STOP_CALLBACK@',stop_line)
manager_path='src/core/hle/service/vi/manager_display_service.cpp';system_path='src/core/hle/service/vi/system_display_service.cpp';am_path='src/core/hle/service/am/display_layer_manager.cpp'
manager_methods='\n'.join(method(read(manager_path),m) for m in ['Result IManagerDisplayService::CreateSharedLayerSession(','void IManagerDisplayService::DestroySharedLayerSession('])
system_methods='\n'.join(method(read(system_path),m) for m in ['Result ISystemDisplayService::GetSharedBufferMemoryHandleId(',
 'Result ISystemDisplayService::AcquireSharedFrameBuffer(','Result ISystemDisplayService::PresentSharedFrameBuffer(',
 'Result ISystemDisplayService::GetSharedFrameBufferAcquirableEvent(','Result ISystemDisplayService::CancelSharedFrameBuffer('])
def declarations_for(text,cls):
    # Bodies contain nested braces; obtain each signature from extraction again.
    import re
    return '\n'.join(match.group(0).rsplit('{',1)[0].replace(cls+'::','')+';' for match in re.finditer(r'(?:Result|void) '+cls+r'::[^{}]+\{',text))
source=source.replace('// @MANAGER_DECLARATIONS@',declarations_for(manager_methods,'IManagerDisplayService')).replace('// @SYSTEM_DECLARATIONS@',declarations_for(system_methods,'ISystemDisplayService'))
source=source.replace('// @SERVICE_METHODS@',manager_methods+'\n'+system_methods).replace('// @AM_METHOD@',method(read(am_path),'Result DisplayLayerManager::WriteAppletCaptureBuffer('))
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
    for case,witness in [('cold','terminated cold VI published providers'),('late','late VI initialization published after stop and GPU drain'),('retained','retained terminated VI lent session entry')]:
        code,log=run(exe,'baseline-'+case,case);assert code==91 and 'FAIL '+witness in log,(case,code,log)
        witnesses[case]=log.strip();print('REPRODUCED '+witness,flush=True)
    (out/'baseline.json').write_text(json.dumps({'revision':a.revision,'witnesses':witnesses},indent=2),encoding='utf-8');raise SystemExit(0)
cases=['cold','late','retained','normal','concurrent','entry-races','unavailable','closed-callers','open-callers']
for i in range(a.repeat):
    for case in cases:
        code,log=run(exe,f'{case}-{i}',case);assert code==0 and 'PASS VI terminal publication '+case in log,(case,code,log)
print(f'PASS nine VI terminal publication paths ({a.repeat} repetitions)',flush=True)
ensure=method(production,sig);getter=method(production,markers[2]);binder_getter=method(production,markers[3]);compose=method(production,markers[4])
gate='        if (m_is_shut_down) return false;'
assert ensure.count(gate)==2
late=''.join(ensure.rsplit(gate,1))
controls=[
 ('omit-terminal-gates',source.replace(ensure,ensure.replace(gate,'')),'cold','terminated cold VI published providers'),
 ('omit-late-publication-check',source.replace(ensure,late),'late','late VI initialization published after stop and GPU drain'),
 ('omit-manager-recheck',source.replace(getter,getter.replace('    if (m_is_shut_down) return nullptr;','')),
  'entry-races','accessor lent manager after termination won second lock'),
 ('omit-binder-recheck',source.replace(binder_getter,binder_getter.replace('    R_UNLESS(!m_is_shut_down, VI::ResultOperationFailed);','')),
  'entry-races','binder accessor published after termination won second lock'),
 ('omit-compose-recheck',source.replace(compose,compose.replace('    if (m_is_shut_down) return false;','')),
  'entry-races','composition ran after termination won second lock'),
 ('initialized-before-terminal',source.replace(ensure,ensure.replace(
  gate+'\n        if (m_is_initialized) return true;',
  '        if (m_is_initialized) return true;\n'+gate,1)),
  'retained','retained terminated VI lent session entry'),
 ('lock-across-lookup',source.replace(ensure,ensure.replace('    {\n        std::scoped_lock lk{m_lock};',
  '    std::scoped_lock lk{m_lock};\n    {',1)),
  'late','VI initialization held publication lock across lookup'),
 ('success-on-refusal',source.replace('    R_UNLESS(manager, VI::ResultOperationFailed);','    if (!manager) return ResultSuccess;'),
  'closed-callers','create caller mishandled terminal refusal'),
 ('discard-retained-manager',source.replace('    m_is_shut_down = true;','    m_is_shut_down = true;\n    m_shared_buffer_manager.reset();'),
  'retained','terminal refusal released retained session provider'),
]
for name,changed,case,expected in controls:
    assert changed!=source,name
    mutant=build(name,changed)
    for i in range(a.repeat):
        code,log=run(mutant,f'{name}-{i}',case);assert code==91 and 'FAIL '+expected in log,(name,code,log)
    print('PASS VI terminal publication control: '+name,flush=True)
inputs=[cpp,header,factory,manager_path,system_path,am_path,fixture,'tools/physical_coherency_tests/run_vi_termination_tests.py']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in inputs},
 'repetitions':a.repeat,'cases':cases,'controls':[c[0] for c in controls],'actual':['OnTerminate/EnsureInitialized/GetSharedBufferManager/GetBinderDriver/ComposeOnDisplay','Eight VI/AM shared-manager caller bodies','VI stop callback registered with real stop_source'],
 'fixtures':['provider/service lookups, display/layer lists, manager/conductor construction','GPU drain marker after stop callback; not native GPU or actual System body'],
 'boundary':'Terminal publication refusal only; retained session payload marker, not real KProcess or native drain acceptance.'},indent=2),encoding='utf-8')
