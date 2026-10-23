# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual six NVDRV Module dispatch bodies with fixture devices/builders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-nvdrv-dispatch-drain').resolve()
out.mkdir(parents=True, exist_ok=True)
cpp_path = 'src/core/hle/service/nvdrv/nvdrv.cpp'
text = (root / cpp_path).read_text()


def method(name):
    marker = ('DeviceFD' if name == 'Open' else 'NvResult') + ' Module::' + name + '('
    start = text.index(marker)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end].replace('Module::', '')


names = ['Open', 'Ioctl1', 'Ioctl2', 'Ioctl3', 'Close', 'QueryEvent']
methods = {name: method(name) for name in names}
interface_path = 'src/core/hle/service/nvdrv/nvdrv_interface.cpp'
interface = (root / interface_path).read_text()


def interface_method(marker):
    start = interface.index(marker)
    end = interface.index('{', start) + 1
    depth = 1
    while depth:
        depth += (interface[end] == '{') - (interface[end] == '}')
        end += 1
    return interface[start:end].replace('NVDRV::', '')


initialize = interface_method('void NVDRV::Initialize(')
service_error = interface_method('void NVDRV::ServiceError(')
source = r'''
#include <array>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include "common/producer_drain.h"
#include "common/owned_retirement.h"
#include "common/scope_exit.h"
using namespace std::chrono_literals;
using u8=unsigned char;using u32=unsigned;using DeviceFD=int;constexpr DeviceFD INVALID_NVDRV_FD=-1;
struct Ioctl {u32 raw=0;};namespace NvCore {using SessionId=int;}
namespace Kernel {struct KEvent {};struct KProcess {};}
enum class NvResult {Success,Busy,InvalidState,NotImplemented,BadParameter,Failed};
void Require(bool value,const char* why) {if(!value) {std::cerr<<"FAIL "<<why<<"\n";std::cerr.flush();std::_Exit(91);}}
#define LOG_ERROR(...) ((void)0)
struct Device {
    std::function<void()> action,on_destroy;int calls=0;NvResult result=NvResult::Success;Kernel::KEvent event;
    ~Device() {if(on_destroy)on_destroy();}
    void Work() {++calls;if(action)action();}
    void OnOpen(NvCore::SessionId,DeviceFD) {Work();}
    void OnClose(DeviceFD) {Work();}
    NvResult Ioctl1(DeviceFD,Ioctl,std::span<const u8>,std::span<u8> out) {Work();out[0]=42;return result;}
    NvResult Ioctl2(DeviceFD,Ioctl,std::span<const u8>,std::span<const u8>,std::span<u8> out) {Work();out[0]=42;return result;}
    NvResult Ioctl3(DeviceFD,Ioctl,std::span<const u8>,std::span<u8> out,std::span<u8> inl) {Work();out[0]=42;inl[0]=43;return result;}
    Kernel::KEvent* QueryEvent(u32) {Work();return &event;}
};
struct Module {
    const std::shared_ptr<Common::ProducerDrain> dispatch_domain;
    std::shared_ptr<Common::OwnedRetirementQueue> retirements;
    struct SessionRetirement {std::shared_ptr<std::optional<NvCore::SessionId>> published;Common::OwnedRetirementQueue::Owner record;};
    std::optional<SessionRetirement> PrepareSessionRetirement(Kernel::KProcess*,const Common::ProducerDrain::Entry&) {
        auto published=std::make_shared<std::optional<NvCore::SessionId>>();
        return SessionRetirement{published,Common::OwnedRetirementQueue::Prepare([]{return true;})};
    }
    using FilesContainerType=std::unordered_map<DeviceFD,std::shared_ptr<Device>>;
    FilesContainerType open_files;DeviceFD next_fd=2;
    struct OpenResult {FilesContainerType::iterator file;};
    std::unordered_map<std::string,std::function<OpenResult(DeviceFD)>> builders;
    struct Container {
        int opens=0;std::function<void()> on_open;
        NvCore::SessionId OpenSession(Kernel::KProcess*) {++opens;if(on_open)on_open();return 5;}
    } container;
    Container& GetContainer() {return container;}
    explicit Module(std::shared_ptr<Common::ProducerDrain> domain):dispatch_domain{std::move(domain)},retirements{std::make_shared<Common::OwnedRetirementQueue>(dispatch_domain)} {}
''' + '\n'.join(methods.values()) + r'''
};
struct Harness {
    std::shared_ptr<Common::ProducerDrain> domain=std::make_shared<Common::ProducerDrain>();
    Module module{domain};std::shared_ptr<Device> device=std::make_shared<Device>();int builds=0;
    std::array<u8,1> input{7},output{9},inlined{11};Kernel::KEvent sentinel;Kernel::KEvent* event=&sentinel;
    Harness() {
        module.open_files.emplace(1,device);
        module.builders["device"]=[this](DeviceFD fd) {++builds;return Module::OpenResult{module.open_files.emplace(fd,device).first};};
    }
    NvResult Run(int kind) {
        switch(kind) {
        case 0:return module.Open("device",0)==INVALID_NVDRV_FD?NvResult::Failed:NvResult::Success;
        case 1:return module.Ioctl1(1,{},input,output);
        case 2:return module.Ioctl2(1,{},input,input,output);
        case 3:return module.Ioctl3(1,{},input,output,inlined);
        case 4:return module.Close(1);
        default:return module.QueryEvent(1,0,event);
        }
    }
};
void Closed(int kind) {
    Harness h;auto closure=h.domain->TryClose(64);Require(bool(closure),"nvdrv close failed");
    Require(h.Run(kind)==(kind==0?NvResult::Failed:NvResult::Busy) && h.device->calls==0 && h.builds==0 &&
        h.module.next_fd==2 && h.module.open_files.size()==1 && h.output[0]==9 && h.inlined[0]==11 && h.event==&h.sentinel,
        "closed nvdrv dispatch performed effects");
    Require(closure->ResumeNoEffects() && h.Run(kind)==NvResult::Success && h.device->calls==1,"nvdrv resume lost dispatch");
}
void Active(int kind,bool seal=false) {
    Harness h;std::promise<void> entered,release;auto began=entered.get_future();auto leave=release.get_future().share();
    h.device->action=[&] {entered.set_value();leave.wait();};
    auto work=std::async(std::launch::async,[&] {return h.Run(kind);});
    Require(began.wait_for(3s)==std::future_status::ready,"nvdrv callback rendezvous timeout");
    auto closure=h.domain->TryClose(64);
    Require(closure && closure->Poll()==Common::ProducerDrain::Result::Pending,"active nvdrv dispatch escaped drain");
    if(seal)closure->Quarantine();
    release.set_value();Require(work.get()==NvResult::Success,"admitted nvdrv dispatch failed");
    Require(closure->Poll()==(seal?Common::ProducerDrain::Result::Unknown:Common::ProducerDrain::Result::DrainedSubset),
        "nvdrv return cleared quarantine or failed drain");
    if(seal)Require(!h.domain->TryEnter(),"old nvdrv completion reopened admission");
    if(kind>=1 && kind<=3)Require(h.output[0]==42 && (kind!=3||h.inlined[0]==43),"nvdrv drain missed output");
    if(kind==4)Require(h.module.open_files.empty(),"nvdrv drain missed file erasure");
    if(kind==5)Require(h.event==&h.device->event,"nvdrv drain missed event publication");
}
void Failed(int kind) {
    Harness h;h.device->action=[] {throw 42;};bool caught=false;
    try{(void)h.Run(kind);}catch(int){caught=true;}
    Require(caught && !h.domain->TryClose(64) && !h.domain->TryEnter(),"nvdrv callback exception granted drain authority");
}
void OrdinaryErrors() {
    Harness h;h.device->result=NvResult::NotImplemented;
    for(int kind=1;kind<=3;++kind)Require(h.Run(kind)==NvResult::NotImplemented,"nvdrv error result changed");
    Require(h.module.Ioctl1(-1,{},h.input,h.output)==NvResult::InvalidState &&
        h.module.Close(99)==NvResult::NotImplemented && h.module.Open("unknown",0)==INVALID_NVDRV_FD,
        "nvdrv validation result changed");
    auto closure=h.domain->TryClose(64);
    Require(closure && closure->Poll()==Common::ProducerDrain::Result::DrainedSubset,
        "ordinary nvdrv error unnecessarily sealed domain");
}
void BuilderAndDestruction() {
    Harness h;std::promise<void> entered,release;auto began=entered.get_future();auto leave=release.get_future().share();
    h.module.builders["device"]=[&](DeviceFD fd) {entered.set_value();leave.wait();return Module::OpenResult{h.module.open_files.emplace(fd,h.device).first};};
    auto work=std::async(std::launch::async,[&] {return h.Run(0);});
    Require(began.wait_for(3s)==std::future_status::ready,"nvdrv builder rendezvous timeout");
    auto closure=h.domain->TryClose(64);
    Require(closure && closure->Poll()==Common::ProducerDrain::Result::Pending,"nvdrv builder escaped drain");
    release.set_value();Require(work.get()==NvResult::Success && closure->ResumeNoEffects(),"nvdrv builder failed drain");
    h.module.open_files.erase(2);
    // The separate destruction rendezvous uses the last map-held shared owner.
    std::promise<void> destroying,destroy_release;auto destroyed=destroying.get_future();auto done=destroy_release.get_future().share();
    h.device->on_destroy=[&] {destroying.set_value();done.wait();};h.device.reset();
    auto close=std::async(std::launch::async,[&] {return h.module.Close(1);});
    Require(destroyed.wait_for(3s)==std::future_status::ready,"nvdrv device destruction rendezvous timeout");
    auto closing=h.domain->TryClose(64);
    Require(closing && closing->Poll()==Common::ProducerDrain::Result::Pending,"nvdrv device destruction escaped drain");
    destroy_release.set_value();Require(close.get()==NvResult::Success && closing->Poll()==Common::ProducerDrain::Result::DrainedSubset,
        "nvdrv close failed device destruction drain");
}
int main() {for(int kind=0;kind<6;++kind){Closed(kind);Active(kind);Active(kind,true);Failed(kind);}
    OrdinaryErrors();BuilderAndDestruction();std::cout<<"PASS actual NVDRV dispatch lifecycle, output, drain and exception quarantine\n";}
'''
init_fixture = r'''
#define LOG_WARNING(...) ((void)0)
constexpr int ResultSuccess=0;
struct HLERequestContext {
    int replies=0;NvResult result=NvResult::Failed;Kernel::KProcess process;std::function<void()> on_reply;
    int GetCopyHandle(int i) {return i+1;}
    template<typename T>struct Owner {T* value;T* GetPointerUnsafe() {return value;}};
    template<typename T>Owner<T> GetObjectFromHandle(int) {return {&process};}
};
namespace IPC {
struct ResponseBuilder {
    HLERequestContext& ctx;ResponseBuilder(HLERequestContext& ctx_,int):ctx{ctx_} {}
    void Push(int) {}
    void PushEnum(NvResult result) {if(ctx.on_reply)ctx.on_reply();++ctx.replies;ctx.result=result;}
};
struct RequestParser {
    RequestParser(HLERequestContext&) {}
    template<typename T>T Pop() {return T{};}
};
}
struct FakeKernel {
    std::shared_ptr<Common::ProducerDrain> domain=std::make_shared<Common::ProducerDrain>();
    const auto& NvdrvDispatchDomain() const {return domain;}
};
struct System {FakeKernel kernel;FakeKernel& Kernel() {return kernel;}};
struct NVDRV {
    System& system;std::shared_ptr<Module> nvdrv;bool is_initialized=false;NvCore::SessionId session_id=0;
    std::optional<Module::SessionRetirement> session_retirement;
    NVDRV(System& system_):system{system_},nvdrv{std::make_shared<Module>(system.kernel.domain)} {}
''' + initialize + '\n' + service_error + r'''
};
void InitializeTests() {
    {
        System system;NVDRV service{system};HLERequestContext ctx;
        auto closure=system.kernel.domain->TryClose(64);Require(bool(closure),"nvdrv init close failed");
        service.Initialize(ctx);
        Require(ctx.result==NvResult::Busy && service.nvdrv->container.opens==0 && !service.is_initialized,
            "closed nvdrv initialization published a session");
        Require(closure->ResumeNoEffects(),"nvdrv init resume failed");
        service.Initialize(ctx);service.Initialize(ctx);
        Require(ctx.result==NvResult::Success && service.is_initialized && service.session_id==5 && service.nvdrv->container.opens==1,
            "nvdrv initialization lost resume or duplicated session");
    }
    for(int stage=0;stage<2;++stage) {
        System system;NVDRV service{system};HLERequestContext ctx;
        std::promise<void> entered,release;auto began=entered.get_future();auto leave=release.get_future().share();
        auto pause=[&] {entered.set_value();leave.wait();};
        if(stage==0)service.nvdrv->container.on_open=pause;else ctx.on_reply=pause;
        auto work=std::async(std::launch::async,[&] {service.Initialize(ctx);});
        Require(began.wait_for(3s)==std::future_status::ready,"nvdrv initialization rendezvous timeout");
        auto closure=system.kernel.domain->TryClose(64);
        Require(closure && closure->Poll()==Common::ProducerDrain::Result::Pending,"active nvdrv initialization escaped drain");
        if(stage==1)closure->Quarantine();
        release.set_value();work.get();
        Require(closure->Poll()==(stage==1?Common::ProducerDrain::Result::Unknown:Common::ProducerDrain::Result::DrainedSubset),
            "nvdrv initialization return lost closure or quarantine");
        Require(ctx.result==NvResult::Success && service.is_initialized,"nvdrv initialization lost response");
    }
    {
        System system;NVDRV service{system};HLERequestContext ctx;
        service.nvdrv->container.on_open=[] {throw 42;};bool caught=false;
        try{service.Initialize(ctx);}catch(int){caught=true;}
        Require(caught && !system.kernel.domain->TryClose(64),"nvdrv initialization exception granted drain authority");
        Require(ctx.replies==0 && !service.is_initialized,"failed nvdrv initialization fabricated Success");
    }
}
'''
source = source.replace('int main() {', init_fixture + '\nint main() {InitializeTests();')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, value):
    path = out / (name + '.cpp')
    path.write_text(value)
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(path), '-o', str(exe)], check=True)
    return exe


