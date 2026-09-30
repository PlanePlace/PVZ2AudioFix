#!/usr/bin/env python3
"""Production gate tests; optionally run native macOS CoreAudio integration.

Host and iOS use the same unrestricted source without caller bypass macros.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
XCODE = Path(os.environ.get('PVZ2_XCODE_DEVELOPER') or subprocess.check_output(['xcode-select', '-p'], text=True).strip())
CLANG = XCODE / 'Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
MAC_SDK = XCODE / 'Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
COMMON = [str(CLANG), '-isysroot', str(MAC_SDK), '-O2', '-Wall', '-Wextra', '-Werror', '-framework', 'AudioToolbox']

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, **kwargs)

def require(args):
    result = run(args)
    if result.returncode: raise RuntimeError(result.stdout + result.stderr)
    return result.stdout + result.stderr

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sample-dir', type=Path, help='Directory containing the two original AAC samples described in tests/README.md')
    parser.add_argument('--native-audio', action='store_true', help='Requires access to macOS CoreAudio service')
    options = parser.parse_args()
    if options.native_audio and (not options.sample_dir or not options.sample_dir.is_dir()):
        parser.error('--native-audio requires --sample-dir; game audio is not distributed with this project')
    BUILD.mkdir(exist_ok=True)
    unit = BUILD / 'fallback_tests'
    require(COMMON + ['-Wno-unused-function', ROOT / 'tests/fallback_tests.c', '-o', unit])
    output = require([unit])
    print(output, end='')
    report = {'fallback_gates_and_buffer_boundaries': output.strip(), 'native_audio': None,
              'device_tested': False}
    if options.native_audio:
        executable = BUILD / 'host_decode_check'
        host = BUILD / 'PVZ2AudioFix-host.dylib'
        require(COMMON + ['-Wno-deprecated-declarations', '-Wl,-no_data_const', '-Wl,-no_fixup_chains',
                          ROOT / 'tests/host_decode_check.c', '-o', executable])
        require(COMMON + ['-dynamiclib',
                          ROOT / 'src/PVZ2AudioFix.c', '-o', host])
        samples = options.sample_dir
        results = []
        for sample in [samples / '01_原始AAC_8000Hz_约8kbps.m4a', samples / '02_原始AAC_11025Hz_约8kbps.m4a']:
            clean = dict(os.environ); clean.pop('DYLD_INSERT_LIBRARIES', None)
            native = run([executable, sample], env=clean)
            assert native.returncode == 1 and '561211770' in native.stdout, native.stdout + native.stderr
            fixed = run([executable, sample], env={**clean, 'DYLD_INSERT_LIBRARIES': str(host)})
            assert fixed.returncode == 0 and 'peak=' in fixed.stdout, fixed.stdout + fixed.stderr
            results.append({'sample': sample.name, 'native_output': native.stdout,
                            'fixed_output': fixed.stdout, 'test_mode': 'production source without caller bypass'})
        report['native_audio'] = results
        print('PASS: original AAC at 8000/11025 Hz, native !siz, production source without caller bypass, nonzero decoded PCM')
    (ROOT / '测试结果.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')

if __name__ == '__main__': main()
