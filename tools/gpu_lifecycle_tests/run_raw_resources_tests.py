"""Layer raw resources: complete candidate construction, no-throw publication and retry."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true')
p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='3ca2af23f61d2883002087092bfd508389b5df4f')
a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-raw-resources').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_raw_resources_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8').replace('\r\n','\n')
def block(t,m):
    start=t.index(m);end=t.index('{',start)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
layer=read('src/video_core/renderer_vulkan/present/layer.cpp');header=read('src/video_core/renderer_vulkan/present/layer.h');c=json.loads(read('tools/gpu_lifecycle_tests/raw_resources_candidate.json'))
if a.candidate:
    for old,new in c['replacements']:assert layer.count(old)==1;layer=layer.replace(old,new)
    bundle=c['bundle']
elif a.baseline:bundle=''
else:bundle=block(header,'    struct RawResources {')+';'
names=['Layer::~Layer()', 'void Layer::RefreshResources(', 'void Layer::WaitForResourceUses(', 'void Layer::ReleaseRawImages(', 'void Layer::ClearRawImages(', 'u64 Layer::CalculateBufferSize(', 'u64 Layer::GetRawImageOffset(']
names+=['void Layer::CreateStagingBuffer(', 'void Layer::CreateRawImages('] if a.baseline else ['vk::Buffer Layer::CreateStagingBuffer(', 'Layer::RawResources Layer::CreateRawImages(']
methods='\n'.join(block(layer,m) for m in names)
decls='void CreateStagingBuffer(const Tegra::FramebufferConfig&);void CreateRawImages(const Tegra::FramebufferConfig&);' if a.baseline else 'vk::Buffer CreateStagingBuffer(const Tegra::FramebufferConfig&);RawResources CreateRawImages(const Tegra::FramebufferConfig&);'
source=read('tools/gpu_lifecycle_tests/raw_resources_fixture.cpp.in').replace('@BUNDLE@',bundle).replace('@FACTORY_DECLS@',decls).replace('@METHODS@',methods)
state=block(header,'    enum class RawUploadState')+';' if 'enum class RawUploadState' in header else ''
source=source.replace('@UPLOAD_STATE@',state).replace('@UPLOAD_MEMBER@','std::vector<RawUploadState> raw_upload_states;' if state else '')
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
    if r.returncode:raise RuntimeError(r.stderr)
    return exe
def run(exe,name,phase,case,code=0,marker='PASS '):
    r=subprocess.run([str(exe),phase,case],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr
    (out/(name+'.log')).write_text(log,encoding='utf-8')
    assert r.returncode==code and marker in log,(name,r.returncode,log)
    if code==86:
        tail=log.split('EVENT shutdown.begin')[-1]
        assert '.destroy' not in tail and 'atexit' not in log,(name,log)
exe=build('production',source)
stages=['buffer','image0','view0','image1','view1','image2','view2']
cases=['normal','sequence','stride','unchanged']+stages+['host-'+x for x in stages]+['gate-'+x for x in stages]+['retry-'+x for x in stages]+['shutdown-view2']
controls=[]
if a.baseline:
    for i in range(a.repeat):
        for phase in ['cold','warm']:
            for case in stages:run(exe,f'{phase}-{case}-{i}',phase,case,91,'FAIL failed creation changed published resources')
        run(exe,f'retry-{i}','cold','retry-view1',91,'FAIL retry used incomplete raw resources')
        run(exe,f'stride-{i}','warm','stride',91,'FAIL stride update reused undersized staging')
    print('PASS sixteen historical raw publication/retry/stride witnesses',flush=True)
else:
    for i in range(a.repeat):
        for phase in ['cold','warm']:
            for case in cases:run(exe,f'{phase}-{case}-{i}',phase,case)
            run(exe,f'{phase}-closed-{i}',phase,'closed',86,'stage=native_borrow.entry')
        run(exe,f'wait-{i}','warm','wait-failure',86,'stage=layer.resource_retirement')
        run(exe,f'idle-{i}','warm','shutdown-failed-idle',86,'stage=renderer.native_idle')
    def control(name,old,new,phase,case,witness):
        assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
        for i in range(a.repeat):run(mutant,f'{name}-{i}',phase,case,91,'FAIL '+witness)
        controls.append(name)
    control('metadata-early','auto candidate = CreateRawImages(framebuffer);','raw_width = framebuffer.width;auto candidate = CreateRawImages(framebuffer);','warm','image1','failed creation changed published resources')
    control('construct-images-in-member','candidate.images.resize(image_count);','raw_images.resize(image_count);candidate.images.resize(image_count);','cold','view1','failed creation changed published resources')
    control('omit-views-commit','raw_image_views.swap(candidate.views);','','cold','normal','retry used incomplete raw resources')
    control('omit-old-use-wait','    WaitForResourceUses();','','warm','normal','native released with unfinished uses')
    control('omit-stride-key','framebuffer.stride == raw_stride && ','','warm','stride','stride update reused undersized staging')
    control('inherit-old-ticks','resource_ticks.swap(candidate.ticks);','if(initializing) resource_ticks.swap(candidate.ticks);','warm','normal','new slots inherited old ticks')
    print('PASS raw resource creation, gates, shutdown and six controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'cases':cases,'phases':['cold','warm'],'terminal_cases':['cold-closed','warm-closed','warm-wait-failure','warm-shutdown-failed-idle'],'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest() for n in sorted(inputs)},'actual':['Layer RefreshResources/CreateStagingBuffer/CreateRawImages, raw bundle, wait/clear/release/destructor, size/offset methods'],'fixtures':['Native allocation and child-first destruction probes, Scheduler checked wait/closure/idle and pending users','Real semaphore factory gates and mapped staging bytes; no actual Vulkan/guest memory'],'boundary':'Serialized raw-resource tuple construction/publication/retry only; no guest coherency or device/game acceptance.'},indent=2)+'\n')
