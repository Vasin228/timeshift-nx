import subprocess

code = """#include <libavcodec/avcodec.h>
#include <stdio.h>

int main() {
    const AVCodec* c_xma2 = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    const AVCodec* c_wmapro = avcodec_find_decoder(AV_CODEC_ID_WMAPRO);
    const AVCodec* c_xma1 = avcodec_find_decoder(AV_CODEC_ID_XMA1);
    printf("xma1: %p, xma2: %p, wmapro: %p\\n", c_xma1, c_xma2, c_wmapro);
    return 0;
}
"""

with open("/tmp/test_codec.c", "w") as f:
    f.write(code)

cmd = "export DEVKITPRO=/opt/devkitpro; /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc -specs=/opt/devkitpro/libnx/switch.specs -I/opt/devkitpro/portlibs/switch/include /tmp/test_codec.c -L/opt/devkitpro/portlibs/switch/lib -lavcodec -lavutil -lm -o /tmp/test_codec.elf"
res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
print("Compile returncode:", res.returncode)
if res.stderr:
    print("STDERR:", res.stderr)
