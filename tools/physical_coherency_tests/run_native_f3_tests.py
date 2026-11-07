#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the real Vulkan F3 witness, including its expected-failing fixed mode.

No extracted cache or renderer body is used. A timeout/crash or unrelated assertion
cannot count as successful defect reproduction.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path,
                        default=Path('build-integration/bin/controlled_cpu_scheduler_tests'))
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--output', type=Path, default=Path('.diagnostics/pr375-pr380'))
    args = parser.parse_args()
    if args.repeat < 1 or args.timeout < 1:
        parser.error('repeat and timeout must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    command = [str(args.binary.resolve()), '[f3]', '--colour-mode', 'none']
    for index in range(args.repeat + 1):
        fixed = index == args.repeat
        name = 'f3-require-fixed' if fixed else f'f3-repeat-{index + 1}'
        env = os.environ.copy()
        env.pop('CITRON_REQUIRE_F3_FIXED', None)
        if fixed:
            env['CITRON_REQUIRE_F3_FIXED'] = '1'
        log = args.output / f'{name}.txt'
        with log.open('w', encoding='utf-8') as stream:
            try:
                result = subprocess.run(command, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                stream.write(f'\nTIMEOUT after {args.timeout} seconds\n')
                raise SystemExit(f'{name}: timeout; see {log}')
            stream.write(f'\nPROCESS EXIT={result.returncode}\n')
        output = log.read_text(encoding='utf-8')
        print(output, flush=True)
        compact = ' '.join(output.split())
        observation = ('F3 full-page expected=99 observed=10; partial-alias expected=99 '
                       'observed=10; same-DAddr=99; remap=55')
        if observation not in output or 'F3 cleanup: guest owners released' not in output:
            raise SystemExit(f'{name}: missing observations or guest teardown; see {log}')
        if not fixed:
            if result.returncode != 0 or 'F3 REPRODUCED (not fixed)' not in output:
                raise SystemExit(f'{name}: witness failed (exit {result.returncode}); see {log}')
        else:
            # Require precisely the two alias-coherency assertion failures and a normal
            # Catch2 test result. Signals, teardown faults and unrelated failures fail CI.
            # Catch2 3.16 uses TestFailureExitCode=42; older v3 releases used
            # failure counts. Still require exactly the two expected assertions.
            # https://github.com/catchorg/Catch2/blob/v3.16.1/src/catch2/catch_session.hpp
            if (result.returncode not in (1, 2, 42) or
                    'full_alias == 99' not in output or 'partial_alias == 99' not in output or
                    not re.search(r'assertions: .*\| 2 failed\b', compact) or
                    'fatal error condition' in output):
                raise SystemExit(f'{name}: unexpected negative-control result '
                                 f'(exit {result.returncode}); see {log}')
            print(f'PASS fixed-mode negative control: exit {result.returncode}', flush=True)


if __name__ == '__main__':
    main()
