"""Managed entry closure: real host threads; no ARM/NCE or full guest admission proof."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/m4-producer-drain';out.mkdir(parents=True,exist_ok=True)
hp='src/common/producer_drain.h';tp='tools/physical_coherency_tests/producer_drain_tests.cpp'
header=(root/hp).read_text().replace('#pragma once','');source=(root/tp).read_text()
inc='#include "common/producer_drain.h"'
cmd=[os.environ.get('CXX','g++'),'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Wshadow','-Werror','-I',str(root/'src')]
def run(name,body,positive=False):
    path=out/(name+'.cpp');path.write_text(body);exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(cmd+[str(path),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
    (out/(name+'.txt')).write_text(result.stdout+result.stderr)
    if positive:
        assert result.returncode==0,result.stderr
        print(result.stdout,end='')
        for repeat in range(1,4):
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
            (out/f'{name}-repeat-{repeat}.txt').write_text(result.stdout+result.stderr)
            assert result.returncode==0,result.stderr
    else:
        assert result.returncode!=0 and 'Assertion' in result.stderr,(name,result.returncode,result.stderr)
        print('PASS expected drain assertion mutant:',name)
mutants=[
 ('open-after-close','if((value & (Closed|Unknown)) || (value & Count)==Count)','if((value & Count)==Count)'),
 ('false-drain','if(!(value & Count))','if(true)'),
 ('early-resume','u64 expected=Closed;','u64 expected=state->value.load();'),
 ('drop-reopens','~Closure(){if(state && !released) Quarantine();}','~Closure(){if(state && !released) state->value.fetch_and(~(Closed|Unknown));}'),
 ('ignore-unknown','if(released || (value & Unknown))','if(released)'),
 ('timeout-is-drained','if(!remaining || !--remaining){Quarantine();return Result::Unknown;}','if(!remaining || !--remaining){return Result::DrainedSubset;}'),
 ('duplicate-close','if(old & (Closed|Unknown))','if((static_cast<void>(old),false))'),
 ('retired-reopens','~ProducerDrain(){Seal();}','~ProducerDrain(){}'),
 ('stale-release','if(released) return false;','if(released){state->value.store(0);return true;}'),
 ('missing-alias-certificate','static constexpr Result GuestBackingAdmission() noexcept {return Result::Unknown;}','static constexpr Result GuestBackingAdmission() noexcept {return Result::DrainedSubset;}'),
]
# Deterministic red before positive: counting no active writers must fail inflight tests.
run('baseline-no-drain-red',source.replace(inc,header.replace(mutants[1][1],mutants[1][2])))
run('logic',source,True)
for name,guard,replacement in mutants:
    assert header.count(guard)==1,name
    run(name,source.replace(inc,header.replace(guard,replacement)))
def body(path,marker):
    text=(root/path).read_text();start=text.index('{',text.index(marker));depth=1;end=start+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
entry=body('src/core/hle/kernel/physical_core.cpp','void PhysicalCore::RunThread(')
start=entry.index('const auto execution = guest_execution.TryEnter();')
entry=entry[start:entry.index('// If we were interrupted, exit immediately.',start)]
factory=body('src/core/hle/kernel/kernel.cpp','KernelCore::TryCloseGuestExecution(')
visit=body('src/core/controlled_cpu_page.h','bool TryVisit(Visit&& visit) const')
fixture=(root/'tools/physical_coherency_tests/producer_drain_integration.cpp.in').read_text()
fixture=fixture.replace('@GUEST_ENTRY@',entry).replace('@KERNEL_BODY@',factory).replace('@VISIT_BODY@',visit)
run('integration',fixture,True)
integration_mutants=[
 ('backend-bypasses-close','if (!execution) return;',''),
 ('host-bypasses-close','if(!execution) return false;',''),
 ('omitted-core','i<drain.cores.size()','i+1<drain.cores.size()'),
 ('early-guest-cancel','if(Poll()!=Result::DrainedSubset) return false;','if(Poll()==Result::Unknown) return false;'),
 ('unknown-not-fanned-out','if(result==Result::Unknown){Quarantine();return Result::Unknown;}','if(result==Result::Unknown){return Result::Unknown;}'),
]
guest_header=(root/'src/core/hle/kernel/guest_execution_drain.h').read_text().replace('#pragma once','')
fixture=fixture.replace('#include "core/hle/kernel/guest_execution_drain.h"',guest_header)
for name,guard,replacement in integration_mutants:
    assert fixture.count(guard)==1,name
    run(name,fixture.replace(guard,replacement))
paths=[hp,tp,'tools/physical_coherency_tests/run_producer_drain_tests.py',
       'src/core/hle/kernel/guest_execution_drain.h','src/core/hle/kernel/physical_core.cpp',
       'src/core/hle/kernel/physical_core.h','src/core/hle/kernel/kernel.cpp','src/core/hle/kernel/kernel.h',
       'src/core/controlled_cpu_page.h','src/core/memory.h','tools/physical_coherency_tests/producer_drain_integration.cpp.in']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
 'mutants':len(mutants)+len(integration_mutants),'boundary':'Real managed host threads, extracted entry/factory/visit bodies with mock backend and Linux alias bypass counterexample; no guest backing or asynchronous GPU lease'},indent=2))
