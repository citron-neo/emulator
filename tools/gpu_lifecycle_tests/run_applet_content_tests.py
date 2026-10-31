"""Applet failed-Draw readback/refusal and checked shutdown, extracted production bodies."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='f892213b0a26175d9ffa925d578447ccb7a78281')
a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-applet-content').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_applet_content_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8').replace('\r\n','\n')
def block(t,m):
    start=t.index(m);end=t.index('{',start)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
r=read('src/video_core/renderer_vulkan/renderer_vulkan.cpp');h=read('src/video_core/renderer_vulkan/renderer_vulkan.h')
c=json.loads(read('tools/gpu_lifecycle_tests/applet_content_candidate.json'))
if a.candidate:
    for old,new in c['replacements']:assert r.count(old)==1,old;r=r.replace(old,new)
    state=c['state']
elif a.baseline:state=''
else:
    state=block(h,'    enum class AppletCaptureState')+';\n    AppletCaptureState applet_capture_state{AppletCaptureState::Empty};'
renderer='\n'.join(block(r,m) for m in ['void RendererVulkan::RenderAppletCaptureLayer(', 'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer(', 'void RendererVulkan::Shutdown()'])
s=read('src/video_core/renderer_vulkan/vk_scheduler.cpp')
scheduler_header=read('src/video_core/renderer_vulkan/vk_scheduler.h')
layer=read('src/video_core/renderer_vulkan/present/layer.cpp')
read('src/video_core/renderer_vulkan/present/layer.h')
if a.candidate:
    for old,new in c['scheduler_replacements']:assert s.count(old)==1;s=s.replace(old,new)
    s+='\n'+c['release_method']
    old,new=c['layer_replacement'];assert layer.count(old)==1;layer=layer.replace(old,new)
release=block(s,'void Scheduler::WaitForResourceRelease(') if not a.baseline else 'void Scheduler::WaitForResourceRelease(u64 tick){Wait(tick);}'
layer_methods='\n'.join(block(layer,m) for m in ['Layer::~Layer()', 'void Layer::ReleaseRawImages(', 'void Layer::ClearRawImages('])
scheduler='\n'.join(block(s,m) for m in ['void Scheduler::Finish(', 'void Scheduler::RequireNativeBorrowAdmission(', 'void Scheduler::FinishNativeBorrow('])
scheduler+='\n'+block(scheduler_header,'    void Wait(u64 tick)').replace('void Wait(', 'void Scheduler::Wait(',1)+'\n'+release+'\n'+block(s,'bool Scheduler::RetirePresentationSourcesAfterDeviceIdle(')
frame=block(read('src/video_core/renderer_vulkan/vk_present_manager.h'),'struct Frame {')+';'
source=read('tools/gpu_lifecycle_tests/applet_content_fixture.cpp.in').replace('@STATE@',state).replace('@FRAME@',frame).replace('@SCHEDULER@',scheduler).replace('@RENDERER@',renderer).replace('@LAYER@',layer_methods)
source=source.replace('@CHECK_CLOSED@','check(r.applet_capture_state==Vulkan::RendererVulkan::AppletCaptureState::Closed,"capture not closed before drains");' if not a.baseline else '')
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    child=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(child.stdout+child.stderr,encoding='utf-8')
    if child.returncode:raise RuntimeError(child.stderr)
    return exe
def run(exe,name,case,code=0,witness='PASS '):
    child=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20);log=child.stdout+child.stderr
    (out/(name+'.log')).write_text(log,encoding='utf-8')
    assert child.returncode==code and witness in log,(name,child.returncode,log)
    if code==86:assert not any(x in log for x in ['image.destroy','view.destroy','framebuffer.destroy','buffer.destroy','atexit']),(name,log)
exe=build('production',source)
cases=['normal','repeat','empty','creation-retry','recording-observer','failed-before','failed-record','failed-submit','failed-split','failed-full','retry','completed-prefix-retry','good-then-fail','native-gate','shutdown-record','shutdown-submit','shutdown-split','shutdown-idle-gate','read-buffer-fail','read-invalidate-fail','release-after-shutdown']
terminals=[('read-submit-fail','capture.applet'),('read-wait-fail','capture.applet'),('read-host-fail','capture.applet'),('read-query-fail','native_borrow.completion'),('read-pending','native_borrow.completion'),('read-callback-fail','capture.applet'),('shutdown-worker-fail','renderer.recording_drain'),('shutdown-idle-fail','renderer.native_idle'),('release-before-idle','native_borrow.entry')]
controls=[]
if a.baseline:
    for i in range(a.repeat):
        for case in ['failed-before','failed-record','failed-submit','failed-split','failed-full','good-then-fail','retry','completed-prefix-retry','recording-observer','release-after-shutdown']:
            witness='resource release submitted after closure' if case=='release-after-shutdown' else 'failed Draw replayed on retry' if case=='retry' else 'recording image exposed readback' if case=='recording-observer' else 'failed Draw exposed readback'
            run(exe,f'{case}-{i}',case,91,'FAIL '+witness)
    print('PASS ten old applet Draw/readback/replay/release witnesses',flush=True)
else:
    for i in range(a.repeat):
        for case in cases:run(exe,f'{case}-{i}',case)
        for case,stage in terminals:run(exe,f'{case}-{i}',case,86,'stage='+stage)
    def control(name,old,new,case,witness):
        assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
        for i in range(a.repeat):run(mutant,f'{name}-{i}',case,91,'FAIL '+witness)
        controls.append(name)
    control('failed-to-recorded','applet_capture_state = AppletCaptureState::Failed;','applet_capture_state = AppletCaptureState::Recorded;','failed-submit','failed Draw exposed readback')
    # Remove both guards for Failed without changing the independent Recording/Closed checks.
    changed=source.replace('applet_capture_state == AppletCaptureState::Failed ||','false ||')
    mutant=build('retry-failed',changed)
    for i in range(a.repeat):run(mutant,f'retry-failed-{i}','retry',91,'FAIL failed Draw replayed on retry')
    controls.append('retry-failed')
    control('publish-recorded-before-draw','applet_capture_state = AppletCaptureState::Recording;','applet_capture_state = AppletCaptureState::Recorded;','recording-observer','recording image exposed readback')
    control('omit-close','applet_capture_state = AppletCaptureState::Closed;','/* no close */','shutdown-submit','capture not closed before drains')
    control('omit-readback-finish','        scheduler.FinishNativeBorrow();','        /* no finish */','normal','invalidate before checked completion')
    control('late-release-wait','scheduler.WaitForResourceRelease(tick);','scheduler.Wait(tick);','failed-record','resource release submitted after closure')
    print('PASS twenty-one content paths, nine terminal paths and six exact controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'cases':cases,'terminal_cases':terminals,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest() for n in sorted(inputs)},'actual':['RenderAppletCaptureLayer/GetAppletCaptureBuffer/Shutdown and Frame declaration','Scheduler Wait/Finish/native-borrow/release and terminal source retirement methods, Layer destructor/release/clear, TerminalFailStop'],'fixtures':['Draw/preparation and recording/native command model with partial/full bytes and staging effects','Vulkan resource probes, host/native completion and shutdown collaborators; real semaphore waits'],'boundary':'Serialized applet recording eligibility, sticky failed-prefix quarantine and checked readback/teardown; no real Vulkan/game, visibility or arbitrary producer admission proof.'},indent=2)+'\n')
