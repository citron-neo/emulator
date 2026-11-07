# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual VI capture body: closed/raw/partial and pool-publication interleavings."""
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
    p.error('positive repeat required; --revision requires --baseline')
root = Path(__file__).resolve().parents[2]
out = (a.output_dir or root / '.diagnostics/m4-vi-capture').resolve()
out.mkdir(parents=True, exist_ok=True)
path = 'src/core/hle/service/vi/shared_buffer_manager.cpp'
raw = (subprocess.check_output(['git', 'show', a.revision + ':' + path], cwd=root)
       if a.revision else (root / path).read_bytes())
production = raw.decode('utf-8').replace('\r\n', '\n')


def method(text, marker):
    start = text.index(marker)
    pos = text.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


capture = method(production, 'Result SharedBufferManager::WriteAppletCaptureBuffer(')
header_path = 'src/core/hle/service/nvdrv/nvdrv.h'
header = (root / header_path).read_text(encoding='utf-8')
module = method(header, '    auto TryEnterDispatch()') + '\n' + method(header, '    void QuarantineDispatch()')
fixture_path = 'tools/physical_coherency_tests/vi_capture_fixture.cpp.in'
source = (root / fixture_path).read_text(encoding='utf-8').replace('// @MODULE_METHODS@', module).replace('// @CAPTURE_METHOD@', capture)
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src')]


def build(name, text):
    cpp = out / (name + '.cpp')
    cpp.write_text(text, encoding='utf-8')
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    return exe


def run(exe, name, case):
    child = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=15)
    log = child.stdout + child.stderr
    (out / (name + '.log')).write_text(log, encoding='utf-8')
    return child.returncode, log


exe = build('production', source)
if a.baseline:
    witnesses = {}
    for case, expected in [('closed', 'closed VI capture performed effects'),
                           ('callback', 'active VI capture escaped drain'),
                           ('empty', 'empty capture cleared destination or reported success'),
                           ('publication', 'capture escaped pool publication')]:
        code, log = run(exe, 'baseline-' + case, case)
        assert code == 91 and 'FAIL ' + expected in log, (case, code, log)
        witnesses[case] = log.strip()
        print('REPRODUCED ' + expected, flush=True)
    (out / 'baseline.json').write_text(json.dumps({'revision': a.revision, 'source_sha256': hashlib.sha256(raw).hexdigest(),
                                                  'witnesses': witnesses}, indent=2), encoding='utf-8')
    raise SystemExit(0)
cases = ['closed', 'callback', 'raw', 'tail', 'empty', 'missing', 'publication', 'partial']
for repetition in range(a.repeat):
    for case in cases:
        code, log = run(exe, f'{case}-{repetition}', case)
        assert code == 0 and 'PASS VI capture ' + case in log, (case, code, log)
print(f'PASS eight actual VI capture paths ({a.repeat} repetitions)', flush=True)
controls = [
    ('unrelated-admission', source.replace('auto dispatch = m_nvdrv->TryEnterDispatch();',
        'static Common::ProducerDrain unrelated; auto dispatch = unrelated.EnterCold();'),
     'closed', 'closed VI capture performed effects'),
    ('release-before-capture', source.replace('    std::vector<u8> capture_buffer(',
        '    dispatch.reset();\n    std::vector<u8> capture_buffer('),
     'callback', 'active VI capture escaped drain'),
    ('release-before-raw', source.replace('    for (auto& block : *page_group)',
        '    dispatch.reset();\n    for (auto& block : *page_group)'),
     'raw', 'active VI capture escaped drain'),
    ('release-before-projection', source.replace('        m_system.GPU().Host1x().MemoryManager().ApplyOpOnPointer(',
        '        dispatch.reset();\n        m_system.GPU().Host1x().MemoryManager().ApplyOpOnPointer('),
     'tail', 'active VI capture escaped drain'),
    ('ignore-empty', source.replace('    R_UNLESS(!capture_buffer.empty(), VI::ResultOperationFailed);', ''),
     'empty', 'empty capture cleared destination or reported success'),
    ('omit-exception-seal', source.replace('if (std::uncaught_exceptions() > exception_depth) m_nvdrv->QuarantineDispatch();',
        '(void)exception_depth;'),
     'partial', 'partial capture exception became clean drain'),
    ('omit-publication-lock', source.replace('        std::scoped_lock lk{m_guard};', '        (void)m_guard;'),
     'publication', 'capture escaped pool publication'),
    ('hold-lock-across-gpu', source.replace('    {\n        std::scoped_lock lk{m_guard};',
        '    std::scoped_lock lk{m_guard};\n    {'),
     'callback', 'GPU wait retained pool publication lock'),
]
for name, changed, case, expected in controls:
    assert changed != source, name
    mutant = build(name, changed)
    for repetition in range(a.repeat):
        code, log = run(mutant, f'{name}-{repetition}', case)
        assert code == 91 and 'FAIL ' + expected in log, (name, code, log)
    print('PASS VI capture control: ' + name, flush=True)
inputs = [path, header_path, fixture_path, 'src/common/producer_drain.h', 'src/common/scope_exit.h',
          'tools/physical_coherency_tests/run_vi_capture_drain_tests.py']
(out / 'extraction.json').write_text(json.dumps({
    'sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in inputs},
    'repetitions': a.repeat, 'cases': cases, 'controls': [control[0] for control in controls],
    'actual': ['WriteAppletCaptureBuffer body', 'Module admission/seal methods', 'ProducerDrain/ScopeGuard'],
    'fixtures': ['GPU capture result and projection, physical bytes/GetPointer, page-group layout',
                 'publisher holding real underlying mutex, not actual CreateSession'],
    'boundary': 'Counted synchronous raw-capture subset only; no mapping-use pin, F2 preservation, native visibility or whole Guest admission.'
}, indent=2), encoding='utf-8')
