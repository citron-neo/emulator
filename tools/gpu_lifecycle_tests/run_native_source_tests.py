"""Native cache source ownership through actual Scheduler submission and completion."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
a=p.parse_args()
if a.repeat<1:p.error('positive repeat')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-native-source').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_native_source_tests.py'}
def read(path):inputs.add(path);return (root/path).read_text(encoding='utf-8').replace('\r\n','\n')
def block(text,marker):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
scheduler=read('src/video_core/renderer_vulkan/vk_scheduler.cpp');scheduler_h=read('src/video_core/renderer_vulkan/vk_scheduler.h')
texture=read('src/video_core/renderer_vulkan/vk_texture_cache.h')
texture_cpp=read('src/video_core/renderer_vulkan/vk_texture_cache.cpp')
view_capture=re.search(r'physical_image_type\{image.info.type\} \{(.*?)    using Shader::TextureType;',texture_cpp,re.S).group(1)
view_store=next(line for line in texture_cpp.splitlines() if 'image_views[static_cast<size_t>(tex_type)] =' in line)
null_ctor=texture_cpp[texture_cpp.index('ImageView::ImageView(TextureCacheRuntime& runtime, const VideoCommon::NullImageViewParams&'):texture_cpp.index('ImageView::~ImageView()')]
null_capture=null_ctor[null_ctor.index('    image_owner = null_image;'):null_ctor.index('    for (u32')]
layer=block(read('src/video_core/renderer_vulkan/present/layer.cpp'),'bool Layer::ConfigureDraw(')
layer_tail = layer[layer.rindex('    } catch (...) {'):] if 'post_upload_failed = true;' in layer else '}'
layer = layer[:layer.index('    auto crop_rect =')] + '\n    return true;\n' + layer_tail
methods='\n'.join(block(scheduler,m) for m in ['void Scheduler::RequireNativeBorrowAdmission(', 'void Scheduler::RetainPresentationSource(',
    'void Scheduler::TrackPresentationSources(', 'void Scheduler::CollectPresentationSources(',
    'bool Scheduler::RetirePresentationSourcesAfterDeviceIdle(', 'u64 Scheduler::SubmitExecution('])
master=read('src/video_core/renderer_vulkan/vk_master_semaphore.h')
present=read('src/video_core/renderer_vulkan/vk_present_manager.cpp')
fixture='tools/gpu_lifecycle_tests/native_source_fixture.cpp.in';source=read(fixture)
parts={'METHODS':methods,'RECORD':'template<class T>\n'+block(scheduler_h,'    void RecordWithUploadBuffer('),
       'BEGIN':block(scheduler,'NativeSourceRetainer<Scheduler::Submission>::Scope Scheduler::BeginPresentationSources('),
       'FINISH':block(scheduler,'void Scheduler::Finish('),
       'DESTRUCTOR':block(scheduler,'Scheduler::~Scheduler('),
       'PACKET':block(read('src/video_core/renderer_vulkan/vk_presentation_source.h'),'struct PresentationSource {')+';',
       'RETAIN_IMAGE':block(texture,'    std::shared_ptr<const vk::Image> RetainCurrentImage('),
       'RETAIN_VIEW':block(texture,'    std::shared_ptr<const PresentationSource> RetainPresentationSource('),
       'VIEW_CAPTURE':view_capture,'VIEW_STORE':view_store,'NULL_CAPTURE':null_capture,
       'INFO':block(read('src/video_core/renderer_vulkan/vk_blit_screen.h'),'struct FramebufferTextureInfo {')+';',
       'ACCELERATE':block(read('src/video_core/renderer_vulkan/vk_rasterizer.cpp'),'std::optional<FramebufferTextureInfo> RasterizerVulkan::AccelerateDisplay('),
       'POLL_NATIVE':block(master,'    [[nodiscard]] VkResult PollNativeResourceLifetime('),
       'POLL_TIMELINE':block(master,'    [[nodiscard]] VkResult PollTimeline('),
       'LAYER':layer,'GET_FRAME':block(present,'Frame* PresentManager::GetRenderFrame('),
       'RELEASE_FRAME':block(present,'void PresentManager::ReleaseRenderFrame(')}
for key,value in parts.items():source=source.replace('@'+key+'@',value)
failure=out/'terminal-fail-stop.cpp';failure.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8')
read('src/common/terminal_fail_stop.h');read('src/video_core/renderer_vulkan/vk_native_source_retainer.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-I',str(root/'src')]
def build(name,text):
    cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
    result=subprocess.run(flags+[str(cpp),str(failure),'-o',str(exe)],capture_output=True,text=True)
    (out/(name+'-compile.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(result.stderr)
    return exe
def run(exe,name,case,witness=None):
    result=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20)
    log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
    assert ((result.returncode==91 and 'FAIL '+witness in log) if witness else
            (result.returncode==0 and 'PASS native source '+case in log)),(name,result.returncode,log)
exe=build('production',source)
cases=['timeline','fallback','record-delay','submit-delay','native-delay','internal-flush','inter-layer-flush',
       'null-image',
       'preparation-finish','completed-on-next-begin',
       'fallback-internal-flush','fallback-failure','fallback-reset-failure',
       'scaled','presentation','queue-failure','query-failure','wait-failure','wrong-queue','missing-attempt',
       'host-throw','record-throw','shutdown-pending','shutdown-worker','idle-active','unsubmitted']
for i in range(a.repeat):
    for case in cases:run(exe,f'{case}-{i}',case)
print('PASS twenty-six native source paths',flush=True)
terminal_cases=[('registration-failure','native_source.admission','host_exception'),
                ('partial-registration-failure','native_source.admission','host_exception'),
                ('closed-record','native_source.record_admission','closed_admission'),
                ('closed-scope','native_source.scope','closed_admission'),
                ('renewal-failure','native_source.record_admission','host_exception'),
                ('closed-admission','native_source.admission','closed_or_missing_owner'),
                ('shutdown-disposal','scheduler.native_source_disposal','unconfirmed_native_owners')]
for i in range(a.repeat):
    for case,stage,reason in terminal_cases:
        result=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20)
        log=result.stdout+result.stderr;(out/(case+f'-{i}.log')).write_text(log,encoding='utf-8')
        assert result.returncode==86 and f'stage={stage} reason={reason}' in log,(case,result.returncode,log)
        assert 'DESTROY native 11' not in log and 'DESTROY native 12' not in log,(case,log)
        if case=='partial-registration-failure':
            assert 'DESTROY native 31' not in log and 'DESTROY native 32' not in log,(case,log)
variants=[
 ('omit-null-image-pin',source.replace('    image_owner = null_image;',''),
  'null-image','image destroyed before native view'),
 ('end-scope-before-final-record',source.replace('if(mode=="inter-layer-flush")flush_complete();',
     'if(mode=="inter-layer-flush"){scope.reset();flush_complete();}'),
  'inter-layer-flush','native source destroyed before recording'),
 ('omit-cache-image-pin',source.replace('    image_owner = image.RetainCurrentImage();',''),
  'timeline','valid accelerated native source refused'),
 ('omit-cache-view-pin',source.replace(view_store,'        static_cast<void>(handle);'),
  'timeline','valid accelerated native source refused'),
 ('omit-owner-handoff',source.replace('if (texture_info) scheduler.RetainPresentationSource(texture_info->native_owner);',''),
  'record-delay','native source destroyed before recording'),
 ('omit-record-renewal',source.replace('        TrackPresentationSources();',''),
  'inter-layer-flush','native source destroyed before recording'),
 ('ignore-native-completion',source.replace('const auto complete = master_semaphore->PollNativeResourceLifetime(receipt->tick);','const auto complete = VK_SUCCESS;'),
  'native-delay','native source released before native completion'),
 ('ignore-queue-result',source.replace('result != VK_SUCCESS || !receipt->queue_attempted', 'false || !receipt->queue_attempted'),
  'queue-failure','uncertain submission released source'),
 ('omit-queue-identity',source.replace('receipt->queue != device.GetGraphicsQueue().Handle()', 'false'),
  'wrong-queue','uncertain submission released source'),
 ('omit-queue-attempt',source.replace('!receipt->queue_attempted.load(std::memory_order_acquire)', 'false'),
  'missing-attempt','uncertain submission released source'),
 ('wrong-scaled-parent',source.replace('if (scaled_image && **scaled_image == current_image) return scaled_image;', 'if (scaled_image) return original_image;'),
  'scaled','valid accelerated native source refused'),
 ('retire-before-closure',source.replace('    if (worker_thread.joinable() || submission_owner->load(std::memory_order_acquire)) return false;',''),
  'shutdown-pending','open scheduler authorized idle owner retirement'),
 ('retire-before-worker-join',source.replace('worker_thread.joinable() || ',''),
  'shutdown-worker','active worker authorized native source release'),
 ('ignore-fallback-failure',source.replace('if (failure != VK_SUCCESS) return failure;','if (false) return failure;'),
  'fallback-failure','uncertain submission released source'),
]
for name,changed,case,witness in variants:
    assert changed!=source,name
    mutant=build(name,changed)
    for i in range(a.repeat):run(mutant,f'{name}-{i}',case,witness)
    print('PASS native source control: '+name,flush=True)
protocol_cases=['normal','pending','split','unknown','idle-active','nested','record-throw']
protocol=build('candidate-protocol',read('tools/gpu_lifecycle_tests/native_source_protocol.cpp'))
for i in range(a.repeat):
    for case in protocol_cases:
        result=subprocess.run([str(protocol),case],capture_output=True,text=True,timeout=20)
        (out/(f'protocol-{case}-{i}.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0 and 'PASS native source protocol' in result.stdout,(case,result.stderr)
(out/'extraction.json').write_text(json.dumps({'repetitions':a.repeat,'cases':cases,'controls':[c[0] for c in variants],
    'terminal_cases':terminal_cases,'protocol_cases':protocol_cases,
    'sha256':{path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'actual':['PresentationSource, native retainer, Image/View retain functions, AccelerateDisplay and Layer source prefix',
              'Scheduler native registration/collection/idle retirement, RecordWithUploadBuffer and full SubmitExecution',
              'MasterSemaphore native/timeline poll, PresentManager GetRenderFrame/ReleaseRenderFrame'],
    'fixtures':['Native resource wrappers/destruction probes, cache metadata/epoch, command chunks/driver queue and native progress',
                'fallback acknowledgment is driven; separate actual WaitThread regression required; no Vulkan/game'],
    'boundary':'Native source image/view ownership only; no guest coherency, native output/capture full lifetime or admission authority.'},indent=2),encoding='utf-8')
