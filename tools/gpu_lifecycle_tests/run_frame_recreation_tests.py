"""Real Frame recreation and Blit publication: candidate tuple, metadata, slots and failure retry."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path);p.add_argument('--baseline',action='store_true');p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='7e8c2d1b8c8c4f014b17f460c239a6090862a097');a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-frame-recreation').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_frame_recreation_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8-sig').replace('\r\n','\n')
def block(s,m):
 start=s.index(m);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
b=read('src/video_core/renderer_vulkan/vk_blit_screen.cpp');h=read('src/video_core/renderer_vulkan/vk_blit_screen.h');present=read('src/video_core/renderer_vulkan/vk_present_manager.cpp');ph=read('src/video_core/renderer_vulkan/vk_present_manager.h');c=json.loads(read('tools/gpu_lifecycle_tests/frame_recreation_candidate.json'))
if a.candidate:
 for old,new in c['blit_replacements']:assert b.count(old)==1;b=b.replace(old,new)
 for old,new in c['present_replacements']:assert present.count(old)==1;present=present.replace(old,new)
 for old,new in c['header_replacements']:assert h.count(old)==1;h=h.replace(old,new)
 for old,new in c['present_header_replacements']:assert ph.count(old)==1;ph=ph.replace(old,new)
factory='void BlitScreen::SetWindowAdaptPass(' if a.baseline else 'std::unique_ptr<WindowAdaptPass> BlitScreen::CreateWindowAdaptPass('
decl='void SetWindowAdaptPass();' if a.baseline else 'std::unique_ptr<WindowAdaptPass>CreateWindowAdaptPass(Settings::ScalingFilter,VkFormat);'
blit='\n'.join(block(b,m)for m in [factory,'void BlitScreen::DrawToFrame(','void BlitScreen::WaitIdle()','vk::Framebuffer BlitScreen::CreateFramebuffer(const Layout::FramebufferLayout&','vk::Framebuffer BlitScreen::CreateFramebuffer(const VkImageView&'])
r=read('src/video_core/renderer_vulkan/renderer_vulkan.cpp');rh=read('src/video_core/renderer_vulkan/renderer_vulkan.h');
if a.candidate:
 for old,new in c['renderer_replacements']:assert r.count(old)==1;r=r.replace(old,new)
applet=block(r,'void RendererVulkan::RenderAppletCaptureLayer(');get=block(r,'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer(');get=get[:get.index('    scheduler.RequireNativeBorrowAdmission();')]+'    return {0x5a};\n}'
source=read('tools/gpu_lifecycle_tests/frame_recreation_fixture.cpp.in')
has_format='VkFormat image_view_format{' in ph
source=source.replace('@HAS_FORMAT@',str(has_format).lower()).replace('@FRAME_FORMAT_GET@','return f.image_view_format;' if has_format else '(void)f;return VK_FORMAT_UNDEFINED;')
for k,v in {'FRAME':block(ph,'struct Frame {')+';','RECREATE':block(present,'void PresentManager::RecreateFrame('),'BLIT':blit,'FACTORY_DECL':decl,'APPLET_STATE':block(rh,'    enum class AppletCaptureState')+';','APPLET':applet+'\n'+get}.items():source=source.replace('@'+k+'@',v)
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-Wno-missing-field-initializers','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
 cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
 r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if r.returncode:raise RuntimeError(r.stderr)
 return exe
def run(exe,name,mode,stage,code=0,marker='PASS recreate'):
 r=subprocess.run([str(exe),mode,stage],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
 assert r.returncode==code and marker in log,(name,r.returncode,log)
 if code==86:
  tail=log.rsplit('EVENT shutdown.joined',1)[-1] if mode=='failed-idle' else log
  assert '.destroy' not in tail and not any(x in log for x in ['owners.released','atexit']),(name,log)
exe=build('production',source);controls=[]
stages=['image','view','framebuffer'];cases=[('direct',s)for s in stages+['success']]+[('cold',s)for s in stages+['success']]+[(m,s)for m in ['warm','host','gate']for s in ['pass']+stages]+[('warm','success'),('shrink','success'),('unchanged','success'),('zero','success'),('framebuffer','pass'),('framebuffer','framebuffer'),('framebuffer-unchanged','framebuffer'),('shutdown','view'),('applet-creation','framebuffer'),('applet-failed','draw')]
cases += [('shrink',str(i)+'-'+str(n))for i in range(3)for n in [1,2]]+[('pool-format','success'),('pool-reverse','success')]
terminal_cases=[('closed','success','native_borrow.entry'),('closed-direct','success','native_borrow.entry'),('closed-framebuffer','success','native_borrow.entry'),('warm','idle','blit.native_idle'),('failed-idle','view','renderer.native_idle')]
if a.baseline:
 witnesses=[('direct','image','failed recreate changed publication'),('direct','view','parent destroyed with live children'),('direct','framebuffer','parent destroyed with live children'),('direct','success','parent destroyed with live children'),('warm','pass','failed update changed published state'),('warm','image','failed update changed published state'),('shrink','success','draw used retired slot'),('framebuffer','framebuffer','failed framebuffer changed publication')]
 for i in range(a.repeat):
  for mode,stage,w in witnesses:run(exe,f'{mode}-{stage}-{i}',mode,stage,91,'FAIL '+w)
 print('PASS eight historical Frame/Blit publication, parent order and shrink witnesses',flush=True)
else:
 for i in range(a.repeat):
  for mode,stage in cases:run(exe,f'{mode}-{stage}-{i}',mode,stage)
  for mode,stage,marker in terminal_cases:run(exe,f'{mode}-{stage}-{i}',mode,stage,86,'stage='+marker)
 def control(name,old,new,mode,stage,w):
  assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
  for i in range(a.repeat):run(mutant,f'{name}-{i}',mode,stage,91,'FAIL '+w)
  controls.append(name)
 control('early-frame-metadata','    auto& dld = device.GetLogical();\n    Frame candidate{};','    auto& dld = device.GetLogical();\n    Frame candidate{};frame->width=width;','direct','image','failed recreate changed publication')
 control('early-image-overwrite','    std::swap(frame->image, candidate.image);','    frame->image=std::move(candidate.image);','direct','success','parent destroyed with live children')
 control('omit-view-publication','    std::swap(frame->image_view, candidate.image_view);','','direct','success','parent destroyed with live children')
 control('replace-sync-identity','    frame->height = candidate.height;','    frame->height = candidate.height;frame->present_done={};','direct','success','recreate replaced synchronization identity')
 control('early-blit-count','        auto candidate = CreateWindowAdaptPass(requested_filter, current_swapchain_view_format);','        image_count=current_swapchain_image_count;auto candidate = CreateWindowAdaptPass(requested_filter, current_swapchain_view_format);','warm','image','failed update changed published state')
 control('omit-slot-normalization','        image_index %= image_count;','','shrink','success','draw used retired slot')
 control('clear-layers-before-frame','        if (presentation_recreate_required) {','        layers.clear();if (presentation_recreate_required) {','warm','image','failed update changed published state')
 control('omit-old-use-idle','    if (resource_update_required) {\n        WaitIdle();','    if (resource_update_required) {','warm','success','native owner destroyed before checked idle')
 control('omit-applet-refusal',applet[:applet.index('    // Filter')],applet[:applet.index('    // Filter')].replace('applet_capture_state == AppletCaptureState::Failed','false'),'applet-failed','draw','recreation reopened failed applet')
 control('global-format-only','frame->image_view_format != current_swapchain_view_format ||','current_swapchain_view_format != swapchain_view_format ||','pool-format','success','draw used stale Frame view format')
 control('omit-global-pass-format','swapchain_view_format != current_swapchain_view_format || presentation_recreate_required;','presentation_recreate_required;','pool-reverse','success','draw used stale Frame view format')
 print('PASS Frame/Blit factories, retry, shrink, shutdown and eleven exact controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'cases':cases,'terminal_cases':terminal_cases,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest()for n in sorted(inputs)},'actual':['Complete PresentManager::RecreateFrame and Frame declaration','Complete BlitScreen pass factory/DrawToFrame/CreateFramebuffer overloads and checked WaitIdle','Complete applet render handler and actual readback refusal prefix'],'fixtures':['Native factories, parent-child/use graph, source Layer/WindowAdapt Draw and helper allocation','Scheduler recording/presentation/device idle collaborators, real semaphore factory observation; no Vulkan runtime','Applet wrapper factories and successful readback tail; full readback checked in separate regression'],'boundary':'Resource tuple and cached metadata publication/retry after existing checked idle; no target content/completion or recoverable presentation-pool cancellation proof.'},indent=2)+'\n')
