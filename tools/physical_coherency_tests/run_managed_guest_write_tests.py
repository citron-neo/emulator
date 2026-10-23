# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Memory write facade admission across shared owner views."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
if a.repeat < 1:
    p.error('--repeat must be positive')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-managed-guest-writes').resolve()
out.mkdir(parents=True, exist_ok=True)
production = (root / 'src/core/memory.cpp').read_text()
names = ['Write'+w for w in ('8','16','32','64')]
names += ['WriteExclusive'+w for w in ('8','16','32','64','128')]
names += ['WriteBlock','WriteBlockUnsafe','CopyBlock','ZeroBlock']
names += ['WritePrepared'+w for w in ('8','16','32','64')]
names += ['WritePreparedCached'+w for w in ('8','16','32','64')]
bodies = {}
for name in names:
    match = re.search(r'(?:void|bool|PreparedCpuWriteResult)\s+Memory::'+name+r'\(', production)
    assert match, name
    pos = production.index('{', match.start())
    end, depth = pos + 1, 1
    while depth:
        depth += (production[end] == '{') - (production[end] == '}')
        end += 1
    bodies[name] = production[match.start():end].replace('Memory::','')
source = r'''
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
#include "core/managed_guest_writes.h"
using u8=unsigned char;using u16=unsigned short;using u32=unsigned;using u64=unsigned long long;
using u16_le=u16;using u32_le=u32;using u64_le=u64;using u128=std::array<u64,2>;
namespace Common {using ProcessAddress=u64;struct PhysicalRegistrationBundle {struct CachedWrite {};};}
using namespace std::chrono_literals;
void Require(bool value,const char* why) {if(!value) {std::cerr<<"FAIL "<<why<<"\n";std::cerr.flush();std::_Exit(91);}}
namespace Core::Memory {
enum class PreparedCpuWriteResult {Stored,Busy};struct PreparedCpuPage {};
struct Memory {
    ManagedGuestWrites managed_writes;
    struct Impl {
        std::atomic<int> effects=0;std::function<void()> hook;
        int Touch() {if(hook)hook();return ++effects;}
        template<class T>void Write(Common::ProcessAddress,T) {Touch();}
        template<class T>PreparedCpuWriteResult Write(const PreparedCpuPage&,Common::ProcessAddress,T) {Touch();return PreparedCpuWriteResult::Stored;}
        template<class T>PreparedCpuWriteResult WriteCached(const PreparedCpuPage&,Common::PhysicalRegistrationBundle::CachedWrite&,Common::ProcessAddress,T) {Touch();return PreparedCpuWriteResult::Stored;}
        template<class T>bool Exclusive(Common::ProcessAddress,T,T) {Touch();return true;}
        bool WriteExclusive8(Common::ProcessAddress a,u8 b,u8 c) {return Exclusive(a,b,c);}
        bool WriteExclusive16(Common::ProcessAddress a,u16 b,u16 c) {return Exclusive(a,b,c);}
        bool WriteExclusive32(Common::ProcessAddress a,u32 b,u32 c) {return Exclusive(a,b,c);}
        bool WriteExclusive64(Common::ProcessAddress a,u64 b,u64 c) {return Exclusive(a,b,c);}
        bool WriteExclusive128(Common::ProcessAddress a,u128 b,u128 c) {return Exclusive(a,b,c);}
        bool WriteBlock(Common::ProcessAddress,const void*,std::size_t) {Touch();return true;}
        bool WriteBlockUnsafe(Common::ProcessAddress,const void*,std::size_t) {Touch();return true;}
        bool CopyBlock(Common::ProcessAddress,Common::ProcessAddress,std::size_t) {Touch();return true;}
        bool ZeroBlock(Common::ProcessAddress,std::size_t) {Touch();return true;}
    };
    std::unique_ptr<Impl> impl=std::make_unique<Impl>();
    explicit Memory(std::shared_ptr<Common::ProducerDrain> domain):managed_writes{std::move(domain)} {}
''' + '\n'.join(bodies.values()) + r'''
};
}
using Core::Memory::Memory;
std::vector<std::function<bool(Memory&)>> Operations() {
    return {
'''
for name in names:
    if name.startswith('WritePreparedCached'):
        call = 'Core::Memory::PreparedCpuPage p;Common::PhysicalRegistrationBundle::CachedWrite c;return m.'+name+'(p,c,0,1)==Core::Memory::PreparedCpuWriteResult::Busy;'
    elif name.startswith('WritePrepared'):
        call = 'Core::Memory::PreparedCpuPage p;return m.'+name+'(p,0,1)==Core::Memory::PreparedCpuWriteResult::Busy;'
    elif name == 'WriteExclusive128':
        call = '(void)m.WriteExclusive128(0,u128{},u128{});'
    elif name.startswith('WriteExclusive'):
        call = '(void)m.'+name+'(0,1,0);'
    elif name in ('WriteBlock','WriteBlockUnsafe'):
        call = 'u8 data=1;(void)m.'+name+'(0,&data,1);'
    elif name == 'CopyBlock':
        call = '(void)m.CopyBlock(0,8,1);'
    elif name == 'ZeroBlock':
        call = '(void)m.ZeroBlock(0,1);'
    else:
        call = 'm.'+name+'(0,1);'
    source += '[](Memory& m) {'+call+('' if name.startswith('WritePrepared') else 'return false;')+'},\n'
