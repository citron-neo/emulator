# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone C++20 M2 model, no production build/dependencies."""
import argparse
import os
import re
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
p.add_argument('--sanitize', action='store_true')
p.add_argument('--seed', type=int, default=372)
p.add_argument('--steps', type=int, default=400)
p.add_argument('--seeds', type=int, default=8)
p.add_argument('--mutation-check', action='store_true')
p.add_argument('--replay', type=Path)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
base = Path(__file__).resolve().parent
out = root / '.diagnostics/m2-model' / ('windows' if os.name == 'nt' else 'linux') / ('sanitized' if args.sanitize else 'normal')
out.mkdir(parents=True, exist_ok=True)
flags = [args.cxx, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-I', str(base / 'model')]
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
sources = [base / 'model/coherency_model.cpp', base / 'model/reference_memory.cpp']
for source in sources:
    subprocess.run(flags + ['-c', str(source), '-o', str(out / (source.stem + '.o'))], check=True)
for name in ['model_tests', 'model_random_tests']:
    exe = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(s) for s in sources] + [str(base / 'tests' / (name + '.cpp')), '-o', str(exe)], check=True)
    command = [str(exe)]
    if name == 'model_random_tests':
        command += (['--replay', str(args.replay.resolve())] if args.replay else
                    [str(args.seed), str(args.steps), str(args.seeds)])
    result = subprocess.run(command, cwd=out, capture_output=True, text=True, timeout=180)
    (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr, end='', flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
if args.mutation_check:
    # Compile an isolated faulty copy, never edit the model under test. Removing
    # authoritative replica refresh must be caught by byte oracle, not dirty flags.
    original = sources[0].read_text()
    pattern = r'if\s*\(auto e = CopySegments\(s, rep, segments, rep == CPU\);\s*e != Error::None\)'
    if len(re.findall(pattern, original)) != 1:
        raise SystemExit('Mutation marker missing/ambiguous: authoritative Read refresh')
    mutant = out / 'stale_read_mutant.cpp'
    mutant.write_text(re.sub(pattern, 'if (auto e = Error::None; e != Error::None)', original))
    exe = out / ('mutant' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(flags + [str(mutant), str(sources[1]), str(base / 'tests/model_tests.cpp'), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    (out / 'mutant.log').write_text(result.stdout + result.stderr)
    if result.returncode == 0 or 'byte oracle mismatch' not in result.stderr:
        raise SystemExit('Independent byte oracle did not kill stale-read mutant: ' + result.stdout + result.stderr)
    print('PASS independent byte oracle rejects isolated stale-read mutant')