exe = build('nvdrv-dispatch', source)
for i in range(a.repeat):
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    log = result.stdout + result.stderr
    (out / f'positive-{i}.log').write_text(log)
    assert result.returncode == 0 and 'PASS actual NVDRV' in log, log
print(f'PASS actual NVDRV dispatch ({a.repeat} repetitions)', flush=True)
controls = []
for name, body in methods.items():
    refusal = '    if (!dispatch) return ' + ('INVALID_NVDRV_FD;' if name == 'Open' else 'NvResult::Busy;')
    controls.append((name + '-omit-admission', source.replace(body, body.replace(refusal, '')),
                     'closed nvdrv dispatch performed effects'))
    marker = {'Open': '    auto opened = builder(fd);', 'Ioctl1': '    return itr->second->Ioctl1(',
              'Ioctl2': '    return itr->second->Ioctl2(', 'Ioctl3': '    return itr->second->Ioctl3(',
              'Close': '    itr->second->OnClose(fd);', 'QueryEvent': '    event = itr->second->QueryEvent(event_id);'}[name]
    controls.append((name + '-early-release', source.replace(body, body.replace(marker, '    dispatch.reset();\n' + marker)),
                     'active nvdrv dispatch escaped drain'))
    controls.append((name + '-omit-exception-seal', source.replace(body, body.replace(
        'if (std::uncaught_exceptions() > exception_depth) dispatch_domain->Seal();', '(void)exception_depth;')),
                     'nvdrv callback exception granted drain authority'))
