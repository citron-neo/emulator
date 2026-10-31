"""Actual final composition and BlitScreen target Frame borrowing: no-unwind failure boundary."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'));p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path);p.add_argument('--baseline',action='store_true');p.add_argument('--candidate',action='store_true')
p.add_argument('--revision',default='7b2225e854c2784314177b9f1da0634cc3aa1e58');a=p.parse_args()
if a.repeat<1 or (a.baseline and a.candidate):p.error('positive repeat; one mode')
root=Path(__file__).resolve().parents[2];out=(a.output_dir or root/'.diagnostics/m4-composition').resolve();out.mkdir(parents=True,exist_ok=True)
inputs={'tools/gpu_lifecycle_tests/run_composition_tests.py'}
def data(n):return subprocess.check_output(['git','show',a.revision+':'+n],cwd=root) if (a.baseline or a.candidate) and n.startswith('src/') else (root/n).read_bytes()
def read(n):inputs.add(n);return data(n).decode('utf-8-sig').replace('\r\n','\n')
def block(s,m):
 start=s.index(m);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
w=read('src/video_core/renderer_vulkan/present/window_adapt_pass.cpp');b=read('src/video_core/renderer_vulkan/vk_blit_screen.cpp');c=json.loads(read('tools/gpu_lifecycle_tests/composition_candidate.json'))
for n in ['src/video_core/renderer_vulkan/present/window_adapt_pass.h','src/video_core/renderer_vulkan/vk_blit_screen.h','src/video_core/renderer_vulkan/present/present_push_constants.h','src/video_core/renderer_vulkan/vk_scheduler.h','src/video_core/renderer_vulkan/vk_scheduler.cpp','src/video_core/renderer_vulkan/renderer_vulkan.cpp','src/video_core/renderer_vulkan/renderer_vulkan.h']:read(n)
if a.candidate:
 for old,new in c['window_replacements']:assert w.count(old)==1;w=w.replace(old,new)
 for old,new in c['blit_replacements']:assert b.count(old)==1;b=b.replace(old,new)
window=block(w,'void WindowAdaptPass::Draw(')
# Observe the exact final Record argument-construction boundary, inside the production handler.
window=window.replace('scheduler.Record([=](vk::CommandBuffer cmdbuf) {','BeforeFinalRecord(scheduler,dst,owner);\n        scheduler.Record([=](vk::CommandBuffer cmdbuf) {')
renderer=read('src/video_core/renderer_vulkan/renderer_vulkan.cpp')
if a.candidate:
 for old,new in c['renderer_replacements']:assert renderer.count(old)==1;renderer=renderer.replace(old,new)
rh=read('src/video_core/renderer_vulkan/renderer_vulkan.h')
applet=block(renderer,'void RendererVulkan::RenderAppletCaptureLayer(')
get=block(renderer,'std::vector<u8> RendererVulkan::GetAppletCaptureBuffer(');get=get[:get.index('    scheduler.RequireNativeBorrowAdmission();')]+'    return {0x5a};\n}'
source=read('tools/gpu_lifecycle_tests/composition_fixture.cpp.in').replace('@APPLET_STATE@',block(rh,'    enum class AppletCaptureState')+';').replace('@APPLET_METHODS@',applet+'\n'+get)
guard_start=renderer.index('    blit_swapchain.DrawToFrame(rasterizer, frame, framebuffers,');guard_end=renderer.index('    CITRON_PROFILE_FRAME_MARK();',guard_start)
guard_start=renderer.rfind('    try {',renderer.index('void RendererVulkan::Composite('),guard_start) if 'present.composition' in renderer else guard_start
guard=renderer[guard_start:guard_end]
source=source.replace('@FIXED@',str(not a.baseline).lower()).replace('@PRESENT_GUARD@',guard).replace('@NATIVE_BORROW@',block(read('src/video_core/renderer_vulkan/vk_scheduler.cpp'),'void Scheduler::FinishNativeBorrow('))
source=source.replace('@WINDOW@',window).replace('@BLIT@',block(b,'void BlitScreen::DrawToFrame(')).replace('@FRAME@',block(read('src/video_core/renderer_vulkan/vk_present_manager.h'),'struct Frame {')+';')
source=source.replace('@FRAME_FORMAT@','f.image_view_format=VK_FORMAT_R8G8B8A8_UNORM;' if 'image_view_format{' in read('src/video_core/renderer_vulkan/vk_present_manager.h') else '')
terminal=out/'terminal.cpp';terminal.write_text(read('src/common/terminal_fail_stop.cpp'),encoding='utf-8');read('src/common/terminal_fail_stop.h')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-I',str(root/'src'),'-isystem',str(root/'externals/Vulkan-Headers/include')]
def build(name,text):
 cpp=out/(name+'.cpp');cpp.write_text(text,encoding='utf-8');exe=out/(name+('.exe' if os.name=='nt' else ''))
 r=subprocess.run(flags+[str(cpp),str(terminal),'-o',str(exe)],capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
 if r.returncode:raise RuntimeError(r.stderr)
 return exe
def run(exe,name,case,slot,code=0,marker='PASS composition'):
 r=subprocess.run([str(exe),case,str(slot)],capture_output=True,text=True,timeout=20);log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
 assert r.returncode==code and marker in log,(name,r.returncode,log)
 if code==86:
  tail=log.rsplit('EVENT composition.borrow',1)[-1]
  assert '.destroy' not in tail and not any(x in log for x in ['owners.released','atexit']),(name,log)
  if 'composition.retirement' in marker or 'native_borrow.completion' in marker:assert 'source_scope.end' not in log,(name,log)
exe=build('production',source);controls=[]
normal=['multi-slot','multi-slot-partial','applet-configure-failure','applet-composition-failure','normal','lanczos','crt','gate','shutdown-active','capture','record-before','record-enqueued','submit-partial','submit-full']
failures=['retire-before','retire-partial','retire-worker','retire-poll-pending','retire-poll-error','present-record-enqueued','present-flush','present-present','closed-recreate','closed-fastpath','worker-begin','worker-clear','worker-pipeline','worker-push','worker-bind','worker-draw','worker-end','idle-failure','closed','closed-direct']
if a.baseline:
 for i in range(a.repeat):
  for slot in range(3):
   for prefix in ['','split-']:
    for case in ['record-enqueued','submit-partial']:run(exe,f'{prefix}{case}-{slot}-{i}',prefix+case,slot,91,'FAIL Frame owner unwound with uncertain composition')
 print('PASS twelve historical target Frame borrow witnesses',flush=True)
else:
 for i in range(a.repeat):
  for slot in range(3):
   for prefix in ['','split-']:
    for case in normal:run(exe,f'{prefix}{case}-{slot}-{i}',prefix+case,slot)
    for case in failures:
     stage='native_borrow.completion' if case.startswith('retire-poll') else 'composition.retirement' if case.startswith('retire-') else 'present.composition' if case.startswith('present-') else 'native_borrow.entry' if case.startswith('closed') else 'renderer.native_idle' if case=='idle-failure' else 'composition.recording' if case.startswith('worker-') else 'composition.retirement'
     run(exe,f'{prefix}{case}-{slot}-{i}',prefix+case,slot,86,'stage='+stage)
 def control(name,old,new,case,witness,code=91):
  assert source.count(old)==1,(name,source.count(old));mutant=build(name,source.replace(old,new))
  for i in range(a.repeat):run(mutant,f'{name}-{i}',case,1,code,witness)
  controls.append(name)
 control('omit-checked-retirement','            scheduler.FinishNativeBorrow();','','record-enqueued','FAIL composition escaped before checked retirement')
 control('omit-native-receipt','const auto result = master_semaphore->PollNativeResourceLifetime(tick);','const auto result = VK_SUCCESS; (void)tick;','retire-poll-pending','FAIL unconfirmed retirement returned')
 control('omit-presentation-host-boundary','Common::TerminalFailStop("present.composition", "host_exception");','throw;','present-record-enqueued','FAIL unexpected host exception escaped')
 control('omit-presentation-vulkan-boundary','Common::TerminalFailStop("present.composition", "vulkan_exception",\n                                 static_cast<int>(error.GetResult()));','throw;','present-present','FAIL unexpected host exception escaped')
 control('unwind-worker-host','Common::TerminalFailStop("composition.recording", "host_exception");','throw;','worker-clear','FAIL Frame owner unwound with uncertain composition')
 control('unwind-worker-vulkan','Common::TerminalFailStop("composition.recording", "vulkan_exception",\n                                         static_cast<int>(error.GetResult()));','throw;','worker-end','FAIL Frame owner unwound with uncertain composition')
 control('extra-normal-finish','    // Advance to next image','    scheduler.Finish();\n    // Advance to next image','normal','FAIL composition added normal submission')
 control('advance-before-record','    // Perform the draw','    ++image_index;\n    // Perform the draw','gate','FAIL queued composition lost owner or advanced slot')
 control('omit-blit-admission','                             VkFormat current_swapchain_view_format) {\n    scheduler.RequireNativeBorrowAdmission();','                             VkFormat current_swapchain_view_format) {','closed-fastpath' if 'CreateWindowAdaptPass(' in b else 'closed-recreate','FAIL closed Blit mutated metadata before admission')
 control('omit-applet-isolation',applet[:applet.index('    // Filter')],applet[:applet.index('    // Filter')].replace('applet_capture_state == AppletCaptureState::Failed','false'),'applet-configure-failure','FAIL healthy composition reopened failed applet')
 control('omit-direct-admission','    scheduler.RequireNativeBorrowAdmission();\n    auto source_scope','    auto source_scope','closed-direct','FAIL closed composition entry admitted')
 print('PASS composition slots, callback/owner gates, terminal boundaries and eleven controls',flush=True)
(out/'extraction.json').write_text(json.dumps({'baseline':a.baseline,'candidate':a.candidate,'revision':a.revision if a.baseline or a.candidate else None,'repeat':a.repeat,'slots':3,'prefixes':['','split-'],'normal_cases':normal,'terminal_cases':failures,'controls':controls,'sha256':{n:hashlib.sha256(data(n)).hexdigest()for n in sorted(inputs)},'actual':['Complete WindowAdaptPass::Draw including final callback, complete BlitScreen::DrawToFrame, Frame and PresentPushConstants declarations, TerminalFailStop','Actual Scheduler::FinishNativeBorrow and Renderer Composite draw/flush/present guard block','Complete applet RenderAppletCaptureLayer and actual readback refusal prefix'],'instrumentation':['BeforeFinalRecord argument-construction boundary inside actual handler; synthetic capture failure','Real semaphore enqueue/callback and active-worker shutdown gates'],'fixtures':['ConfigureDraw/source helper/internal Finish collaborators, native resources/use graph, queued recording/native effects and checked-idle model','Pass/resource preparation, applet allocation/readback success tail and presentation collaborators; no Vulkan/game/native completion receipt'],'boundary':'Host errors retire the checked prefix before unwinding borrowed Frame/pass/source owners; unconfirmed retirement, callback and dequeued presentation errors are terminal. Applet Failed remains isolated; success is enqueue only.'},indent=2)+'\n')
