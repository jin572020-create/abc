#!/usr/bin/env python3
"""Exact-output regression for the 30 supplied lexical fixtures."""
import argparse
from pathlib import Path
import subprocess
import sys
import difflib

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--compiler', default='./compiler', help='path relative to project root or absolute')
a = p.parse_args()
exe = Path(a.compiler)
if not exe.is_absolute():
    exe = ROOT / exe
exe = exe.resolve()
if not exe.is_file():
    sys.exit(f'Compiler not found: {exe}. Run make or cmake --build build first.')

fixtures = sorted((ROOT / 'tests' / 'lexical').glob('*.c'))
if len(fixtures) != 30:
    sys.exit(f'Expected 30 lexical fixtures; found {len(fixtures)}')

passed = 0
for source in fixtures:
    expected_file = source.with_suffix('.token')
    expected = expected_file.read_bytes().replace(b'\r\n', b'\n')
    run = subprocess.run([str(exe), '-token'], input=source.read_bytes(),
                         capture_output=True, timeout=10)
    bad_input = expected.startswith(b'reject\n')
    correct_exit = run.returncode != 0 if bad_input else run.returncode == 0
    ok = run.stdout == expected and correct_exit and not run.stderr
    print(f'{"PASS" if ok else "FAIL"} {source.stem} (returncode={run.returncode})')
    if ok:
        passed += 1
    else:
        actual_lines = run.stdout.decode('utf-8', errors='replace').splitlines(keepends=True)
        expected_lines = expected.decode('utf-8', errors='replace').splitlines(keepends=True)
        print(''.join(list(difflib.unified_diff(expected_lines, actual_lines,
                     fromfile='expected', tofile='actual'))[:40]))
        if run.stderr:
            print('stderr:', run.stderr.decode('utf-8', errors='replace')[:400])
print(f'RESULT: {passed}/{len(fixtures)}')
sys.exit(0 if passed == len(fixtures) else 1)
