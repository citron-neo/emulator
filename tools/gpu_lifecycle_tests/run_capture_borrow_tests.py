"""Extracted capture/helper methods: stack/native borrows must not unwind uncertain work."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='ce2007b440cad4677882a35382f4fec1b8cd30de',help='Pinned pre-fix source for baseline/candidate only')
a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-capture-borrow').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_capture_borrow_tests.py'}
def data(n):
    return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8').replace('\r\n','\n')
def block(t,m):
    start=t.index(m);end=t.index('{',start)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
renderer=read('src/video_core/renderer_vulkan/renderer_vulkan.cpp')
renderer_header=read('src/video_core/renderer_vulkan/renderer_vulkan.h')
util=read('src/video_core/renderer_vulkan/present/util.cpp')
scheduler=read('src/video_core/renderer_vulkan/vk_scheduler.cpp')
read('src/video_core/renderer_vulkan/vk_scheduler.h')
read('src/video_core/renderer_vulkan/vk_present_manager.h')
methods='\n'.join(block(renderer,m) for m in ['vk::Buffer RendererVulkan::RenderToBuffer(', 'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer('])
upload=block(util,'void UploadImage(')
candidate=read('tools/gpu_lifecycle_tests/capture_borrow_candidate.json')
c=json.loads(candidate)
if a.candidate:
    for old,new in c['replacements']:
        assert old in methods+upload,old
        methods=methods.replace(old,new);upload=upload.replace(old,new)
    checked=c['checked_methods']
elif a.baseline:checked='void Scheduler::RequireNativeBorrowAdmission()const{}\nvoid Scheduler::FinishNativeBorrow(){Finish();WaitWorker();}'
else:checked='\n'.join(block(scheduler,m) for m in ['void Scheduler::RequireNativeBorrowAdmission(', 'void Scheduler::FinishNativeBorrow('])
checked+='\n'+block(scheduler,'void Scheduler::Finish(')
fixture=read('tools/gpu_lifecycle_tests/capture_borrow_fixture.cpp.in')
state=block(renderer_header,'    enum class AppletCaptureState')+';\n    AppletCaptureState applet_capture_state{AppletCaptureState::Empty};' if 'enum class AppletCaptureState' in renderer_header else ''
source=fixture.replace('@CHECKED_METHODS@',checked).replace('@CAPTURE_METHODS@',methods).replace('@UPLOAD_METHOD@',upload).replace('@CONTENT_STATE@',state).replace('@INIT_CONTENT_STATE@','applet_capture_state=mode=="empty"?AppletCaptureState::Empty:AppletCaptureState::Recorded;' if state else '')
failure=out/'terminal.cpp';failure.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+'.exe' if os.name=='nt' else name)
    r=subprocess.run(flags+[str(cpp),str(failure),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    if r.returncode:raise RuntimeError(r.stderr)
    return exe
def run(exe,label,path,mode,code,marker):
    r=subprocess.run([str(exe),path,mode],capture_output=True,text=True,timeout=15);log=r.stdout+r.stderr
    (out/(label+'.log')).write_text(log,encoding='utf-8')
    assert r.returncode==code and marker in log,(label,r.returncode,log)
    if code==86:
        assert 'atexit' not in log and 'EVENT buffer.destroy\n' not in log and 'EVENT image.destroy\n' not in log,(label,log)
        after_borrow=log.split('EVENT borrow.begin')[-1] if 'EVENT borrow.begin' in log else ''
        assert '.destroy' not in after_borrow,(label,log)
exe=build('production',source)
paths=['capture','applet','upload'];failures=['outside','record','callback','finish-before','finish-after','worker','poll-pending','poll-error','fallback-reset','closed','vk-finish','vk-record','record-active','context']
if a.baseline:
    for path in paths:
        for mode in ['record','finish-before','finish-after','record-active']:
            run(exe,path+'-'+mode,path,mode,91,'FAIL ')
    for mode in ['draw','internal-finish']:run(exe,'capture-'+mode,'capture',mode,91,'FAIL ')
    print('PASS historical local-owner unwind witnesses',flush=True)
else:
    for i in range(a.repeat):
        for path in paths:
            run(exe,f'{path}-normal-{i}',path,'normal',0,'PASS ')
            for mode in failures:
                stage={'capture':'capture.render','applet':'capture.applet','upload':'helper.upload'}[path]
                marker='stage=native_borrow.entry reason=closed_admission' if mode=='closed' else 'stage=native_borrow.completion reason=vulkan_result' if mode in ['poll-pending','poll-error','fallback-reset'] else 'stage='+stage+' reason='+('vulkan_exception' if mode.startswith('vk-') else 'host_exception')
                run(exe,f'{path}-{mode}-{i}',path,mode,86,marker)
        for mode in ['draw','internal-finish']:run(exe,f'capture-{mode}-{i}','capture',mode,86,'stage=capture.render reason=host_exception')
        for path in ['capture','applet']:run(exe,f'{path}-invalidate-{i}',path,'invalidate',0,'PASS ')
        run(exe,f'upload-flush-{i}','upload','flush',0,'PASS ')
        for path in paths:run(exe,f'{path}-allocate-{i}',path,'buffer-create',0,'PASS ')
        run(exe,f'capture-frame-create-{i}','capture','framebuffer-create',0,'PASS ')
        run(exe,f'applet-empty-{i}','applet','empty',0,'PASS ')
    controls=[('omit-capture-guard','capture','record', 'capture.render'),('omit-applet-guard','applet','record','capture.applet'),('omit-upload-guard','upload','record','helper.upload')]
    # Removing a catch must expose the exact old lifetime witness, not a build failure.
    for name,path,mode,stage in controls:
        changed=source.replace('Common::TerminalFailStop("'+stage+'", "host_exception");','throw;')
        assert changed!=source
        run(build(name,changed),name,path,mode,91,'FAIL ')
    changed=source.replace('const auto result = master_semaphore->PollNativeResourceLifetime(tick);','const auto result = VK_SUCCESS;\n    (void)tick;')
    assert changed!=source
    run(build('omit-native-check',changed),'omit-native-check','capture','poll-pending',91,'FAIL ')
    changed=source.replace('scheduler.RequireNativeBorrowAdmission();','/* no entry check */').replace('    RequireNativeBorrowAdmission();','    /* no recheck */')
    run(build('omit-admission',changed),'omit-admission','capture','closed',91,'FAIL closed admission accepted')
    changed=source.replace('    WaitWorker();','    /* no worker drain */')
    run(build('omit-worker-drain',changed),'omit-worker-drain','capture','normal',91,'FAIL normal synchronous semantics')
    changed=source.replace('const auto tick = CurrentTick();','const auto tick = CurrentTick()+1;')
    run(build('wrong-completion-tick',changed),'wrong-completion-tick','capture','normal',91,'FAIL wrong native completion tick')
    print('PASS capture/helper positive, terminal and exact controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'paths':paths,'terminal_cases':failures,'capture_extra':['draw','internal-finish'],'controls':[] if a.baseline else ['omit-capture-guard','omit-applet-guard','omit-upload-guard','omit-native-check','omit-admission','omit-worker-drain','wrong-completion-tick'],'sha256':{n:hashlib.sha256(data(n)).hexdigest() for n in sorted(inputs)},'boundary':'Actual RenderToBuffer/GetAppletCaptureBuffer/UploadImage and checked scheduler adapter, fixture allocation/driver/recording; no GPU/game or full helper lifetime proof.'},indent=2)+'\n')
