#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <complex.h>
#include <fftw3.h>
#include "ringbuf.h"

#define audio_min(a,b) ((a) < (b) ? (a) : (b) )
#define audio_max(a,b) ((a) > (b) ? (a) : (b) )
#define audio_notzero(a,b) ( ((a) == 0) ? (b) : (a) )

typedef struct frange {
    double freq;
    double amp;
    double prevamp;
    unsigned int first_bin;
    unsigned int last_bin;
} frange;

typedef struct audio_processor {
    unsigned int samplerate;
    unsigned int channels;
    unsigned int samplesize;
    unsigned int framerate;

    unsigned int samples_available;
    unsigned int samples_pos;
    unsigned int samples_pos_read;

    unsigned int samples_len; /* samplerate * 4 */
    unsigned int sample_window_len; /* samplerate/framerate */
    unsigned int chunk_len;         /* 2048 */
    unsigned int fftw_len;   /* chunk_len / 2 - 1 */
    double sample_max_val; /* pow(2,(8*samplesize-1)) */
    int firstflag;

    ringbuf_t samples;
    double *window; /* window[chunk_len] */

    double *fftw_buffer;   /* samples_mono[chunk_len] */
    double *fftw_in;   /* samples_mono[chunk_len] */
    fftw_complex *fftw_out; /*fftw_output[fftw_len] */
    fftw_plan plan;

    unsigned int spectrum_len;
    frange *spectrum_cur;

    unsigned int output_buffer_len;
    char *output_buffer; /* output_buffer[sample_window_len * samplesize * channels] */
    void (*audio_downmix_func)(struct audio_processor *);

} audio_processor;

#define AUDIO_PROCESSOR_ZERO { \
    .samplerate = 0, \
    .channels = 0, \
    .samplesize = 0, \
    .framerate = 0, \
    .samples_available = 0, \
    .samples_pos = 0, \
    .samples_pos_read = 0, \
    .samples_len = 0, \
    .sample_window_len = 0, \
    .chunk_len = 0, \
    .fftw_len = 0, \
    .sample_max_val = 0.0f, \
    .firstflag = 0, \
    .samples = NULL, \
    .window = NULL, \
    .fftw_in = NULL, \
    .fftw_out = NULL, \
    .plan = NULL, \
    .spectrum_len = 0, \
    .spectrum_cur = NULL, \
    .output_buffer_len = 0, \
    .output_buffer = NULL, \
    .audio_downmix_func = NULL, \
}

#ifdef __cplusplus
extern "C" {
#endif

int
audio_processor_init(audio_processor *processor);

int
audio_processor_reload(audio_processor *processor);

int
audio_processor_free(audio_processor *processor);

void audio_processor_fftw(audio_processor *processor);
void write_mono_buffer(int fd, audio_processor *p);
void audio_processor_copy_amps(audio_processor *processor);

#ifdef __cplusplus
}
#endif

#endif

