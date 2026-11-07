# SPDX-License-Identifier: GPL-2.0-or-later
"""Real preallocated retirement queue, allocation-failure and race witnesses."""
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
out=(a.output_dir or root/'.diagnostics/m4-owned-retirement').resolve();out.mkdir(parents=True,exist_ok=True)
header_path='src/common/owned_retirement.h';fixture='tools/physical_coherency_tests/owned_retirement_tests.cpp.in'
header=(root/header_path).read_text();source=(root/fixture).read_text()
flags=[a.cxx,'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror','-Wno-mismatched-new-delete']


def run(name,value,expected=None):
    stubs=out/name/'common';stubs.mkdir(parents=True,exist_ok=True);(stubs/'owned_retirement.h').write_text(value)
    cpp=out/(name+'.cpp');cpp.write_text(source);exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run(flags+['-I',str(stubs.parent),'-I',str(root/'src'),str(cpp),'-o',str(exe)],check=True)
    for i in range(a.repeat if expected is None else 1):
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15);log=result.stdout+result.stderr
        (out/f'{name}-{i}.log').write_text(log)
        assert (result.returncode==0 and 'PASS owned retirement' in log) if expected is None else (result.returncode!=0 and 'FAIL '+expected in log),(name,log)
    print('PASS owned retirement: '+name,flush=True)


run('production',header)
controls=[
    ('discard-closed',header.replace('            Keep(std::move(record), failed.load(std::memory_order_acquire));','            record.reset();'),
     'closed retirement discarded owners or executed effects'),
    ('replay-closed',header.replace('            if (!entry) { Keep(std::move(record), false); return false; }',''),
     'closed retirement replayed effects'),
    ('discard-unknown',header.replace('if (record) KeepLocked(std::move(record), true);','record.reset();'),
     'uncertain retirement released owners'),
    ('omit-failure-seal',header.replace('        domain->Seal();',''),
     'uncertain retirement granted admission'),
    ('ignore-throw',header.replace('// A callback may have partially retired owners. Never replay it.','return true;'),
     'uncertain retirement released owners'),
    ('release-entry-first',header.replace('        record.reset(); // Release captures before the counted entry leaves.',
        '        entry.reset();\n        record.reset();'),
     'final owner release escaped retirement entry'),
    ('older-success-ignores-failure',header.replace('if (!failed.load(std::memory_order_acquire)) return true;','return true;'),
     'older retirement success released newer quarantine owners'),
]
for name,value,expected in controls:
    assert value!=header;run(name,value,expected)
paths=[header_path,fixture,'src/common/producer_drain.h','tools/physical_coherency_tests/run_owned_retirement_tests.py']
(out/'extraction.json').write_text(json.dumps({'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'positive_repeats':a.repeat,'controls':[c[0] for c in controls],
    'boundary':'Actual queue and ProducerDrain; fixture owner/callback, no native device completion or trusted terminal cleanup.'},indent=2))
