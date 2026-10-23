# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Android OpenSSL tool discovery across reused CMake state; no build."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/openssl-tool-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'CMakeModules/openssl_build.cmake').read_text(encoding='utf-8')
start = source.index('    get_filename_component(_OPENSSL_ANDROID_TOOL_DIR')
end = source.index('    if (NOT _OPENSSL_ANDROID_CLANG', start)
discovery = source[start:end]
suffix = '.exe' if os.name == 'nt' else ''
steps = []
for index, (triple, api) in enumerate([
    ('aarch64-linux-android', '21'),
    ('armv7a-linux-androideabi', '23'),
    ('armv7a-linux-androideabi', '24'),
]):
    directory = out / f'ndk-{index}'
    directory.mkdir(exist_ok=True)
    clang = directory / f'{triple}{api}-clang{suffix}'
    ar = directory / f'llvm-ar{suffix}'
    for program in (clang, ar):
        program.touch()
        if os.name != 'nt': program.chmod(0o755)
    steps += [
        f'set(CMAKE_C_COMPILER "{directory.as_posix()}/clang{suffix}")',
        f'set(_OPENSSL_ANDROID_TRIPLE "{triple}")',
        f'set(_OPENSSL_ANDROID_API "{api}")',
        discovery,
        f'if(NOT _OPENSSL_ANDROID_CLANG STREQUAL "{clang.as_posix()}" OR '
        f'NOT _OPENSSL_ANDROID_AR STREQUAL "{ar.as_posix()}")',
        '  message(FATAL_ERROR "Tool lookup retained a stale ABI/API/NDK path")',
        'endif()',
    ]
script = out / 'check.cmake'
script.write_text('\n'.join(steps), encoding='utf-8')
subprocess.run(['cmake', '-P', str(script)], check=True)
print('PASS reused CMake tool discovery follows current ABI, API and NDK')
