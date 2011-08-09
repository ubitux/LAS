/* vim: set et sts=4 sw=4:
 *
 * LAS, Lossy Audio Spotter
 * Copyright (C) 2011  Clément Bœsch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <libavcodec/avfft.h>
#include <libavformat/avformat.h>

#define FFT_NBITS   9
#define FFT_WINSIZE (1<<FFT_NBITS)

struct las {
    AVCodecContext *codec;
    uint8_t *audio_buf;
    int16_t *samples;
    int samples_bsize;
    int filled;
    RDFTContext *rdft;
    FFTSample *rdft_data;
    int win_count;
    float fft[FFT_WINSIZE/2 + 1];
};

static float hann[FFT_WINSIZE];

static void precalc_hann(void)
{
    for (int i = 0; i < FFT_WINSIZE; i++)
        hann[i] = .5f * (1 - cos(2*M_PI*i / (FFT_WINSIZE-1)));
}

static void process_samples(struct las *c)
{
    c->win_count++;
    c->filled = 0;

    // Resample (int16_t to float), downmix if necessary and apply Hann window
    int16_t *s16 = c->samples;
    switch (c->codec->channels) {
    case 2:
        for (int i = 0; i < FFT_WINSIZE; i++)
            c->rdft_data[i] = (s16[i*2] + s16[i*2+1]) / (2*32768.f) * hann[i];
        break;
    case 1:
        for (int i = 0; i < FFT_WINSIZE; i++)
            c->rdft_data[i] = s16[i] / 32768.f * hann[i];
        break;
    }

    // FFT
#define FFT_ASSIGN_VALUES(i, re, im) c->fft[i] += re*re + im*im
    float *bin = c->rdft_data;
    av_rdft_calc(c->rdft, bin);
    for (int i = 1; i < FFT_WINSIZE/2; i++)
        FFT_ASSIGN_VALUES(i, bin[i*2], bin[i*2+1]);
    FFT_ASSIGN_VALUES(0,             bin[0], 0);
    FFT_ASSIGN_VALUES(FFT_WINSIZE/2, bin[1], 0);
}

static int process_audio_pkt(struct las *c, AVPacket *pkt)
{
    while (pkt->size > 0) {
        uint8_t *data = c->audio_buf;
        int data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
        int len       = avcodec_decode_audio3(c->codec, (int16_t*)data, &data_size, pkt);
        if (len < 0) {
            pkt->size = 0;
            return -1;
        }
        pkt->data += len;
        pkt->size -= len;
        while (data_size > 0) {
            int needed = c->samples_bsize - c->filled; // in bytes
            int ncpy   = data_size >= needed ? needed : data_size;
            memcpy((uint8_t*)c->samples + c->filled, data, ncpy);
            c->filled += ncpy;
            if (c->filled != c->samples_bsize)
                break;
            process_samples(c);
            data      += ncpy;
            data_size -= ncpy;
        }
    }
    return 0;
}

int main(int ac, char **av)
{
    unsigned i;
    static struct las ctx;

    if (ac != 2) {
        fprintf(stderr, "Usage: %s song\n", av[0]);
        return 1;
    }

    /* FFmpeg init */
    av_register_all();
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, av[1], NULL, NULL) ||
        av_find_stream_info(fmt_ctx) < 0)
        errx(1, "unable to load %s", av[1]);
    int stream_id = -1;
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_id = i;
            break;
        }
    }
    if (stream_id == -1)
        errx(1, "no audio stream found");
    ctx.codec = fmt_ctx->streams[stream_id]->codec;
    AVCodec *adec = avcodec_find_decoder(ctx.codec->codec_id);
    if (!adec)
        errx(1, "unsupported codec");
    avcodec_open(ctx.codec, adec);
    if (ctx.codec->channels != 1 && ctx.codec->channels != 2)
        errx(1, "unsupported number of channels (%d)", ctx.codec->channels);

    /* LAS init */
    precalc_hann();
    ctx.samples_bsize = FFT_WINSIZE * ctx.codec->channels * sizeof(*ctx.samples);
    ctx.samples       = av_malloc(ctx.samples_bsize);
    ctx.rdft          = av_rdft_init(FFT_NBITS, DFT_R2C);
    ctx.rdft_data     = av_malloc(FFT_WINSIZE * sizeof(*ctx.rdft_data));
    ctx.audio_buf     = av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

    /* Process packets */
    AVPacket pkt;
    while (av_read_frame(fmt_ctx, &pkt) >= 0)
        if (pkt.stream_index == stream_id && process_audio_pkt(&ctx, &pkt) < 0)
            warnx("error while processing audio packet");
    if (ctx.filled) { // flush buffer
        memset((uint8_t*)ctx.samples + ctx.filled, 0, ctx.samples_bsize - ctx.filled);
        process_samples(&ctx);
    }

    /* Gnuplot histogram */
    printf("set terminal png font tiny size 300,200\n"
           "set style data boxes\n"
           "set style fill solid\n"
           "set xrange [0:%f]\n"
           "set yrange [-120:0]\n"
           "set xlabel 'Frequency (kHz)'\n"
           "set ylabel 'Log magnitude (dB)'\n"
           "plot '-' using ($0/%d.*%f):1 notitle\n",
           44.1f/2.f, FFT_WINSIZE, 44.1f);
    for (i = 0; i < sizeof(ctx.fft)/sizeof(*ctx.fft); i++) {
        float x = ctx.fft[i] / ctx.win_count / FFT_WINSIZE;
        ctx.fft[i] = 10 * log10(x ? x : -1e20);
        printf("%f\n", ctx.fft[i]);
    }

    /* Find higher frequency used */
    unsigned last = sizeof(ctx.fft)/sizeof(*ctx.fft) - 1;
    for (i = last - 1; i > 0; i--)
        if (fabsf(ctx.fft[i] - ctx.fft[last])  > 1.5f ||
            fabsf(ctx.fft[i] - ctx.fft[i + 1]) > 3.f)
            break;

    /* Try to detect huge falls too */
#define THRESHOLD_FALL 5
    for (unsigned n = i - THRESHOLD_FALL; n; n--) {
        if (ctx.fft[n] - ctx.fft[n + THRESHOLD_FALL] > 25.f) {
            i = n;
            break;
        }
    }

    /* Report lossy/lossless estimation in a Gnuplot comment */
    int hfreq = i * 44100 / FFT_WINSIZE;
    printf("# higher freq=%dHz → %s\n", hfreq, hfreq < 21000 ? "lossy" : "lossless");

    /* Cleanup */
    av_free(ctx.audio_buf);
    av_free(ctx.rdft_data);
    av_rdft_end(ctx.rdft);
    av_free(ctx.samples);
    avcodec_close(ctx.codec);
    av_close_input_file(fmt_ctx);
    return 0;
}
