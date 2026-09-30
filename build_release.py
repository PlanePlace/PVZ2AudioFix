#!/usr/bin/env python3
"""Build audio-only dylib plus rootful/rootless Debian packages on this Mac."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
XCODE = Path(os.environ.get('PVZ2_XCODE_DEVELOPER') or subprocess.check_output(['xcode-select', '-p'], text=True).strip())
CLANG = XCODE / 'Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
SDK = XCODE / 'Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk'
BUILD = ROOT / 'build'

def run(args):
    subprocess.run([str(a) for a in args], check=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def compile_library(output, minimum):
    run([CLANG, '-target', f'arm64-apple-ios{minimum}', '-isysroot', SDK, '-dynamiclib',
         '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden',
         '-framework', 'AudioToolbox', '-install_name', '@rpath/PVZ2AudioFix.dylib',
         ROOT / 'src/PVZ2AudioFix.c', '-o', output])
    run(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', output])
    run(['/usr/bin/codesign', '--verify', '--strict', output])

def package(scheme, library):
    root = BUILD / scheme
    root.mkdir(parents=True, exist_ok=True)
    prefix = 'var/jb/' if scheme == 'rootless' else ''
    destination = root / (prefix + 'Library/MobileSubstrate/DynamicLibraries')
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, destination / 'PVZ2AudioFix.dylib')
    shutil.copy2(ROOT / 'PVZ2AudioFix.plist', destination / 'PVZ2AudioFix.plist')
    (destination / 'PVZ2AudioFix.dylib').chmod(0o755)
    (destination / 'PVZ2AudioFix.plist').chmod(0o644)
    control = (ROOT / 'control').read_text()
    if scheme == 'rootless':
        control = control.replace('Architecture: iphoneos-arm\n', 'Architecture: iphoneos-arm64\n')
        control = control.replace('firmware (>= 14.4)', 'firmware (>= 15.0)')
    control += f'Installed-Size: {(library.stat().st_size + 1023) // 1024 + 1}\n'
    (root / 'DEBIAN').mkdir(exist_ok=True)
    (root / 'DEBIAN/control').write_text(control)
    output = ROOT / f'PVZ2AudioFix_1.1.0_{scheme}.deb'
    run(['dpkg-deb', '-Zgzip', '-z9', '--root-owner-group', '--build', root, output])
    run(['dpkg-deb', '--info', output])
    # Inspect unpacked final package content, modes, UID and actual filter.
    with tempfile.TemporaryDirectory(prefix='pvz2-audio-deb-') as temp:
        data = Path(temp) / 'data.tar'
        with data.open('wb') as stream:
            subprocess.run(['dpkg-deb', '--fsys-tarfile', str(output)], stdout=stream, check=True)
        with tarfile.open(data) as archive:
            files = [x for x in archive.getmembers() if x.isfile()]
            assert len(files) == 2
            for item in files:
                assert item.uid == 0 and item.gid == 0
                assert item.name.lstrip('./').startswith(prefix + 'Library/MobileSubstrate/DynamicLibraries/')
                content = archive.extractfile(item).read()
                if item.name.endswith('.dylib'):
                    assert item.mode == 0o755 and hashlib.sha256(content).hexdigest() == sha(library)
                else:
                    assert item.mode == 0o644
                    assert plistlib.loads(content) == {'Filter': {'Executables': ['PvZ2']}}
    return output

def main():
    if not SDK.exists():
        raise SystemExit('Full Xcode with the iPhoneOS SDK is required. Set PVZ2_XCODE_DEVELOPER to its Developer directory.')
    BUILD.mkdir(exist_ok=True)
    standalone = ROOT / 'PVZ2AudioFix.dylib'
    rootless = BUILD / 'PVZ2AudioFix-rootless.dylib'
    compile_library(standalone, '14.0')
    compile_library(rootless, '15.0')
    packages = [package('rootful', standalone), package('rootless', rootless)]
    manifest = {
        'version': '1.1.0-1', 'architecture': 'arm64',
        'runtime_uuid_restriction': False,
        'runtime_game_version_restriction': False,
        'runtime_fixed_callsite_restriction': False,
        'format_scope': 'Mono AAC, 8000 or 11025 Hz, native channel-layout !siz only',
        'target': 'Legacy PvZ2 native ARM64 processes with compatible writable imports; compatibility is build-dependent',
        'hooked_apis': ['AudioFileGetProperty', 'AudioFileGetPropertyInfo'],
        'device_confirmed': 'User reported successful 1.1.0 tests for Chinese iOS 1.7.1, 1.7.4 and 2.5.0 on 2026-09-30; exact installation-mode matrix not specified',
        'device_validation': 'User reported successful 1.1.0 testing; source-packaging revision and individual DEB environments not separately device-tested',
        'checks': ['iOS warnings-as-errors compile', 'ad-hoc codesign strict verification',
                   'final DEB architecture, content, modes, owners, paths and filter verification'],
        'files': {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in [standalone, *packages]},
        'source_sha256': sha(ROOT / 'src/PVZ2AudioFix.c'),
    }
    validation = ROOT / '测试结果.json'
    if validation.exists():
        tests = json.loads(validation.read_text())
        manifest['test_report_sha256'] = sha(validation)
        manifest['host_tests'] = {
            'fallback_cases': tests['fallback_gates_and_buffer_boundaries'],
            'native_audio_integration_passed': bool(tests.get('native_audio')),
            'native_audio_test_boundary': 'macOS integration uses the same unrestricted C source without host-only caller or UUID bypasses',
        }
    (ROOT / '构建校验.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    paths = ['PVZ2AudioFix.dylib', 'PVZ2AudioFix_1.1.0_rootful.deb', 'PVZ2AudioFix_1.1.0_rootless.deb',
             'README.md', 'LICENSE', 'CHANGELOG.md', 'docs/COMPATIBILITY.md', 'docs/TECHNICAL.md', 'tests/README.md', '.gitignore', '构建校验.json', 'build_release.py', 'Makefile', 'control', 'PVZ2AudioFix.plist',
             'src/PVZ2AudioFix.c', 'tests/fallback_tests.c', 'tests/host_decode_check.c', 'tests/run_host_checks.py']
    if validation.exists(): paths.append(validation.name)
    with zipfile.ZipFile(ROOT / 'PVZ2AudioFix_1.1.0_发布包.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in paths:
            archive.write(ROOT / path, 'PVZ2AudioFix/' + path)
    print(json.dumps(manifest, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
