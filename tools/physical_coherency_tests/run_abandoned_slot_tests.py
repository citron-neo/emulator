# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual warm layer/consumer destruction and escaped producer slot-publication bodies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--output-dir', type=Path)
p.add_argument('--baseline', action='store_true')
p.add_argument('--revision')
a = p.parse_args()
if a.repeat < 1 or (a.revision and not a.baseline):
    p.error('positive repeat; revision requires baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-abandoned-slot').resolve()
out.mkdir(parents=True, exist_ok=True)
inputs = set()


def read(path):
    inputs.add(path)
    return (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root).decode()
            if a.revision else (root / path).read_text(encoding='utf-8')).replace('\r\n', '\n')


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


base = 'src/core/hle/service/nvnflinger/'
producer = block(read(base + 'buffer_queue_producer.cpp'), 'Status BufferQueueProducer::SetPreallocatedBuffer(')
consumer = '\n'.join(block(read(base + file), marker) for file, marker in [
    ('buffer_queue_consumer.cpp', 'Status BufferQueueConsumer::Disconnect()'),
    ('buffer_queue_core.cpp', 'void BufferQueueCore::FreeBufferLocked('),
    ('buffer_queue_core.cpp', 'void BufferQueueCore::FreeAllBuffersLocked()'),
    ('buffer_queue_core.cpp', 's32 BufferQueueCore::GetPreallocatedBufferCountLocked() const'),
    ('consumer_base.cpp', 'void ConsumerBase::Abandon()'),
    ('consumer_base.cpp', 'void ConsumerBase::AbandonLocked()'),
    ('consumer_base.cpp', 'void ConsumerBase::FreeBufferLocked(')])
surface = '\n'.join(block(read(base + 'surface_flinger.cpp'), marker) for marker in [
    'void SurfaceFlinger::DestroyLayer(', 'void SurfaceFlinger::DestroyBufferQueue(', 'void SurfaceFlinger::RemoveDisplay('])
cache = '\n'.join(block(read(base + 'hardware_composer.cpp'), marker) for marker in [
    'void HardwareComposer::RemoveLayerLocked(', 'bool HardwareComposer::TryAcquireFramebufferLocked(',
    'HardwareComposer::CacheStatus HardwareComposer::CacheFramebufferLocked('])
graphic = read(base + 'ui/graphic_buffer.cpp')
graphic = graphic[graphic.index('namespace Service::android {'):]
factory = block(read('src/core/hle/service/nvdrv/nvdrv.cpp'), 'Common::OwnedRetirementQueue::Owner Module::PrepareGraphicBufferRetirement(')
fixture = 'tools/physical_coherency_tests/abandoned_slot_fixture.cpp.in'
source = (root / fixture).read_text(encoding='utf-8')
for key, value in {'PRODUCER': producer, 'CONSUMER': consumer, 'SURFACE': surface,
                   'CACHE': cache, 'GRAPHIC': graphic, 'FACTORY': factory,
                   'LAYER_DESTRUCTOR': block(read(base + 'display.h'), '    ~Layer()')}.items():
    source = source.replace('@' + key + '@', value)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    path = out / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    child = subprocess.run(flags + [str(path), '-o', str(exe)], capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(child.stdout + child.stderr, encoding='utf-8')
    if child.returncode:
        raise RuntimeError(child.stderr)
    return exe


def run(exe, label, case, witness=None):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (label + '.log')).write_text(log, encoding='utf-8')
    assert (child.returncode == 91 and 'FAIL ' + witness in log) if witness else (
        child.returncode == 0 and 'PASS abandoned slot ' + case in log), (label, child.returncode, log)


exe = build('production', source)
if a.baseline:
    witnesses = [('closed', 'borrowed producer republished abandoned slot'),
                 ('late', 'prepared candidate published after abandonment')]
    for case, message in witnesses:
        run(exe, 'baseline-' + case, case, message)
        print('REPRODUCED ' + message, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'witnesses': witnesses}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['closed', 'late', 'deferred', 'pin-zero', 'pin-throw', 'free-failure', 'normal', 'bad-slot', 'global-close', 'cache', 'uncached', 'cached-late']
