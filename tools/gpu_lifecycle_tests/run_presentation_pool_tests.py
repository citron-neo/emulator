"""Bounded fixed presentation pool audit and reproduced swapchain ownership failures."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--repeat',type=int,default=3);p.add_argument('--output-dir',type=Path)
p.add_argument('--baseline',action='store_true');p.add_argument('--candidate',action='store_true');p.add_argument('--revision',default='c3fa0f0d4bc791234f64a2d2c84f3dd78d6ede8e');a=p.parse_args()
if a.repeat<1 or(a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-presentation-pool').resolve();out.mkdir(parents=True,exist_ok=True);inputs={'tools/gpu_lifecycle_tests/run_presentation_pool_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root)if(a.baseline or a.candidate)and n.startswith('src/')else(root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8-sig').replace('\r\n','\n')
def block(s,m):
 start=s.index(m);end=s.index('{',start)+1;d=1
 while d:d+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
s=read('src/video_core/renderer_vulkan/vk_swapchain.cpp');h=read('src/video_core/renderer_vulkan/vk_swapchain.h');v=read('src/video_core/renderer_vulkan/vk_present_manager.cpp');ph=read('src/video_core/renderer_vulkan/vk_present_manager.h');c=json.loads(read('tools/gpu_lifecycle_tests/presentation_pool_candidate.json'))
if a.candidate:
 for old,new in c['swapchain_replacements']:assert s.count(old)==1;s=s.replace(old,new)
 for old,new in c['present_replacements']:assert v.count(old)==1;v=v.replace(old,new)
 for old,new in c['swapchain_header_replacements']:assert h.count(old)==1;h=h.replace(old,new)
 for old,new in c['present_header_replacements']:assert ph.count(old)==1;ph=ph.replace(old,new)
new='bool Swapchain::Create('in s
swap='\n'.join(block(s,m)for m in [('bool 'if new else'void ')+'Swapchain::Create(','bool Swapchain::AcquireNextImage(','void Swapchain::Present(','void Swapchain::Destroy()'])
impl=block(v,'void PresentManager::CopyToSwapchainImpl(');cmd='    const vk::CommandBuffer cmdbuf{frame->cmdbuf};';impl=impl[:impl.index(cmd)]+'    RecordCopy(*this, frame);\n'+cmd+'\n'+impl[impl.index('    const VkSemaphore present_semaphore'):]
methods='\n'.join(block(v,m)for m in ['Frame* PresentManager::GetRenderFrame(','void PresentManager::Present(','void PresentManager::ReleaseRenderFrame(','void PresentManager::PresentThread(','void PresentManager::WaitPresent()','void PresentManager::Shutdown()','void PresentManager::RecreateSwapchain(','void PresentManager::SetImageCount()','void PresentManager::CopyToSwapchain(Frame*'])+'\n'+impl
methods=methods.replace('    std::scoped_lock swapchain_lock{swapchain_mutex};', '    ObservePresentJoin(swapchain_mutex);\n    std::scoped_lock swapchain_lock{swapchain_mutex};')
source=read('tools/gpu_lifecycle_tests/presentation_pool_fixture.cpp.in')
for k,t in {'FRAME':block(ph,'struct Frame {')+';','SWAPCHAIN':swap,'PRESENT':methods,'CREATE_DECL':('bool'if new else'void')+' Create(VkSurfaceKHR,u32,u32);','REBUILD':'return Create(s,w,h);'if new else'Create(s,w,h);return true;','COPY_DECL':'void CopyToSwapchainImpl(Frame*,bool&);'if'bool& copy_started'in impl else'void CopyToSwapchainImpl(Frame*);'}.items():source=source.replace('@'+k+'@',t)
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(n,t):
 cpp=out/(n+'.cpp');cpp.write_text(t,encoding='utf-8');exe=out/(n+('.exe'if os.name=='nt'else''));r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True);(out/(n+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if r.returncode:raise RuntimeError(r.stderr)
 return exe
def run(exe,n,case,code=0,marker='PASS pool'):
 r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr;(out/(n+'.log')).write_text(log,encoding='utf-8');assert r.returncode==code and marker in log,(n,r.returncode,log)
 if code==86:assert'OWNERS_RELEASED'not in log and'ATEXIT'not in log,(n,log)
exe=build('production',source);controls=[];cases=['normal','async','pending-fence','stale-outdated','outdated-valid-index','surface-lost','active-rebuild','shutdown'];terminal_cases={'failed-idle':'swapchain.retirement','present-lost':'present.copy','bad-acquire':'swapchain.acquire','bad-index':'swapchain.acquire','zero-surface':'present.recreate','create-fail':'swapchain.create','semaphore-fail':'swapchain.create'}
if a.baseline:
 for i in range(a.repeat):
  for case,w in [('stale-outdated','acquire used invalid image index'),('failed-idle','swapchain owner destroyed with pending use'),('surface-lost','surface destroyed before child swapchain')]:run(exe,f'{case}-{i}',case,91,'FAIL '+w)
 print('PASS three critical historical owner/index witnesses',flush=True)
else:
 for i in range(a.repeat):
  for case in cases:run(exe,f'{case}-{i}',case)
  for case,stage in terminal_cases.items():run(exe,f'{case}-{i}',case,86,'stage='+stage)
 def control(n,old,replacement,case,w):
  assert source.count(old)==1,(n,source.count(old));mutant=build(n,source.replace(old,replacement))
  for i in range(a.repeat):run(mutant,f'{n}-{i}',case,91,'FAIL '+w)
  controls.append(n)
 control('use-index-on-outdated','return true; // No image was acquired; image_index is not an output receipt.','break;','outdated-valid-index','failed acquire touched resource ticks')
 control('ignore-idle','if (idle != VK_SUCCESS)','if (false)','failed-idle','swapchain owner destroyed with pending use')
 control('publish-surface-before-child','                auto candidate = CreateSurface(instance, render_window.GetWindowInfo());','                surface = CreateSurface(instance, render_window.GetWindowInfo());auto candidate=CreateSurface(instance,render_window.GetWindowInfo());','surface-lost','surface destroyed before child swapchain')
 control('retry-after-copy',' || copy_started','','present-lost','render_ready consumed twice')
 control('release-before-copy','        CopyToSwapchain(frame);\n\n        ReleaseRenderFrame(frame);','        ReleaseRenderFrame(frame);\n        CopyToSwapchain(frame);','active-rebuild','active present lost Frame owner')
 print('PASS fixed-pool shrink/grow, active worker, failed acquire, surface owner and five controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'cases':cases,'terminal_cases':terminal_cases,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest()for n in sorted(inputs)},'actual':['Swapchain Create/AcquireNextImage/Present/Destroy, GetRenderFrame/Present/Release/PresentThread/Shutdown/WaitPresent/SetImageCount/RecreateSwapchain/CopyToSwapchain','CopyToSwapchainImpl acquisition prefix and real graphics-submit/present suffix; Frame declaration'],'fixtures':['Swapchain allocation/semaphore factories and native driver/fence completion graph; command recording replaced by RecordCopy observer','Fixed Frame allocation mirrors constructor once; real std::jthread/semaphore gates; WaitPresent observer checks actual mutex contention before native join'],'boundary':'Bounded Presentation Pool audit; these fixtures do not prove whole producer closure, native Vulkan/game behavior or Guest-backed admission.'},indent=2)+'\n')
