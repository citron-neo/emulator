"""Actual Layer post-upload quarantine and FXAA/FSR/FSR2 draw/initialization protocol."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path);p.add_argument('--baseline',action='store_true');p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='e96ea256378eadff48775cad73e1f6098b1ab370');a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-post-upload').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_post_upload_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8-sig').replace('\r\n','\n')
def block(s,m):
 start=s.index(m);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
s=read('src/video_core/renderer_vulkan/present/layer.cpp');h=read('src/video_core/renderer_vulkan/present/layer.h');c=json.loads(read('tools/gpu_lifecycle_tests/post_upload_candidate.json'))
if a.candidate:
 for old,new in c['replacements']:assert s.count(old)==1;s=s.replace(old,new)
 for old,new in c['header_replacements']:assert h.count(old)==1;h=h.replace(old,new)
helpers=[]
for cls in ['FXAA','FSR','FSR2']:
 t=read('src/video_core/renderer_vulkan/present/'+cls.lower()+'.cpp');read('src/video_core/renderer_vulkan/present/'+cls.lower()+'.h')
 draw=block(t,('void ' if cls=='FXAA' else 'VkImageView ')+cls+'::Draw(')
 pos=draw.index('{')+1;tag='aa' if cls=='FXAA' else cls.lower();draw=draw[:pos]+'\n    stage="'+tag+'";'+draw[pos:]
 helpers.extend([block(t,'void '+cls+'::UploadImages('),draw])
# Audited shared temporal/static state, not executed as these helper kinds here.
for n in ['taa','smaa']:read('src/video_core/renderer_vulkan/present/'+n+'.cpp');read('src/video_core/renderer_vulkan/present/'+n+'.h')
read('src/video_core/vulkan_common/vulkan_wrapper.h')
configure=block(s,'bool Layer::ConfigureDraw(')
configure=configure.replace('    if (!use_accelerated) {','    if (!use_accelerated) {\n        stage="raw";')
# Synthetic host-boundary injections; actual vkUpdateDescriptorSets is noexcept void.
configure=configure.replace('UpdateDescriptorSet(source_image_view, sampler, image_index);','StageFault("descriptor-before");\n        UpdateDescriptorSet(source_image_view, sampler, image_index);\n        StageFault("descriptor-after");')
methods='\n'.join(block(s,m) for m in ['bool Layer::UpdateRawImage(', 'void Layer::UpdateDescriptorSet(', 'void Layer::RefreshResources(', 'void Layer::WaitForResourceUses(', 'u64 Layer::GetRawImageOffset(', 'Layer::~Layer()', 'void Layer::ReleaseRawImages(', 'void Layer::ClearRawImages('])+ '\n'+configure
renderer=read('src/video_core/renderer_vulkan/renderer_vulkan.cpp');renderer_h=read('src/video_core/renderer_vulkan/renderer_vulkan.h')
applet=block(renderer,'void RendererVulkan::RenderAppletCaptureLayer(')
get=block(renderer,'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer(')
get=get[:get.index('    scheduler.RequireNativeBorrowAdmission();')]+'    return {0x5a};\n}'
source=read('tools/gpu_lifecycle_tests/post_upload_fixture.cpp.in')
source=source.replace('@APPLET_STATE@',block(renderer_h,'    enum class AppletCaptureState')+';').replace('@APPLET_METHODS@',applet+'\n'+get)
for k,v in {'HELPERS':'\n'.join(helpers),'METHODS':methods,'STATE':block(h,'    enum class RawUploadState')+';','BUNDLE':(block(h,'    struct RawResources')+';').replace('vk::ImageView','vk::View')}.items():source=source.replace('@'+k+'@',v)
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
 cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
 r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if r.returncode:raise RuntimeError(r.stderr)
 return exe
def run(exe,name,case,helper,slot,code=0,marker='PASS post upload'):
 r=subprocess.run([str(exe),case,helper,str(slot)],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
 assert r.returncode==code and marker in log,(name,r.returncode,log)
 if code==86:assert not any(x in log for x in ['.destroy','owners.released','atexit']),(name,log)
exe=build('production',source);controls=[];matrix={}
for helper in ['aa','fsr','fsr2']:
 stages=['aa']+([] if helper=='aa' else [helper])
 cases=['applet-helper','applet-descriptor','normal','repeat','warm-normal','gate-owner','gate-descriptor','rebuild','new-layer','shutdown','warm-shutdown','shutdown-active']
 cases+=['descriptor-before','descriptor-after','matrix','vertex','retain']
 cases += [tag+'-'+f for tag in stages for f in ['record-before','record-after','draw-enqueued','descriptor','finish-before','finish-partial','finish-after']]
 cases += ['warm-'+tag+'-'+f for tag in stages for f in ['draw-enqueued','descriptor']]
 terminals=['closed','idle-failure','retirement-failure','shutdown-active-idle-failure'];matrix[helper]={'cases':cases,'terminal_cases':terminals}
 if a.baseline:
  for i in range(a.repeat):
   for slot in range(3):
    for case in [helper+'-draw-enqueued','warm-'+helper+'-draw-enqueued']:run(exe,f'{helper}-{case}-{slot}-{i}',case,helper,slot,91,'FAIL uncertain post-upload path was retried')
    run(exe,f'{helper}-outputs-{slot}-{i}','descriptor-after',helper,slot,91,'FAIL failed configure published outputs')
 else:
  for i in range(a.repeat):
   for slot in range(3):
    for case in cases:run(exe,f'{helper}-{case}-{slot}-{i}',case,helper,slot)
    for case in terminals:run(exe,f'{helper}-{case}-{slot}-{i}',case,helper,slot,86,'stage='+('native_borrow.entry' if case=='closed' else 'layer.resource_retirement' if case=='retirement-failure' else 'renderer.native_idle'))
if not a.baseline:
 def control(name,old,new,case,witness):
  assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
  for i in range(a.repeat):run(mutant,f'{name}-{i}',case,'fsr',1,91,'FAIL '+witness)
  controls.append(name)
 control('omit-entry-quarantine','if (post_upload_failed) return false;','','fsr-draw-enqueued','uncertain post-upload path was retried')
 control('omit-failure-publication','post_upload_failed = true;','post_upload_failed = false;','aa-draw-enqueued','uncertain post-upload path was retried')
 control('slot-only-quarantine','if (post_upload_failed) return false;','if (post_upload_failed && image_index==1) return false;','fsr-finish-partial','uncertain post-upload path was retried')
 control('raw-generation-resets-helper','raw_upload_states.swap(candidate.uploads);','raw_upload_states.swap(candidate.uploads);post_upload_failed=false;','rebuild','raw generation reset helper quarantine')
 control('drop-owner-in-catch','post_upload_failed = true;','post_upload_failed = true;anti_alias.reset();','fsr-draw-enqueued','helper released with outstanding work')
 control('publish-output-early','SetMatrixData(candidate_push_constants, layout);','SetMatrixData(*out_push_constants, layout);','descriptor-after','failed configure published outputs')
 control('omit-failure-tick','resource_ticks[image_index] = scheduler.CurrentTick();','','fsr-draw-enqueued','failed helper prefix lost latest tick')
 control('omit-applet-draw-refusal',applet[:applet.index('    // Filter')],applet[:applet.index('    // Filter')].replace('applet_capture_state == AppletCaptureState::Failed','false'),'applet-helper','healthy Layer reopened failed applet')
 control('omit-applet-readback-refusal',get,get.replace('applet_capture_state == AppletCaptureState::Failed','false'),'applet-descriptor','healthy Layer reopened failed applet')
 control('extra-normal-finish','*out_descriptor_set = descriptor_sets[image_index];','*out_descriptor_set = descriptor_sets[image_index];scheduler.FinishNativeBorrow();','normal','normal configure added submission')
print('PASS post-upload '+('27 historical witnesses' if a.baseline else 'helper/descriptor paths and ten controls'),flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'slots':3,'matrix':matrix,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest() for n in sorted(inputs)},'actual':['Complete ConfigureDraw, UpdateRawImage callback, UpdateDescriptorSet, refresh/wait/release/destructor and raw declarations','Complete FXAA/FSR/FSR2 Draw and UploadImages bodies','Complete applet RenderAppletCaptureLayer and actual GetAppletCaptureBuffer refusal prefix'],'instrumentation':['Stage tags at helper Draw and raw call; synthetic host throws before/after Layer descriptor invocation','Helper descriptor allocator boundaries modeled; real semaphore command/descriptor and worker join gates'],'fixtures':['Helper native allocations/descriptor sets, helper descriptor vectors, native queue/commands/pixels, source copy/unswizzle, matrix/vertex math','Applet allocation/blit/readback success tail are collaborators; full readback checked separately','TAA/SMAA source and shared state audited, not executed; no Vulkan/device/game completion proof'],'boundary':'Sticky Layer helper quarantine retains owners and refuses retries across slots/generations. Vulkan descriptor update is noexcept void; no recoverable native failure receipt is claimed.'},indent=2)+'\n')