source += r'''
    };
}
void ClosedViews() {
    auto domain=std::make_shared<Common::ProducerDrain>();Memory a{domain},b{domain};
    auto operations=Operations();for(auto& op:operations)op(a);
    Require(a.impl->effects==21,"healthy facade writes lost effects");
    auto closed=domain->TryClose(64);Require(bool(closed),"shared write domain failed close");
    Memory late{domain};
    for(auto* memory:{&a,&b,&late})for(auto& op:operations) {
        const auto before=memory->impl->effects.load();bool refused=false;
        try {refused=op(*memory);}catch(const Core::Memory::ManagedGuestWrites::Refused&) {refused=true;}
        Require(refused && memory->impl->effects==before,"closed managed write entered Impl or silently returned success");
    }
    Require(closed->Poll()==Common::ProducerDrain::Result::DrainedSubset,"closed write subset failed drain");
    Require(closed->ResumeNoEffects(),"drained write subset failed no-effects resume");
    for(auto& op:operations)op(b);
    Require(b.impl->effects==21,"resumed facade writes remained closed");
}
void Retained(int index) {
    auto domain=std::make_shared<Common::ProducerDrain>();Memory memory{domain};
    std::promise<void> entered,release;auto started=entered.get_future();auto released=release.get_future().share();
    memory.impl->hook=[&] {entered.set_value();released.wait();};auto operations=Operations();
    auto writer=std::async(std::launch::async,[&] {operations[index](memory);});
    Require(started.wait_for(3s)==std::future_status::ready,"write rendezvous timeout");
    auto closed=domain->TryClose(64);Require(bool(closed),"active write close failed");
    Require(closed->Poll()==Common::ProducerDrain::Result::Pending,"active managed store escaped drain");
    release.set_value();writer.get();
    Require(closed->Poll()==Common::ProducerDrain::Result::DrainedSubset,"finished managed store failed drain");
}
void ExceptionsAndContention() {
    auto domain=std::make_shared<Common::ProducerDrain>();Memory memory{domain};
    memory.impl->hook=[] {throw std::bad_alloc();};
    for(auto& op:Operations()) {bool threw=false;try {op(memory);}catch(const std::bad_alloc&) {threw=true;}
        Require(threw,"write exception swallowed");}
    auto closed=domain->TryClose(64);Require(closed && closed->Poll()==Common::ProducerDrain::Result::DrainedSubset,
        "write exception leaked managed entry");Require(closed->ResumeNoEffects(),"exception drain could not resume");
    memory.impl->hook={};std::vector<std::future<void>> workers;
    for(int i=0;i<8;++i)workers.push_back(std::async(std::launch::async,[&] {for(int j=0;j<1000;++j)memory.Write8(0,1);}));
    for(auto& worker:workers)worker.get();
    Require(memory.impl->effects==8000,"contention dropped ordinary managed store");
}
int main() {ClosedViews();for(int i=0;i<21;++i)Retained(i);ExceptionsAndContention();
    std::cout<<"PASS 21 actual Memory write facades share closure, retain entries and preserve contended stores\n";}
'''
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror','-I',str(root/'src')]


def build(name, text, extra=()):
    cpp = out/(name+'.cpp');cpp.write_text(text)
    exe = out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(flags+list(extra)+[str(cpp),'-o',str(exe)],check=True)
    return exe


exe=build('managed-writes',source)
for i in range(a.repeat):
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    log=result.stdout+result.stderr;(out/f'positive-{i}.log').write_text(log)
    assert result.returncode==0 and 'PASS 21 actual Memory' in log,log
print('PASS managed guest writes ('+str(a.repeat)+' repetitions)',flush=True)
nce_main=source.index('int main() {ClosedViews();')
nce_source=source[:nce_main]+r'''
int main() {
    auto domain=std::make_shared<Common::ProducerDrain>();Memory memory{domain};
    for(auto& operation:Operations())operation(memory);
    Require(memory.impl->effects==21,"unsupported NCE facade changed existing stores");
    Require(!domain->TryClose(64),"unsupported NCE build granted a managed write closure");
    std::cout<<"PASS NCE compile-time refusal preserves facade stores without tracked admission\n";
}
'''
nce=build('nce-unsupported',nce_source,['-DHAS_NCE'])
result=subprocess.run([str(nce)],capture_output=True,text=True,timeout=30)
(out/'nce-unsupported.log').write_text(result.stdout+result.stderr)
assert result.returncode==0,result.stdout+result.stderr
print('PASS NCE unsupported-domain branch',flush=True)
for name in names:
    guard=('    auto producer = managed_writes.TryEnter();\n'
           '    if constexpr (ManagedGuestWrites::TrackingSupported()) {\n'
           '        if (!producer) return PreparedCpuWriteResult::Busy;\n    }') if name.startswith('WritePrepared') else '    auto producer = managed_writes.Enter();'
    assert bodies[name].count(guard)==1
    changed=source.replace(bodies[name],bodies[name].replace(guard,''))
    assert changed!=source
    mutant=build('omit-'+name,changed)
    result=subprocess.run([str(mutant)],capture_output=True,text=True,timeout=30)
    log=result.stdout+result.stderr;(out/('omit-'+name+'.log')).write_text(log)
    assert result.returncode!=0 and 'FAIL closed managed write entered Impl or silently returned success' in log,(name,log)
    print('PASS managed store control: '+name,flush=True)
paths=['src/core/memory.cpp','src/core/memory.h','src/core/managed_guest_writes.h',
       'src/core/hle/kernel/kernel.cpp','src/core/hle/kernel/kernel.h','src/common/producer_drain.h',
       'tools/physical_coherency_tests/run_managed_guest_write_tests.py']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats':a.repeat,'controls':names,'actual':['21 complete Memory facade write methods','ManagedGuestWrites and ProducerDrain'],
    'fixtures':['Impl stores/callbacks and facade construction using an explicitly shared domain'],
    'boundary':'Managed API effects only; no NCE/fastmem/raw/GPU or whole service exclusion, no Guest admission.'},indent=2))
