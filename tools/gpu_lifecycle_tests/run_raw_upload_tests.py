"""Extracted Layer raw upload/ConfigureDraw: recording eligibility, retries and shutdown."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='3165ece04dccac87feaff78eeeed1e2e767ae3fd')
a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-raw-upload').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_raw_upload_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8-sig').replace('\r\n','\n')
def block(t,m):
    start=t.index(m);end=t.index('{',start)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
s=read('src/video_core/renderer_vulkan/present/layer.cpp');h=read('src/video_core/renderer_vulkan/present/layer.h')
c=json.loads(read('tools/gpu_lifecycle_tests/raw_upload_candidate.json'))
if a.candidate:
    for old,new in c['replacements']:assert s.count(old)==1,old;s=s.replace(old,new)
    for old,new in c['header_replacements']:assert h.count(old)==1,old;h=h.replace(old,new)
state='' if a.baseline else block(h,'    enum class RawUploadState')+';'
bundle=block(h,'    struct RawResources')+';'
# vk::ImageView is a controlled native handle wrapper in this fixture.
bundle=bundle.replace('vk::ImageView','vk::View')
configure=block(s,'bool Layer::ConfigureDraw(');configure_tail = configure[configure.rindex('    } catch (...) {'):] if 'post_upload_failed = true;' in configure else '}'
configure = configure[:configure.index('    auto crop_rect =')] + '\n    return true;\n' + configure_tail
methods='\n'.join(block(s,m) for m in ['bool Layer::UpdateRawImage(', 'u64 Layer::GetRawImageOffset(', 'void Layer::RefreshResources(', 'void Layer::WaitForResourceUses(', 'Layer::~Layer()', 'void Layer::ReleaseRawImages(', 'void Layer::ClearRawImages('])+ '\n'+configure
source=read('tools/gpu_lifecycle_tests/raw_upload_fixture.cpp.in')
for k,v in {'STATE':state,'BUNDLE':bundle,'STATE_MEMBER':'' if a.baseline else 'std::vector<RawUploadState>raw_upload_states=std::vector<RawUploadState>(3);','NEW_STATES':'' if a.baseline else 'r.uploads.resize(3);','CHECK_RECORDING':'' if a.baseline else 'check(l.raw_upload_states[slot]==Vulkan::Layer::RawUploadState::Recording,"enqueue published Recorded too early");','METHODS':methods}.items():source=source.replace('@'+k+'@',v)
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    if r.returncode:raise RuntimeError(r.stderr)
    return exe
def run(exe,name,case,slot,code=0,marker='PASS raw upload'):
    r=subprocess.run([str(exe),case,str(slot)],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
    assert r.returncode==code and marker in log,(name,r.returncode,log)
    if code==86:assert not any(x in log for x in ['buffer.destroy','owners.released','atexit']),(name,log)
exe=build('production',source)
uncertain=['record-before','record-enqueued','submit-partial','submit-full','rebuild','shutdown','idle-failure']
cases=['normal','repeat','internal-finish','shutdown-active']+['pre-'+x for x in ['mapped','source-false','source-throw','unswizzle','flush']]+['gate-'+x for x in ['enqueue','barrier','copy','post']]+[prefix+x for prefix in ['','warm-'] for x in uncertain if x!='idle-failure']
terminal_cases=['closed-upload','shutdown-active-idle-failure','rebuild-wait-failure','closed','worker-barrier','worker-copy','worker-post','idle-failure','warm-idle-failure']
controls=[]
if a.baseline:
    for i in range(a.repeat):
        for slot in range(3):
            for case in ['record-enqueued','submit-partial','warm-submit-full']:run(exe,f'{case}-{slot}-{i}',case,slot,91,'FAIL uncertain upload was retried')
    print('PASS nine historical uncertain-upload retry witnesses',flush=True)
else:
    for i in range(a.repeat):
        for slot in range(3):
            for case in cases:run(exe,f'{case}-{slot}-{i}',case,slot)
            for case in terminal_cases:
                stage=('native_borrow.entry' if case.startswith('closed') else 'layer.raw_upload.recording' if case.startswith('worker-') else 'layer.resource_retirement' if case=='rebuild-wait-failure' else 'renderer.native_idle')
                run(exe,f'{case}-{slot}-{i}',case,slot,86,'CITRON TERMINAL FAIL-STOP stage='+stage)
    def control(name,old,new,case,witness,code=91):
        assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
        for i in range(a.repeat):run(mutant,f'{name}-{i}',case,1,code,witness)
        controls.append(name)
    control('omit-failed','raw_upload_states[image_index] == RawUploadState::Failed','false','record-enqueued','FAIL uncertain upload was retried')
    control('omit-quarantine','raw_upload_states[image_index] = RawUploadState::Failed;','raw_upload_states[image_index] = RawUploadState::Recorded;','submit-partial','FAIL uncertain upload was retried')
    control('omit-state-generation','raw_upload_states.swap(candidate.uploads);','','rebuild','FAIL fresh resource generation remained quarantined')
    control('early-recorded','raw_upload_states[image_index] = RawUploadState::Recording;','raw_upload_states[image_index] = RawUploadState::Recorded;','gate-enqueue','FAIL enqueue published Recorded too early')
    control('enqueue-completion','jobs.emplace_back(std::forward<T>(f));','jobs.emplace_back(std::forward<T>(f));Drain();Submit();','normal','FAIL enqueue treated as native completion')
    control('omit-final-barrier','        write_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;','        write_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;','normal','FAIL sampled image without final upload barrier')
    control('omit-direct-admission','bool Layer::UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index) {\n    scheduler.RequireNativeBorrowAdmission();','bool Layer::UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index) {','closed-upload','FAIL closed raw upload admitted')
    control('omit-failure-tick','resource_ticks[image_index] = scheduler.CurrentTick();','','record-enqueued','FAIL failed prefix lost slot retirement tick')
    print('PASS raw upload slots, retries, gates, terminal boundaries and eight controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'slots':3,'cases':cases,'terminal_cases':terminal_cases,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest() for n in sorted(inputs)},'actual':['Complete UpdateRawImage callback, ConfigureDraw through AA Draw, RawUploadState and RawResources declarations','RefreshResources, WaitForResourceUses, GetRawImageOffset, destructor/release/clear and TerminalFailStop'],'fixtures':['Scheduler chunks/native queue/pixels, native resource handles, AA consumer, raw candidate factory, source copy/unswizzle/flush','Real semaphore gates and owner/worker threads; no real Vulkan or guest Memory'],'boundary':'Slot recording eligibility is not GPU completion; applet failed quarantine is validated separately.'},indent=2)+'\n')
