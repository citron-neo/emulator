# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GraphicBuffer/session retirement factories and destructors; fixture kernel/devices."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat',type=int,default=3)
p.add_argument('--output-dir',type=Path)
p.add_argument('--cxx',default=os.environ.get('CXX','g++'))
a=p.parse_args()
if a.repeat<1:p.error('--repeat must be positive')
root=Path(__file__).resolve().parents[2]
out=(a.output_dir or root/'.diagnostics/m4-nvdrv-retirement').resolve();out.mkdir(parents=True,exist_ok=True)


def block(text,marker):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]


base='src/core/hle/service/'
module=(root/(base+'nvdrv/nvdrv.cpp')).read_text()
container=(root/(base+'nvdrv/core/container.cpp')).read_text()
container_h=(root/(base+'nvdrv/core/container.h')).read_text()
interface=(root/(base+'nvdrv/nvdrv_interface.cpp')).read_text()
graphic=(root/(base+'nvnflinger/ui/graphic_buffer.cpp')).read_text()
producer=(root/(base+'nvnflinger/buffer_queue_producer.cpp')).read_text()
factories='\n'.join(block(module,marker).replace('Module::','') for marker in (
    'std::optional<Module::SessionRetirement> Module::PrepareSessionRetirement(',
    'Common::OwnedRetirementQueue::Owner Module::PrepareGraphicBufferRetirement('))
session_decl=block(container_h,'struct Session {')
failure_decl=block(container_h,'struct ProcessReleaseFailure {')
release_helper=block(container,'static void ReleaseProcessOwner(')
lifetime_decl=block(container_h,'struct SessionLifetime {')
lifetime_methods=container[container.index('Container::SessionLifetime::SessionLifetime('):container.index('Session::Session(')]
session_methods=container[container.index('Session::Session('):container.index('struct ContainerImpl {')]
destructor=block(interface,'NVDRV::~NVDRV()').replace('NVDRV::','')
set_buffer=block(producer,'Status BufferQueueProducer::SetPreallocatedBuffer(').replace('BufferQueueProducer::','')
# Exact ownership/reuse prefix. The application heap preallocation optimization
# is outside this fixture; no full heap mapper/session initialization claim.
open_session=block(container,'SessionId Container::OpenSession(')
open_session=open_session[:open_session.index('    // Optimization')]+open_session[open_session.rindex('\n    return '):]
open_session=open_session.replace('Container::','')
close_session=block(container,'void Container::CloseSession(').replace('Container::','')
retain_session=block(container,'std::optional<Container::SessionLifetime> Container::RetainSession(').replace('Container::','')
graphic=graphic[graphic.index('namespace Service::android {'):]
fixture='tools/physical_coherency_tests/nvdrv_retirement_tests.cpp.in'
source=(root/fixture).read_text()
for key,value in {'SESSION_DECL':session_decl,'SESSION_METHODS':session_methods,'MODULE_METHODS':factories,
                  'SESSION_ID':block(container_h,'struct SessionId {'),
                  'FAILURE_DECL':failure_decl,'RELEASE_HELPER':release_helper,'LIFETIME_DECL':lifetime_decl,'LIFETIME_METHODS':lifetime_methods,
                  'NVDRV_DESTRUCTOR':destructor,'GRAPHIC_BODIES':graphic,'SET_BUFFER':set_buffer,'OPEN_SESSION':open_session,
                  'CLOSE_SESSION':close_session,'RETAIN_SESSION':retain_session}.items():
    source=source.replace('@'+key+'@',value)
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(root/'src')]


def run(name,value,expected=None):
    cpp=out/(name+'.cpp');cpp.write_text(value);exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(flags+[str(cpp),'-o',str(exe)],check=True)
    for i in range(a.repeat if expected is None else 1):
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15);log=result.stdout+result.stderr
        (out/f'{name}-{i}.log').write_text(log)
        assert (result.returncode==0 and 'PASS actual GraphicBuffer' in log) if expected is None else (result.returncode!=0 and 'FAIL '+expected in log),(name,log)
    print('PASS NVDRV retirement: '+name,flush=True)


