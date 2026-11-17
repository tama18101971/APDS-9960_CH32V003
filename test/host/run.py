"""Run driver regression against the pinned dependency's public i2c.h."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--i2c-include', type=Path, default=root / '.pio/libdeps/ch32v003f4p6_evt_r0/I2C-CH32V003/src')
args = parser.parse_args()
if not (args.i2c_include / 'i2c.h').is_file():
    parser.error('i2c.h not found: run pio pkg install, or pass --i2c-include')
compiler = os.environ.get('CC') or shutil.which('gcc')
if not compiler and os.name == 'nt':
    candidate = Path.home() / '.platformio/packages/toolchain-gccmingw32/bin/gcc.exe'
    if candidate.is_file():
        compiler = str(candidate)
if not compiler:
    parser.error('host GCC not found; set CC to a GCC executable')
compiler_args = [compiler] if Path(compiler).is_file() else shlex.split(compiler)
env = os.environ.copy()
if Path(compiler_args[0]).is_file():
    env['PATH'] = str(Path(compiler_args[0]).parent) + os.pathsep + env.get('PATH', '')
build = root / '.pio/host-tests'
build.mkdir(parents=True, exist_ok=True)
variants = {
    'budget': ['-DAPDS_OWN_SYSTICK=0'],
    'systick': ['-DAPDS_OWN_SYSTICK=1'],
    'calibration-clamp': ['-DAPDS_OWN_SYSTICK=0', '-DAPDS_CAL_PROX_MAX=100'],
    'no-calibration': ['-DAPDS_OWN_SYSTICK=0', '-DAPDS_ENABLE_CALIBRATION=0'],
}
for name, flags in variants.items():
    binary = build / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(compiler_args + ['-std=gnu99', '-O0', '-Wall', '-Wextra', '-Werror'] + flags +
                   ['-I' + str(root / 'test/host'), '-I' + str(args.i2c_include),
                    str(root / 'test/host/regression.c'), '-o', str(binary)], check=True, cwd=root, env=env)
    print(name, flush=True)
    subprocess.run([str(binary)], check=True, cwd=root, env=env)
