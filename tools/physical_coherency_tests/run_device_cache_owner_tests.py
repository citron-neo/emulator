#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual ASID cache routing plus real HostMemory/PTEs; injected translation layout."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[2]
out=root/'.diagnostics/m4-device-cache-owner';out.mkdir(parents=True,exist_ok=True)
prerequisite=root/'.diagnostics/m4-mapping-publication/production.cpp'
assert prerequisite.exists(),'Run run_mapping_publication_tests.py first'
head=prerequisite.read_text().split('int main() {',1)[0]
counter_path='src/core/device_memory_manager.inc'
methods_path='src/video_core/host1x/gpu_device_memory_manager.cpp'
def extract(text,marker):
    start=text.index(marker);opening=text.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
counter=extract((root/counter_path).read_text(),'void DeviceMemoryManager<Traits>::UpdatePagesCachedCount(')
counter='template<class Traits>\n'+counter
methods=extract((root/methods_path).read_text(),'struct MaxwellDeviceMethods')+';'
forward='        interface->RasterizerMarkRegionCached(address, size, caching);'
assert methods.count(forward)==1
methods=methods.replace(forward,'        RecordCacheTarget(interface, address, size, caching);\n'+forward)
test='tools/physical_coherency_tests/device_cache_owner_tests.cpp.in'
source=head+(root/test).read_text().replace('// @DEVICE_METHODS@',methods).replace('// @COUNTER_BODY@',counter)
boundary=('        if (asid.id != asid_2.id) [[unlikely]] {\n'
          '            release_pending();\n            asid = asid_2;\n'
          '            memory_device_inter = registered_processes[asid_2.id];')
assert source.count(boundary)==1
late_owner=boundary.replace('            release_pending();\n','')+'\n            release_pending();'
variants=[('production',source,None),
    ('stale-asid-on-return',source.replace('            asid = asid_2;\n',''),'cache publication must target the exact process'),
    ('switch-owner-before-flush',source.replace(boundary,late_owner),'cache publication must target the exact process'),
    ('omit-owner-boundary-flush',source.replace(boundary,boundary.replace('            release_pending();\n','')),
     'cache publication must target the exact process')]
stubs=root/'.diagnostics/m4-mapping-publication/stubs'
command=[os.environ.get('CXX','g++'),'-std=c++23','-O1','-pthread','-Wall','-Wextra','-Werror',
    '-Wno-unused-parameter','-I',str(stubs),'-I',str(root/'src')]
command += [str(root/p) for p in ['src/common/host_memory.cpp','src/common/page_table.cpp','src/common/virtual_buffer.cpp']]
for name,text,expected in variants:
    assert expected is None or text!=source
    cpp=out/(name+'.cpp');cpp.write_text(text);exe=out/name
    subprocess.run(command+[str(cpp),'-o',str(exe)],check=True)
    for repeat in range(1,2 if expected else 4):
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
        log=result.stdout+result.stderr;(out/f'{name}-{repeat}.txt').write_text(log)
        if expected: assert result.returncode!=0 and 'assert' in log.lower() and expected in log,(name,log)
        else: assert result.returncode==0 and 'PASS device cache owner routing:' in log,(name,log)
    print('PASS '+name,flush=True)
paths=[counter_path,methods_path,'src/core/device_memory_manager.h','src/core/memory.cpp',
    'src/common/range_mutex.h','src/common/host_memory.cpp','src/common/page_table.cpp',
    'src/core/controlled_mapping_publication.h',test,
    'tools/physical_coherency_tests/run_device_cache_owner_tests.py',
    'tools/physical_coherency_tests/run_mapping_publication_tests.py',
    'tools/physical_coherency_tests/run_cpu_mapping_tests.py','.github/workflows/pr375-pr380-integration.yml']
(out/'extraction.json').write_text(json.dumps({'positive_repeats':3,
    'assertion_mutants':[v[0] for v in variants[1:]],
    'sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'boundary':'Real cache/PTE/HostMemory publication and production ASID routing body; injected translations, no process lifetime or GPU quiescence proof'},indent=2)+'\n')