run('production',source)
controls=[
    ('omit-retain-pid',source.replace(' ||\n        session.pid != session.process->GetProcessId()', ''),
     'PID mismatch retained source owner'),
    ('omit-retain-incarnation',source.replace('!id.generation || id.generation != session.id.generation ||','false ||'),
     'reused session retained old source owner'),
    ('omit-session-frame-retirement',source.replace(
        '    auto frame_retirement = impl->host1x.MemoryManager().BeginFrameMappingMutation();','').replace(
        '        frame_retirement.Complete();','').replace('    frame_retirement.Complete();',''),
     'shutdown session retained old frame authority'),
    ('discard-preparation-failure',source.replace('retirements->Quarantine(std::move(record));','record.reset();'),
     'failed GraphicBuffer preparation discarded uncertain owners'),
    ('borrow-module-owner',source.replace('owner = shared_from_this()', 'owner = std::shared_ptr<Module>(this, [](Module*) {})'),
     'closed GraphicBuffer retirement executed or released owners'),
    ('omit-graphic-lifetime',source.replace('[owner = shared_from_this(), handle, lifetime]', '[owner = shared_from_this(), handle]').replace(
        '            if (!lifetime->backing.OwnerAlive()) return false;',''),
     'GraphicBuffer lost Process/backing owner'),
    ('graphic-direct-cleanup',source.replace(block(graphic,'GraphicBuffer::~GraphicBuffer()'),
        'GraphicBuffer::~GraphicBuffer(){if(m_retirement){auto& file=m_owner->GetContainer().GetNvMapFile();file.UnpinHandle(BufferId());file.FreeHandle(BufferId(),true);}}'),
     'closed GraphicBuffer retirement executed or released owners'),
    ('session-direct-cleanup',source.replace(destructor,
        '~NVDRV(){if(session_retirement)nvdrv->GetContainer().CloseSession(**session_retirement->published,true);}'),
     'closed session retirement executed or released owners'),
    ('destroy-slot-before-admission',source.replace('    auto dispatch = nvdrv->TryEnterDispatch();','    slots[slot] = {};\n    auto dispatch = nvdrv->TryEnterDispatch();'),
     'refused GraphicBuffer publication destroyed old slot'),
    ('assign-session-members-first',source.replace(block(session_methods,'Session& Session::operator=('),
        'Session& Session::operator=(Session&& other){backing=std::move(other.backing);process_owner=std::move(other.process_owner);mapper=std::move(other.mapper);return *this;}'),
     'session assignment released backing before mapper'),
    ('ignore-free-refusal',source.replace('return file.FreeHandle(handle->id, true).has_value();','file.FreeHandle(handle->id,true);return true;'),
     'refused GraphicBuffer free released owners'),
    ('release-unknown-session',source.replace('if (!lifetime->backing.OwnerAlive() || !*published) return false;',
        'if (!lifetime->backing.OwnerAlive() || !*published) return true;'),
     'unknown session was guessed or released'),
    ('retire-session-under-lock',source.replace('    std::optional<Session> retired;\n','').replace(
        '    std::scoped_lock lk(impl->session_guard);','    std::scoped_lock lk(impl->session_guard);\n    std::optional<Session> retired;'),
     'session reuse released old owners under session_guard'),
    ('hold-graphic-facade-through-retire',source.replace('        m_owner.reset(); // Record now supplies the retained Module anchor.',''),
     'GraphicBuffer provider release escaped retirement entry'),
    ('hold-session-facade-through-retire',source.replace('        nvdrv.reset();',''),
     'session provider release escaped retirement entry'),
    ('allow-duplicate-session-retire',source.replace('    if (session_id.generation != session.id.generation || !session.is_active || session.ref_count <= 0)\n        throw std::runtime_error("nvdrv session already retired");',''),
     'duplicate session retirement changed reference accounting'),
    ('borrow-retired-session',source.replace('        !session.is_active || session.ref_count <= 0 ||\n', ''),
     'retired session admitted a new owner borrow'),
    ('discard-empty-descriptor',source.replace('    // Allocate the public wrapper before any nvmap effect.',
        '    if(!buffer)return {};\n    // Allocate the public wrapper before any nvmap effect.'),
     'empty GraphicBuffer descriptor lost supported behavior'),
    ('ignore-graphic-owner-epoch',source.replace('            if (!lifetime->backing.OwnerAlive()) return false;',''),
     'stale GraphicBuffer backing permitted owner callbacks'),
    ('discard-failed-process-backing',source.replace('auto* record = failure.release();',
        'failure.reset();auto* record = static_cast<ProcessReleaseFailure*>(nullptr);').replace(
        '            record->next = head;', '            if(record)record->next = head;'),
     'failed Process close lost backing or retried zero-ref owner'),
    ('retry-failed-process-close',source.replace('        failure->process_identity = process;','        process->Close();\n        failure->process_identity = process;'),
     'failed Process close lost backing or retried zero-ref owner'),
    ('omit-process-failure-seal',source.replace('        if (failure->domain) failure->domain->Seal();',''),
     'failed Process close granted admission'),
    ('close-process-after-epoch-death',source.replace('    if (!backing.OwnerAlive()) {\n        retain_failure();\n        return;\n    }',''),
     'stale Process snapshot dereferenced retired Kernel owner'),
    ('omit-process-quarantine-ledger',source.replace('        do {\n            record->next = head;\n        } while (!failed_process_releases.compare_exchange_weak(\n            head, record, std::memory_order_acq_rel, std::memory_order_acquire));',
        '        (void)record; (void)head;'),
     'failed Process close lost quarantine bookkeeping'),
]
for name,value,expected in controls:
    assert value!=source;run(name,value,expected)
paths=[base+p for p in ['nvdrv/nvdrv.cpp','nvdrv/nvdrv.h','nvdrv/nvdrv_interface.cpp','nvdrv/nvdrv_interface.h',
    'nvdrv/core/container.cpp','nvdrv/core/container.h','nvnflinger/ui/graphic_buffer.cpp','nvnflinger/ui/graphic_buffer.h',
    'nvnflinger/buffer_queue_producer.cpp']]+['src/common/owned_retirement.h','src/common/producer_drain.h',fixture,
    'tools/physical_coherency_tests/run_nvdrv_retirement_tests.py','src/common/frame_mapping.h']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats':a.repeat,'controls':[c[0] for c in controls],
    'actual':['Module retirement factory bodies','GraphicBuffer factory/destructor','NVDRV destructor','SetPreallocatedBuffer','Session construction/move/destruction','OpenSession ownership/reuse prefix (heap optimization excluded)','CloseSession and RetainSession bodies'],
    'fixtures':['Process reference primitive, backing allocation, nvmap/Container/device/native work'],
    'boundary':'Local retained retirement owner protocol; no whole-device native completion or trusted terminal quarantine disposal.'},indent=2))