for i in range(a.repeat):
    for case in cases:
        run(exe, f'{case}-{i}', case)
print(f'PASS twelve abandoned-slot paths ({a.repeat} repetitions)', flush=True)
gate = '    if (core->is_abandoned) return Status::NoInit;'
assert producer.count(gate) == 2
controls = [
    ('omit-both-gates', source.replace(producer, producer.replace(gate, '')), 'closed', 'borrowed producer republished abandoned slot'),
    ('omit-late-gate', source.replace(producer, ''.join(producer.rsplit(gate, 1))), 'late', 'prepared candidate published after abandonment'),
    ('omit-preflight', source.replace(producer, producer.replace(gate, '', 1)), 'closed', 'closed queue acquired new backing effects'),
    ('success-on-refusal', source.replace(producer, producer.replace('return Status::NoInit;', 'return Status::NoError;')), 'closed', 'borrowed producer republished abandoned slot'),
    ('hold-core-over-preparation', source.replace(producer, producer.replace('    {\n        std::scoped_lock lock{core->mutex};', '    std::scoped_lock lock{core->mutex};\n    {', 1).replace('    std::scoped_lock lock{core->mutex};\n\n', '    /* lock already held */\n\n')), 'late', 'core lock held across backing preparation'),
    ('direct-retirement', source.replace('        queue->Retire(std::move(m_retirement));', '        m_retirement.reset();'), 'deferred', 'cancelled candidate lost retained backing owners'),
    ('ignore-free-failure', source.replace('return file.FreeHandle(handle->id, true).has_value();', 'file.FreeHandle(handle->id, true); return true;'), 'free-failure', 'failed candidate retirement granted success authority'),
]
# A deliberately broken cached-buffer hit that releases its strong owner.
old = '    if (it != m_framebuffers.end()) {'
controls.append(('drop-cached-framebuffer', source.replace(cache, cache.replace(old, old + '\n        it->second.item.graphic_buffer.reset();', 1)), 'cache', 'cached framebuffer failed to retain original backing'))
prefix, suffix = producer.rsplit(gate, 1)
controls.append(('retire-under-core-lock', source.replace(producer, prefix + gate.replace('return Status::NoInit;', '{ graphic_buffer.reset(); return Status::NoInit; }') + suffix), 'late', 'candidate retirement ran under core lock'))
controls.append(('clear-before-final-check', source.replace(producer, prefix + '    slots[slot] = {};\n' + gate + suffix), 'late', 'refused candidate changed queue metadata'))
for name, changed, case, message in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for i in range(a.repeat):
        run(mutant, f'{name}-{i}', case, message)
    print('PASS abandoned-slot control: ' + name, flush=True)
inputs.update([fixture, 'tools/physical_coherency_tests/run_abandoned_slot_tests.py',
               base + 'ui/graphic_buffer.h', 'src/common/owned_retirement.h', 'src/common/producer_drain.h'])
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(inputs)},
    'repetitions': a.repeat, 'cases': cases, 'controls': [c[0] for c in controls],
    'actual': ['SetPreallocatedBuffer', 'ConsumerBase Abandon and slot clearing; BufferQueueConsumer Disconnect and core frees',
               'SurfaceFlinger warm removal methods; Layer destructor', 'GraphicBuffer construction/retirement and Module prepared factory',
               'HardwareComposer cache/acquire/remove methods; ProducerDrain/OwnedRetirementQueue'],
    'fixtures': ['Queue/provider/container layouts, binder registry, cache map type and acquisition',
                 'nvmap effects, Process/backing tokens, event signaling and native-drain marker'],
    'boundary': 'Local abandoned queue publication and CPU ownership only; no actual Memory/KProcess, native GPU completion or whole guest admission.'
}, indent=2), encoding='utf-8')
