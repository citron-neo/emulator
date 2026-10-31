"""Actual session transitions and nvmap source recapture, with controlled devices/PTEs."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
p.add_argument('--baseline', action='store_true')
p.add_argument('--revision')
a = p.parse_args()
if a.repeat < 1 or (a.revision and not a.baseline): p.error('positive repeat; revision requires baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-session-source').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()
inputs.add('tools/gpu_lifecycle_tests/run_session_source_tests.py')
def read(path):
    inputs.add(path)
    return (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root).decode()
            if a.revision and path.startswith('src/') else (root / path).read_text(encoding='utf-8')).replace('\r\n', '\n')
def block(text, marker):
    start=text.index(marker); end=text.index('{', start)+1; depth=1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
base='src/core/hle/service/nvdrv/core/'
container=read(base+'container.cpp'); header=read(base+'container.h')
nvmap=read(base+'nvmap.cpp'); nvmap_h=read(base+'nvmap.h')
open_session=block(container, 'SessionId Container::OpenSession(')
# Keep the actual final return; application heap preallocation is not this fixture's scope.
open_session=open_session[:open_session.index('    // Optimization')]+open_session[open_session.rindex('\n    return '):]
methods='\n'.join([open_session, block(container, 'void Container::CloseSession(')])
validate=(block(container, 'bool Container::IsDisplaySessionActive(')
          if 'Container::IsDisplaySessionActive(' in container else '')
validate=validate.replace('    std::scoped_lock', '    if (before_validation) before_validation();\n    std::scoped_lock', 1)
getter=block(nvmap, 'std::optional<Common::FrameMappingDomain::Stamp> NvMap::GetDisplayMapping(')
getter=getter.replace('    return mapping;', '    if (after_validation) after_validation();\n    return mapping;')
nvmethods='\n'.join(block(nvmap, m) for m in ['NvMap::Handle::Handle(', 'NvResult NvMap::Handle::Alloc(',
    'std::shared_ptr<NvMap::Handle> NvMap::GetHandle('])+'\n'+getter
fixture='tools/gpu_lifecycle_tests/session_source_fixture.cpp.in'
source=read(fixture)
nvdisp=read('src/core/hle/service/nvdrv/devices/nvdisp_disp0.cpp')
generation_field=re.search(r'u64 next_session_generation\{[^}]*\};',container)
for key,value in {'SESSION_ID':block(header,'struct SessionId {')+';',
                  'HANDLE':block(nvmap_h,'    struct Handle {').replace('std::mutex mutex;', 'TrackedMutex mutex;')+';',
                  'METHODS':methods,'VALIDATE':validate,'NVMETHODS':nvmethods,
                  'NVDISP':block(nvdisp,'void nvdisp_disp0::Composite('),
                  'BLEND':block(nvdisp,'Tegra::BlendMode ConvertBlending('),
                  'GENERATION_FIELD':generation_field.group(0) if generation_field else 'u64 next_session_generation{1};',
                  'HAS_GENERATION':str(int('u64 generation' in block(header,'struct SessionId {')))}.items():
    source=source.replace('@'+key+'@',value)
domain=read('src/common/frame_mapping.h').replace('std::mutex mutex;', 'ObservedMutex mutex;')
(out/'common').mkdir(exist_ok=True)
(out/'common/frame_mapping.h').write_text(domain,encoding='utf-8')
(out/'frame_mapping_observer.h').write_text(read('tools/gpu_lifecycle_tests/frame_mapping_observer.h'),encoding='utf-8')
def build(name,text):
    path=out/(name+'.cpp');path.write_text(text,encoding='utf-8')
    exe=out/(name+('.exe' if os.name=='nt' else ''))
    result=subprocess.run([a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
        '-Wno-unused-parameter','-I',str(out),'-I',str(root/'src'),str(path),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode: raise RuntimeError(result.stderr)
    return exe
def run(exe,name,case,witness=None):
    result=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20)
    log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
    assert ((result.returncode==91 and 'FAIL '+witness in log) if witness else
            (result.returncode==0 and 'PASS session source '+case in log)),(name,result.returncode,log)
exe=build('production',source)
inputs.update(['src/core/hle/service/nvnflinger/ui/graphic_buffer.h',
               'src/core/hle/service/nvnflinger/hwc_layer.h', 'src/video_core/framebuffer_config.h'])
baseline_cases=[('shutdown','retired session recaptured source'),('stale','retired session recaptured source'),
                ('stale-stamp','stale session retained queued source stamp')]
cases=['normal','shutdown','stale','stale-stamp','refcount','slot-reuse','late-close',
       'before-validation','after-validation','active-shutdown','active-stale',
       'failure','wrong-pid','invalid','exhaustion']
controls=[]
if a.baseline:
    for i in range(a.repeat):
        for case,witness in baseline_cases: run(exe,f'baseline-{case}-{i}',case,witness)
    print('REPRODUCED shutdown/stale recapture and stale queued stamp',flush=True)
else:
    for i in range(a.repeat):
        for case in cases: run(exe,f'{case}-{i}',case)
    print('PASS fifteen session source paths',flush=True)
    late_capture='''std::optional<Common::FrameMappingDomain::Stamp> NvMap::GetDisplayMapping(Handle::Id handle) {
        auto description=GetHandle(handle);if(!description)return {};
        SessionId session_id{};
        {std::scoped_lock lock{description->mutex};session_id=description->session_id;}
        if(!core.IsDisplaySessionActive(session_id))return {};
        if(after_validation)after_validation();
        std::scoped_lock lock{description->mutex};
        return host1x.MemoryManager().CaptureFrameMapping(description->d_address,description->aligned_size);
    }'''
    variants=[
        ('capture-after-validation',source.replace(getter,late_capture),
         'after-validation','retirement interleaving admitted source use'),
        ('omit-session-validation',source.replace('!core.IsDisplaySessionActive(session_id)', 'false'),
         'shutdown','retired session recaptured source'),
        ('omit-source-incarnation',source.replace('!id.generation || id.generation != session.id.generation ||', 'false ||'),
         'slot-reuse','reused slot authorized old source'),
        ('omit-stale-revocation',source.replace('                auto stale_sources = impl->host1x.MemoryManager().BeginFrameMappingMutation();','').replace('                stale_sources.Complete();',''),
         'stale-stamp','stale session retained queued source stamp'),
        ('omit-close-incarnation',source.replace('session_id.generation != session.id.generation || ', ''),
         'late-close','late close affected replacement session'),
        ('omit-pid-check',source.replace('session.pid == session.process->GetProcessId()', 'true'),
         'wrong-pid','PID mismatch recaptured source'),
        ('lose-published-incarnation',source.replace('\n    return session.id;','\n    return SessionId{new_id};'),
         'normal','live source refused'),
        ('omit-generation-exhaustion',source.replace('    if (impl->next_session_generation == 0)\n        throw std::runtime_error("nvdrv session generation exhausted");',''),
         'exhaustion','generation exhaustion registered reusable identity'),
        ('hold-handle-over-validation',source.replace('    if (!mapping || !core.IsDisplaySessionActive(session_id)) return {};',
            '    std::scoped_lock wrong_lock{description->mutex};\n    if (!mapping || !core.IsDisplaySessionActive(session_id)) return {};'),
         'before-validation','source validation retained handle/table lock'),
    ]
    for name,changed,case,witness in variants:
        assert changed!=source,name
        mutant=build(name,changed)
        for i in range(a.repeat):run(mutant,f'{name}-{i}',case,witness)
        controls.append(name);print('PASS session source control: '+name,flush=True)
(out/'extraction.json').write_text(json.dumps({
    'baseline':a.baseline,'revision':a.revision,'repetitions':a.repeat,
    'cases':[c[0] for c in baseline_cases] if a.baseline else cases,'controls':controls,
    'sha256':{path:hashlib.sha256(subprocess.check_output(['git','show',a.revision+':'+path],cwd=root)
        if a.revision and path.startswith('src/') else (root/path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'actual':['SessionId and Handle layouts; Handle constructor/Alloc; GetHandle/GetDisplayMapping',
              'OpenSession admission/reuse prefix and actual final return; full CloseSession and IsDisplaySessionActive',
              'nvdisp Composite conversion; real HwcLayer, source packet and framebuffer headers',
              'real FrameMappingDomain with observed std::mutex waits'],
    'fixtures':['Session owner storage/Process reference primitives, backing and mapper, device mapping/callbacks',
                'GraphicBuffer public construction/destruction, System/GPU publication; application heap optimization omitted',
                'no real Memory/GetPointerImpl, native driver or device'],
    'boundary':'No new frame-source capture for retired or replaced session; queued stale stamp revoked; native lifetime and content authority remain separate.'
},indent=2),encoding='utf-8')
