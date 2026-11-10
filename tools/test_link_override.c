#include <libavcodec/avcodec.h>
#include <stdio.h>

extern const AVCodec ff_xma2_decoder;

int main() {
    printf("ff_xma2_decoder addr: %p\n", &ff_xma2_decoder);
    const AVCodec* c = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    printf("found decoder: %p\n", c);
    return 0;
}