refused = '    if (!initialization) {\n        ServiceError(ctx, NvResult::Busy);\n        return;\n    }'
controls += [
    ('Initialize-omit-admission', source.replace(initialize, initialize.replace(
        'auto initialization = domain->EnterCold();', 'static Common::ProducerDrain unrelated;auto initialization = unrelated.EnterCold();')),
     'closed nvdrv initialization published a session'),
    ('Initialize-early-release', source.replace(initialize, initialize.replace(
        '        session_id = container.OpenSession(', '        initialization.reset();\n        session_id = container.OpenSession(')),
     'active nvdrv initialization escaped drain'),
    ('Initialize-omit-exception-seal', source.replace(initialize, initialize.replace(
        'if (std::uncaught_exceptions() > exception_depth) domain->Seal();', '(void)exception_depth;')),
     'nvdrv initialization exception granted drain authority'),
    ('Initialize-success-on-exception', source.replace(initialize, initialize.replace(
        'if (std::uncaught_exceptions() > exception_depth) domain->Seal();',
        'if (std::uncaught_exceptions() > exception_depth) {domain->Seal();ServiceError(ctx, NvResult::Success);}')),
     'failed nvdrv initialization fabricated Success'),
]
for name, changed, expected in controls:
    assert changed != source
    mutant = build(name, changed)
    result = subprocess.run([str(mutant)], capture_output=True, text=True, timeout=20)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    assert result.returncode != 0 and 'FAIL ' + expected in log, (name, log)
    print('PASS NVDRV dispatch control: ' + name, flush=True)
paths = [cpp_path, interface_path, 'src/core/hle/service/nvdrv/nvdrv.h', 'src/common/producer_drain.h', 'src/common/scope_exit.h',
         'tools/physical_coherency_tests/run_nvdrv_dispatch_drain_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats': a.repeat, 'controls': [c[0] for c in controls],
    'actual': ['Module Open, Ioctl1/2/3, Close and QueryEvent bodies', 'NVDRV Initialize and ServiceError', 'ProducerDrain and ScopeGuard'],
    'fixtures': ['Module member layout, builders, devices and their queued/native work'],
    'boundary': 'Counted synchronous dispatch only; no escaped accessor, session teardown, native GPU or full Guest admission proof.'}, indent=2))
