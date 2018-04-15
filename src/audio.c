#include <skalibs/strerr.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <complex.h>
#include <fftw3.h>
#include <fcntl.h>
#include "audio.h"

#define SMOOTH_DOWN 0.2
#define SMOOTH_UP 0.8

#ifdef __cplusplus
extern "C" {
#endif

static double window_blackman_harris(int i, int n) {
    double a = (2.0f * M_PI) / (n - 1);
    return 0.35875 - 0.48829*cos(a*i) + 0.14128*cos(2*a*i) - 0.01168*cos(3*a*i);
}

static double find_amplitude(fftw_complex *out, unsigned int start, unsigned int end, unsigned int chunk_len) {
    unsigned int i;
    double val = -INFINITY;
    double tmp = 0.0f;
    for(i=start;i<=end;i++) {
        tmp = 20.0f * log10(2.0f * cabs(out[i]) / chunk_len);
        val = audio_max(tmp,val);
    }
    return val;
}


static void mono_downmix(audio_processor *processor) {
    unsigned int i = 0;
    unsigned int s = 0;
    int32_t m = 0;

    char *buffer = processor->output_buffer;

    while(i<processor->sample_window_len) {
        s = i * processor->samplesize * 2;
        switch(processor->samplesize) {
            case 1: {
                m += buffer[s];
                break;
            }
            case 2: {
                if(buffer[s+1] & 0x80) {
                    m = 0xffff << 16 | buffer[s+1] << 8 | buffer[s];
                }
                else {
                    m = buffer[s+1] << 8 | buffer[s];
                }
                break;
            }
            case 3: {
                if(buffer[s+2] & 0x80) {
                    m = 0xff << 24 | buffer[s+2] << 16 | buffer[s+1] << 8 | buffer[s];
                }
                else {
                    m = buffer[s+2] << 16 | buffer[s+1] << 8 | buffer[s];
                }
                break;
            }
        }
        processor->fftw_in[i] = (double)m / processor->sample_max_val;
        processor->fftw_in[i] *= processor->window[i];
        i++;
    }
}


static void stereo_downmix(audio_processor *processor) {
    unsigned int i = 0;
    unsigned int s = 0;
    int32_t l = 0;
    int32_t r = 0;
    int32_t m = 0;

    char *buffer = processor->output_buffer;

    while(i<processor->sample_window_len) {
        s = i * processor->samplesize * 2;
        switch(processor->samplesize) {
            case 1: {
                l += buffer[s];
                r += buffer[s+1];
                break;
            }
            case 2: {
                if(buffer[s+1] & 0x80) {
                    l = 0xffff << 16 | buffer[s+1] << 8 | buffer[s];
                }
                else {
                    l = buffer[s+1] << 8 | buffer[s];
                }
                if(buffer[s+3] & 0x80) {
                    r = 0xffff << 16 | buffer[s+3] << 8 | buffer[s+2];
                }
                else {
                    r = buffer[s+3] << 8 | buffer[s+2];
                }
                break;
            }
            case 3: {
                if(buffer[s+2] & 0x80) {
                    l = 0xff << 24 | buffer[s+2] << 16 | buffer[s+1] << 8 | buffer[s];
                }
                else {
                    l = buffer[s+2] << 16 | buffer[s+1] << 8 | buffer[s];
                }
                if(buffer[s+5] & 0x80) {
                    r = 0xff << 24 | buffer[s+5] << 16 | buffer[s+4] << 8 | buffer[s+3];
                }
                else {
                    r = buffer[s+5] << 16 | buffer[s+4] << 8 | buffer[s+3];
                }
                break;
            }
        }
        m = (l/2) + (r/2);
        processor->fftw_in[i] = (double)m / processor->sample_max_val;
        processor->fftw_in[i] *= processor->window[i];
        i++;
    }
}


void audio_processor_fftw(audio_processor *processor) {
    if(ringbuf_memcpy_from(processor->output_buffer,processor->samples,processor->output_buffer_len) == NULL) {
        fprintf(stderr,"Warning - tried to underflow\n");
        return;
    }
    (processor->audio_downmix_func)(processor);

    unsigned int i = 0;

    if(!processor->plan) {
        processor->plan = fftw_plan_dft_r2c_1d(processor->chunk_len,processor->fftw_in,processor->fftw_out,FFTW_ESTIMATE);
    }

    fftw_execute(processor->plan);

    for(i=0;i<processor->spectrum_len;i++) {
        processor->spectrum_cur[i].amp = find_amplitude(processor->fftw_out,processor->spectrum_cur[i].first_bin,processor->spectrum_cur[i].last_bin,processor->chunk_len);
        if(!isfinite(processor->spectrum_cur[i].amp)) {
            processor->spectrum_cur[i].amp = -100.0f; /* filtered out next line */
        }
        if(processor->spectrum_cur[i].amp < -90.0f) {
            processor->spectrum_cur[i].amp = 0.0f;
        }
        else {
            processor->spectrum_cur[i].amp += 90.0f;
        }


        if(processor->firstflag) {
            if(processor->spectrum_cur[i].amp < processor->spectrum_cur[i].prevamp) {
                processor->spectrum_cur[i].amp =
                  processor->spectrum_cur[i].amp * SMOOTH_DOWN +
                  processor->spectrum_cur[i].prevamp * ( 1 - SMOOTH_DOWN);
            }
            else {
                processor->spectrum_cur[i].amp =
                  processor->spectrum_cur[i].amp * SMOOTH_UP +
                  processor->spectrum_cur[i].prevamp * ( 1 - SMOOTH_UP);
            }
        }
        processor->spectrum_cur[i].prevamp = processor->spectrum_cur[i].amp;
    }
}

int
audio_processor_reload(audio_processor *processor) {
    if(processor->channels == 2) {
        processor->audio_downmix_func = &stereo_downmix;
    }
    else {
        processor->audio_downmix_func = &mono_downmix;
    }
    return 1;
}

int
audio_processor_init(audio_processor *processor) {
    if(!processor) return 0;

    unsigned int i = 0;
    unsigned int j = 0;
    double bin_size = 0.0f;
    double freq_ratio = 0.0f;
    double freq_range = 20000.0f - 50.0f;

    if(processor->channels > 2) {
        strerr_warn1x("Too many channels, max is 2");
        return 0;
    }
    if(processor->samplesize > 3) {
        strerr_warn1x("Too big sample size, max is 3");
        return 0;
    }

    processor->chunk_len = 2048;
    processor->samples_available = 0;
    processor->samples_pos = 0;
    processor->samples_pos_read = 0;
    processor->samples_len = processor->samplerate * 4;
    processor->sample_window_len = processor->samplerate / processor->framerate;
    while(processor->chunk_len < processor->sample_window_len) {
        processor->chunk_len = processor->chunk_len * 2;
    }
    bin_size = (double)processor->samplerate / (double)processor->chunk_len;
    processor->fftw_len = (processor->chunk_len / 2) + 1;
    processor->firstflag = 0;
    if(processor->samplesize > 1) {
        processor->sample_max_val = pow(2,(8*processor->samplesize-1));
    }
    else {
        processor->sample_max_val = 256.0f;
    }

    processor->output_buffer_len = processor->sample_window_len * processor->samplesize * processor->channels;

    if(processor->channels == 2) {
        processor->audio_downmix_func = &stereo_downmix;
    }
    else {
        processor->audio_downmix_func = &mono_downmix;
    }

    processor->samples = ringbuf_new(65536);
    if(!processor->samples) {
        return audio_processor_free(processor);
    }

    processor->window = (double *)malloc(sizeof(double) * processor->sample_window_len);
    if(!processor->window) {
        return audio_processor_free(processor);
    }
    for(i=0;i<processor->sample_window_len;i++) {
        processor->window[i] = window_blackman_harris(i,processor->sample_window_len - 1);
        /* processor->window[i] = window_hanning(i,processor->sample_window_len - 1); */
    }

    processor->fftw_in = (double *)fftw_malloc(sizeof(double) * processor->chunk_len);
    if(!processor->fftw_in) {
        return audio_processor_free(processor);
    }

    processor->fftw_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * processor->chunk_len);
    if(!processor->fftw_out) {
        return audio_processor_free(processor);
    }

    processor->output_buffer = (char *)malloc(processor->output_buffer_len);
    if(!processor->output_buffer) {
        return audio_processor_free(processor);
    }
    memset(processor->output_buffer,0,processor->output_buffer_len);

    for(i=processor->sample_window_len; i<processor->chunk_len; i++) {
        processor->fftw_in[i] = 0;
    }

    processor->spectrum_cur = (frange *)malloc(sizeof(frange) * processor->spectrum_len);
    if(!processor->spectrum_cur) {
        return audio_processor_free(processor);
    }

    for(i=0;i<processor->spectrum_len;i++) {
        processor->spectrum_cur[i].first_bin = 0;
        processor->spectrum_cur[i].last_bin = 0;
        processor->spectrum_cur[i].freq = 50.0f + ((freq_range / (processor->spectrum_len - 1)) * i);
        processor->spectrum_cur[i].amp = 0.0f;
        processor->spectrum_cur[i].prevamp = 0.0f;
    }

    j = 1;
    while(j < processor->spectrum_cur[0].freq / bin_size) {
        processor->spectrum_cur[0].first_bin = audio_notzero(audio_min(processor->spectrum_cur[0].first_bin,j),j);
        processor->spectrum_cur[0].last_bin = audio_max(processor->spectrum_cur[0].last_bin,j);
        j++;
    }
    for(i = 1;
        i < (processor->spectrum_len - 1) && j < ((double)processor->chunk_len/2 - 1) && processor->spectrum_cur[i+1].freq < ((double)processor->samplerate/2); i++) {
        freq_ratio = (processor->spectrum_cur[i+1].freq) / bin_size;
        while(j < freq_ratio) {
            processor->spectrum_cur[i].first_bin = audio_notzero(audio_min(processor->spectrum_cur[i].first_bin,j),j);
            processor->spectrum_cur[i].last_bin = audio_max(processor->spectrum_cur[i].last_bin,j);
            j++;
        }
    }
    for(; j < ((double)processor->chunk_len/2 + 1); j++) {
        processor->spectrum_cur[processor->spectrum_len-1].first_bin = audio_notzero(audio_min(processor->spectrum_cur[processor->spectrum_len-1].first_bin,j),j);
        processor->spectrum_cur[processor->spectrum_len-1].last_bin = audio_max(processor->spectrum_cur[processor->spectrum_len-1].last_bin,j);
    }

    return 1;
}

int
audio_processor_free(audio_processor *processor) {
    if(processor->samples) {
        ringbuf_free(&(processor->samples));
    }

    if(processor->window) free(processor->window);
    if(processor->output_buffer) free(processor->output_buffer);
    if(processor->plan) fftw_destroy_plan(processor->plan);
    if(processor->fftw_in) fftw_free(processor->fftw_in);
    if(processor->fftw_out) fftw_free(processor->fftw_out);
    if(processor->spectrum_cur) free(processor->spectrum_cur);
    fftw_cleanup();
    return 0;
}


#ifdef __cplusplus
}
#endif


