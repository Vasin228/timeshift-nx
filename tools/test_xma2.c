#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    const AVCodec* c_xma = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    if (!c_xma) { printf("No xma2\n"); return 1; }

    AVCodecContext* ctx = avcodec_alloc_context3(c_xma);
    ctx->sample_rate = 48000;
    ctx->ch_layout.nb_channels = 2;
    ctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;

    uint8_t edata[34 + AV_INPUT_BUFFER_PADDING_SIZE] = {0};
    uint16_t streams = 1;
    uint32_t mask = 3;
    uint16_t ba = 2048;
    uint16_t bps = 16;
    uint8_t ver = 4;
    uint16_t opts = 0x10d6;
    memcpy(edata, &streams, 2);
    memcpy(edata + 2, &mask, 4);
    memcpy(edata + 10, &ba, 2);
    memcpy(edata + 24, &bps, 2);
    edata[31] = ver;
    memcpy(edata + 32, &opts, 2);
    ctx->extradata = edata;
    ctx->extradata_size = 34;
    ctx->block_align = 2048;

    int ret = avcodec_open2(ctx, c_xma, NULL);
    printf("xma2 avcodec_open2 ret: %d\n", ret);
    return 0;
}
