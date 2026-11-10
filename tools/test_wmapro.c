#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    const AVCodec* c_wma = avcodec_find_decoder(AV_CODEC_ID_WMAPRO);
    if (!c_wma) return 1;

    AVCodecContext* ctx = avcodec_alloc_context3(c_wma);
    ctx->sample_rate = 48000;
    ctx->ch_layout.nb_channels = 2;
    ctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;

    uint8_t edata[18 + AV_INPUT_BUFFER_PADDING_SIZE] = {0};
    uint16_t bps = 16;
    uint32_t mask = 3;
    uint16_t flags = 0x10d6;
    memcpy(edata + 0, &bps, 2);
    memcpy(edata + 2, &mask, 4);
    memcpy(edata + 14, &flags, 2);
    ctx->extradata = edata;
    ctx->extradata_size = 18;
    ctx->block_align = 2048;

    int ret = avcodec_open2(ctx, c_wma, NULL);
    printf("avcodec_open2 ret: %d\n", ret);

    // Let's create an AVPacket of block_align = 2048 bytes
    AVPacket* pkt = av_packet_alloc();
    uint8_t pkt_data[2048 + AV_INPUT_BUFFER_PADDING_SIZE] = {0};
    // WMAPRO packet header: 4 bits seq (0), 2 bits skip (0), 15 bits num_bits_prev_frame (0)
    // 21 bits total. All zeros means sequence 0, prev_bits 0.
    pkt->data = pkt_data;
    pkt->size = 2048;

    AVFrame* frame = av_frame_alloc();
    ret = avcodec_send_packet(ctx, pkt);
    printf("avcodec_send_packet ret: %d\n", ret);

    ret = avcodec_receive_frame(ctx, frame);
    printf("avcodec_receive_frame ret: %d, nb_samples: %d\n", ret, frame->nb_samples);

    // Send packet 2 (seq 1)
    // seq is 4 bits: seq = 1 << 4 in first byte?
    // In get_bits(gb, 4): first 4 bits of the packet are sequence number!
    // So byte 0 high 4 bits = 1: 0x10.
    pkt_data[0] = 0x10;
    ret = avcodec_send_packet(ctx, pkt);
    printf("avcodec_send_packet 2 ret: %d\n", ret);
    ret = avcodec_receive_frame(ctx, frame);
    printf("avcodec_receive_frame 2 ret: %d, nb_samples: %d\n", ret, frame->nb_samples);

    return 0;
}
