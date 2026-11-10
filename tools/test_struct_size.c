#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XMA_MAX_STREAMS 8
#define WMAPRO_MAX_CHANNELS 8

typedef struct WMAProChannelGrp {
    uint8_t num_channels;
    uint8_t transform;
    uint8_t transform_band[WMAPRO_MAX_CHANNELS];
    float   decorrelation_matrix[WMAPRO_MAX_CHANNELS * WMAPRO_MAX_CHANNELS];
    float*  channel_data[WMAPRO_MAX_CHANNELS];
} WMAProChannelGrp;

typedef struct WMAProChannelCtx {
    int16_t  prev_block_len;
    uint8_t  transmit_coefs;
    uint8_t  num_subframes;
    uint16_t subframe_len[WMAPRO_MAX_CHANNELS];
    float*   coeffs;
    float*   out;
} WMAProChannelCtx;

// Let's check size of WMAProDecodeCtx in the compiled ffmpeg!
int main() {
    const AVCodec* c_xma = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    const AVCodec* c_wma = avcodec_find_decoder(AV_CODEC_ID_WMAPRO);
    printf("c_xma priv_data_size: %d\n", c_xma->priv_data_size);
    printf("c_wma priv_data_size: %d\n", c_wma->priv_data_size);
    return 0;
}
