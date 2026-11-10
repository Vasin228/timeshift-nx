import subprocess

code = """#include <switch.h>
#include <csignal>
#include <exception>
#include <cstdio>
#include <cstdlib>

void sig_handler(int sig) {
    fprintf(stderr, "Caught signal: %d\\n", sig);
    fflush(stderr);
}
int main() {
    signal(SIGSEGV, sig_handler);
    signal(SIGABRT, sig_handler);
    std::set_terminate([]() {
        fprintf(stderr, "terminate called\\n");
    });
    return 0;
}
"""

with open("/tmp/test_signals.cpp", "w") as f:
    f.write(code)

import os
env = os.environ.copy()
env['DEVKITPRO'] = '/opt/devkitpro'
env['DEVKITARM'] = '/opt/devkitpro/devkitARM'

res = subprocess.run([
    "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++",
    "-fPIE",
    "-specs=/opt/devkitpro/libnx/switch.specs",
    "-I/opt/devkitpro/libnx/include",
    "-L/opt/devkitpro/libnx/lib",
    "/tmp/test_signals.cpp",
    "-lnx",
    "-o", "/tmp/test_signals.elf"
], env=env, capture_output=True, text=True)

print("Return code:", res.returncode)
if res.stderr:
    print("Stderr:", res.stderr)
