"""Actual presentation frame-reuse completion and an open accelerated-owner witness."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--revision')
a=p.parse_args()
if a.repeat<1 or (a.revision and not a.baseline):p.error('positive repeat; revision requires baseline')
root=Path(__file__).resolve().parents[2]
out=(a.output_dir or root/'.diagnostics/m4-present-completion').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_present_completion_tests.py'}
def read(path):
    inputs.add(path)
    return (subprocess.check_output(['git','show',a.revision+':'+path],cwd=root).decode()
            if a.revision and path.startswith('src/') else (root/path).read_text(encoding='utf-8')).replace('\r\n','\n')
def block(text,marker):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
present_path='src/video_core/renderer_vulkan/vk_present_manager.cpp'
present=read(present_path)
methods='\n'.join(block(present,m) for m in ['Frame* PresentManager::GetRenderFrame(',
    'void PresentManager::Present(', 'void PresentManager::WaitPresent(',
    'void PresentManager::PresentThread(', 'void PresentManager::ReleaseRenderFrame(',
    'void PresentManager::Shutdown('])
wrapper=read('src/video_core/vulkan_common/vulkan_wrapper.h')
fence=block(wrapper,'class Fence :')
wait=block(fence,'    VkResult Wait(');reset=block(fence,'    void Reset(')
failure=out/'terminal-fail-stop.cpp';failure.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8')
read('src/common/terminal_fail_stop.h')
fixture='tools/gpu_lifecycle_tests/present_completion_fixture.cpp.in'
source=read(fixture).replace('@METHODS@',methods).replace('@WAIT@',wait).replace('@RESET@',reset)
isolated=out/'include/video_core';isolated.mkdir(parents=True,exist_ok=True)
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
       '-I',str(out/'include'),'-I',str(root/'src')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    result=subprocess.run(flags+[str(cpp),str(failure),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(result.stderr)
    return exe
def run(exe,name,case,code=0,witness=None,stage=None,result=None):
    child=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20)
    log=child.stdout+child.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
    assert child.returncode==code,(name,child.returncode,log)
    if code==0:assert 'PASS present completion '+case in log,(name,log)
    elif code==91:assert 'FAIL '+witness in log,(name,log)
    else:
        assert f'CITRON TERMINAL FAIL-STOP stage={stage} ' in log,(name,log)
        assert f'vk_result={result}\n' in log,(name,log)
        assert not any(s in log for s in ['DESTROY ', 'ATEXIT_HANDLER','FRAME_RETURNED']), (name,log)
        reason='vulkan_result' if stage=='present.frame_completion' else 'vulkan_exception'
        assert 'reason='+reason+' ' in log,(name,log)
exe=build('production',source)
if a.baseline:
    run(exe,'baseline-failed-wait','wait--4',91,'unconfirmed native completion reached fence reset')
    print('REPRODUCED failed native fence wait authorized frame reuse',flush=True)
else:
    cases=['normal','repeat','pending','empty','limit','async','sync','host-drain']
    failures=[('wait-'+str(n),'present.frame_completion',n) for n in [-4,-1,1,2]]
    failures += [('reset-'+str(n),'present.frame_reset',n) for n in [-4,-1]]
    for i in range(a.repeat):
        for case in cases:run(exe,f'{case}-{i}',case)
        for case,stage,result in failures:run(exe,f'{case}-{i}',case,86,stage=stage,result=result)
    print('PASS eight reuse/worker paths and six terminal failures',flush=True)
    controls=[
        ('ignore-wait-result',source.replace('if (completed != VK_SUCCESS)','if (false)'),
         'wait--4','unconfirmed native completion reached fence reset'),
        ('omit-native-wait',source.replace('const auto completed = frame->present_done.Wait();','const auto completed = VK_SUCCESS;'),
         'normal','unconfirmed native completion reached fence reset'),
        ('omit-fence-reset',source.replace('        frame->present_done.Reset();',''),
         'normal','confirmed frame did not reset exactly once'),
        ('return-before-confirmation',source.replace('    const char* stage = "present.frame_completion";',
            '    return frame;\n    const char* stage = "present.frame_completion";'),
         'normal','frame returned without checked completion'),
    ]
    for name,changed,case,witness in controls:
        assert changed!=source,name
        mutant=build(name,changed)
        for i in range(a.repeat):run(mutant,f'{name}-{i}',case,91,witness)
        print('PASS present completion control: '+name,flush=True)

# Historical owner witness is kept only by the pinned baseline run.
if a.baseline:
    # A defect witness, deliberately not acceptance of native owner handoff.
    owner_fixture='tools/gpu_lifecycle_tests/accelerated_owner_fixture.cpp.in'
    raster=read('src/video_core/renderer_vulkan/vk_rasterizer.cpp')
    info=block(read('src/video_core/renderer_vulkan/vk_blit_screen.h'),'struct FramebufferTextureInfo {')+';'
    cache_header=read('src/video_core/texture_cache/texture_cache_base.h')
    ticks=re.search(r'static constexpr size_t TICKS_TO_DESTROY = (\d+);',cache_header).group(1)
    owner=read(owner_fixture).replace('@INFO@',info).replace('@TICKS@',ticks).replace('@ACCELERATE@',
        block(raster,'std::optional<FramebufferTextureInfo> RasterizerVulkan::AccelerateDisplay('))
    ring=read('src/video_core/delayed_destruction_ring.h')
    (isolated/'delayed_destruction_ring.h').write_text(ring,encoding='utf-8')
    owner_exe=build('open-owner-witness',owner)
    for i in range(a.repeat):run(owner_exe,f'open-owner-{i}','owner',91,'CPU ring age retired accelerated native source')
    changed_ring=ring.replace('        elements[index].clear();','        // Control: keep retired owners alive during the observation.')
    assert changed_ring!=ring
    (isolated/'delayed_destruction_ring.h').write_text(changed_ring,encoding='utf-8')
    held=build('owner-held-control',owner)
    for i in range(a.repeat):run(held,f'owner-held-{i}','owner-held')
    (isolated/'delayed_destruction_ring.h').write_text(ring,encoding='utf-8')
    print('HISTORICAL pre-fix witness: accelerated bare handles outlive CPU ring retirement',flush=True)
(out/'extraction.json').write_text(json.dumps({
    'baseline':a.baseline,'revision':a.revision,'repetitions':a.repeat,
    'cases':[] if a.baseline else cases,'failures':[] if a.baseline else failures,
    'controls':[] if a.baseline else [c[0] for c in controls],
    'historical_owner_witness':a.baseline,
    'sha256':{path:hashlib.sha256(subprocess.check_output(['git','show',a.revision+':'+path],cwd=root)
        if a.revision and path.startswith('src/') else (root/path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'actual':['PresentManager GetRenderFrame/Present/WaitPresent/PresentThread/ReleaseRenderFrame/Shutdown',
              'Fence Wait/Reset bodies and TerminalFailStop implementation',
              'Owner witness runs only for pinned pre-fix baseline; current handoff has its own source suite'],
    'fixtures':['Native fence dispatch/results, frame resource probes, scheduler recording drain and copy-to-swapchain',
                'cache view/native handles and destruction probes; no actual driver or native completion'],
    'boundary':'Checked presentation-frame reuse failure, not a completed accelerated image owner handoff.'
},indent=2),encoding='utf-8')
