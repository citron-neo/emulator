#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual roster, Open/Close and scoped references; injected slab/Finalize collaborators."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/m4-process-owner-roster'
stub = out / 'stubs'
(stub / 'core/hle/kernel').mkdir(parents=True, exist_ok=True)
(stub / 'common').mkdir(parents=True, exist_ok=True)
(stub / 'common/assert.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#define ASSERT(x) assert(x)
#define UNIMPLEMENTED() std::abort()
#define UNREACHABLE() std::abort()
''')
(stub / 'core/hle/kernel/kernel.h').write_text('''#pragma once
#include <unordered_map>
#include "core/hle/kernel/process_owner_roster.h"
namespace Kernel {
class KernelCore {
public:
 ProcessOwnerRoster roster;
 std::unordered_map<const KAutoObject*,unsigned> unregister_calls;
 void RegisterKernelObject(KAutoObject*) {}
 void UnregisterKernelObject(KAutoObject* p) {++unregister_calls[p];}
 unsigned UnregisterCount(const KAutoObject* p) const {
   const auto it=unregister_calls.find(p);return it==unregister_calls.end()?0:it->second;
 }
 bool IsObjectListContainerValid() const {return false;}
 struct Container {void Unregister(KAutoObject*) {}} container;
 Container& ObjectListContainer() {return container;}
 u64 RegisterProcessOwner(KProcess* p) {return roster.Register(p);}
 void BeginProcessOwnerRetirement(u64 id) {roster.BeginRetirement(id);}
 void CompleteProcessOwnerRetirement(u64 id) {roster.CompleteRetirement(id);}
};}
''')
process_stub = '''#pragma once
#include <functional>
#include <utility>
#include "core/hle/kernel/kernel.h"
namespace Kernel {
class KWorkerTask : public KAutoObject {
public: explicit KWorkerTask(KernelCore& k):KAutoObject{k} {}
};
template<class Derived,class Base> class KAutoObjectWithSlabHeapAndContainer : public Base {
 static void Free(KernelCore&,Derived* object) {object->FreeForTest();}
public:
 explicit KAutoObjectWithSlabHeapAndContainer(KernelCore& k):Base{k} {}
 bool IsInitialized() const {return true;}
 uintptr_t GetPostDestroyArgument() const {return 0;}
 static void PostDestroy(uintptr_t) {}
 @SLAB_DESTROY@
};
class KProcess : public KAutoObjectWithSlabHeapAndContainer<KProcess,KWorkerTask> {
 struct OwnerRegistration {
   KernelCore& kernel;u64 id{};~OwnerRegistration();
 };
 OwnerRegistration m_owner_registration;
public:
 bool finalized{};
 std::function<void()> on_finalize,on_free,before_open,on_member_destroy;
 struct MemberCleanup {
   std::function<void()>* callback;
   ~MemberCleanup() {if(*callback) (*callback)();}
 } cleanup{&on_member_destroy};
 explicit KProcess(KernelCore& k,bool open=true):KAutoObjectWithSlabHeapAndContainer{k},m_owner_registration{k} {
   m_owner_registration.id=m_kernel.RegisterProcessOwner(this);
   if(open) KAutoObject::Create(this);
 }
 ~KProcess() override;
 void Destroy() override;
 bool Open() {if(before_open) before_open();return KAutoObject::Open();}
 void Finalize() override {if(on_finalize) on_finalize();finalized=true;}
 void FreeForTest() {if(on_free) on_free();}
 void AdoptReplacementForTest(u64 id) {m_owner_registration.id=id;}
};}
'''


def extract(marker, path='src/core/hle/kernel/k_process.cpp', after=None):
    text = (root / path).read_text()
    start = text.index(marker, text.index(after) if after else 0)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


slab_destroy = extract('virtual void Destroy() override', 'src/core/hle/kernel/slab_helpers.h',
                       'class KAutoObjectWithSlabHeapAndContainer')
(stub / 'core/hle/kernel/k_process.h').write_text(process_stub.replace('@SLAB_DESTROY@', slab_destroy))
destroy = extract('void KProcess::Destroy()')
destructor = extract('KProcess::~KProcess()')
registration_destructor = extract('KProcess::OwnerRegistration::~OwnerRegistration()')
lifecycle_source = '#include "core/hle/kernel/k_process.h"\nnamespace Kernel {\n' + \
                   destroy + '\n' + destructor + '\n' + registration_destructor + '\n}\n'
path = 'src/core/hle/kernel/process_owner_roster.cpp'
source = (root / path).read_text()
test_path = 'tools/physical_coherency_tests/process_owner_roster_tests.cpp'
command = [os.environ.get('CXX', 'g++'), '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-I', str(stub), '-I', str(root / 'src')]
mutants = [
    ('ignore-retiring-owner', source.replace('        if (record.retiring) return std::nullopt;',
                                            '        if (record.retiring) continue;'),
     'retiring owner must refuse capture'),
    ('omit-registration-revision', source.replace('    state->records.push_back({id, process});\n    state->Changed();',
                                                '    state->records.push_back({id, process});'),
     'new owner must invalidate prior inventory'),
    ('erase-reused-address', source.replace(
        '    if (std::erase_if(state->records, [id](const auto& record) { return record.id == id; }) != 1)',
        '    const auto old = std::ranges::find(state->records, id, &Record::id);\n'
        '    const auto pointer = old == state->records.end() ? nullptr : old->process;\n'
        '    if (std::erase_if(state->records, [pointer](const auto& record) { return record.process == pointer; }) != 1)'),
     'exact retirement must preserve replacement registration'),
]
ref_path = 'src/core/hle/kernel/k_auto_object.h'
references = (root / ref_path).read_text()
variants = [('production', source, references, None)] + [
    (name, body, references, expected) for name, body, expected in mutants]
variants.append(('omit-owner-pin', source, references.replace('        owner.m_obj = object;',
                 '        owner.m_obj = nullptr; object->Close();'), 'snapshot must own real references'))
lifecycle_mutants = {
    'unconditional-destructor-unregister': (lifecycle_source.replace(destructor,
        'KProcess::~KProcess() {\n'
        '    if (m_owner_registration.id) m_kernel.BeginProcessOwnerRetirement(m_owner_registration.id);\n'
        '    m_kernel.UnregisterKernelObject(this);\n}'),
        'Close must own the only unregister after consumed registration'),
    'omit-direct-destructor-unregister': (lifecycle_source.replace(
        '        m_kernel.UnregisterKernelObject(this);', ''),
        'direct C++ destruction must unregister exactly once'),
}
variants += [(name, source, references, expected) for name, (_, expected) in lifecycle_mutants.items()]
for name, body, refs, expected in variants:
    life = lifecycle_mutants[name][0] if name in lifecycle_mutants else lifecycle_source
    assert expected is None or body != source or refs != references or life != lifecycle_source
    variant = out / 'headers' / name / 'core/hle/kernel'
    variant.mkdir(parents=True, exist_ok=True)
    (variant / 'k_auto_object.h').write_text(refs)
    cpp = out / (name + '.cpp')
    cpp.write_text(body)
    lifecycle = out / (name + '-lifecycle.cpp')
    lifecycle.write_text(life)
    exe = out / (name + '.bin')
    subprocess.run(command[:1] + ['-I', str(out / 'headers' / name)] + command[1:] + [str(cpp), str(lifecycle), str(root / test_path),
                   str(root / 'src/core/hle/kernel/k_auto_object.cpp'), '-o', str(exe)], check=True)
    for repeat in range(1, 4 if expected is None else 2):
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        log = result.stdout + result.stderr
        (out / f'{name}-{repeat}.txt').write_text(log)
        if expected is None:
            assert result.returncode == 0 and 'PASS process owner roster:' in log, log
        else:
            assert result.returncode != 0 and expected in log and 'assert' in log.lower(), log
    print('PASS ' + name, flush=True)
paths = [path, 'src/core/hle/kernel/process_owner_roster.h',
         'src/core/hle/kernel/k_auto_object.h', 'src/core/hle/kernel/k_auto_object.cpp',
         'src/core/hle/kernel/k_process.cpp', 'src/core/hle/kernel/k_process.h',
         'src/core/hle/kernel/kernel.cpp', 'src/core/hle/kernel/kernel.h', test_path,
         'src/core/hle/kernel/slab_helpers.h', 'src/core/hle/kernel/k_slab_heap.h',
         'src/core/CMakeLists.txt', 'src/tests/core/controlled_cpu_scheduler.cpp',
         '.github/workflows/pr375-pr380-integration.yml',
         'tools/physical_coherency_tests/run_process_owner_roster_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'positive_repeats': 3, 'assertion_mutants': [item[0] for item in variants[1:]],
    'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
    'boundary': 'Actual roster, scoped refs, Open/Close and KProcess Destroy/destructor; '
                'injected slab/Finalize collaborators, no writer closure or guest admission',
}, indent=2) + '\n')
