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
#define AMP_MAX 70.0f
#define AMP_MIN 70.0f
#define AMP_BOOST 1.8f

#ifdef __cplusplus
extern "C" {
#endif

static double itur_468(double freq) {
    /* only calculate this for freqs > 1000 */
    if(freq >= 1000.0f) {
        return 0.0f;
    }
    double h1 = (-4.737338981378384 * pow(10,-24) * pow(freq,6)) +
                ( 2.043828333606125 * pow(10,-15) * pow(freq,4)) -
                ( 1.363894795463638 * pow(10,-7)  * pow(freq,2)) +
                1;
    double h2 = ( 1.306612257412824 * pow(10,-19) * pow(freq,5)) -
                ( 2.118150887518656 * pow(10,-11) * pow(freq,3)) +
                ( 5.559488023498642 * pow(10,-4)  * freq);
    double r1 = ( 1.246332637532143 * pow(10,-4) * freq ) /
                sqrt(pow(h1,2) + pow(h2,2));
    return 18.2f + (20.0f * log10(r1));
}

static double window_none(int i, int n) {
    (void)i;
    (void)n;
    return 1.0f;
}

static double window_hann(int i, int n) {
    return 0.50f * (1.0f - cos( (2.0f * M_PI *i)/((double)n-1.0f)));
}

static double window_blackman(int i, int n) {
    double a_0 = 0.42f;
    double a_1 = 0.5f;
    double a_2 = 0.08f;
    return a_0 - ( a_1 * cos( (2.0f * M_PI * i) / ((double)n - 1.0f) )) + ( a_2 * cos( (4.0f * M_PI * i)/ ( (double)n - 1) ) );
}

static double window_blackman_harris(int i, int n) {
    double a = (2.0f * M_PI) / (n - 1);
    return 0.35875 - 0.48829*cos(a*i) + 0.14128*cos(2*a*i) - 0.01168*cos(3*a*i);
}

static double find_amplitude_low(fftw_complex *out, unsigned int start, unsigned int end, unsigned int chunk_len) {
    (void)end;
    return 20.0f * log10(2.0f * cabs(out[start]) / chunk_len);
}

static double find_amplitude_mid(fftw_complex *out, unsigned int start, unsigned int end, unsigned int chunk_len) {
    unsigned int mid = start + ((end - start) / 2);
    return 20.0f * log10(2.0f * cabs(out[mid]) / chunk_len);
}

static double find_amplitude_avg(fftw_complex *out, unsigned int start, unsigned int end, unsigned int chunk_len) {
    unsigned int i;
    unsigned int len = end - start;
    double avg = 0.0f;
    for(i=start;i<=end;i++) {
        avg += 20.0f * log10(2.0f * cabs(out[i]) / chunk_len);
    }
    return avg / len;

}

static double find_amplitude_max(fftw_complex *out, unsigned int start, unsigned int end, unsigned int chunk_len) {
    unsigned int i = 0;
    double val = -INFINITY;
    double tmp = 0.0f;
    for(i=start;i<=end;i++) {
        tmp = 20.0f * log10(2.0f * cabs(out[i]) / chunk_len);
        /* see https://groups.google.com/d/msg/comp.dsp/cZsS1ftN5oI/rEjHXKTxgv8J */
        val = audio_max(tmp,val);
    }
    return val;
}


static void mono_downmix(audio_processor *processor) {
    unsigned int i = 0;
    unsigned int s = 0;
    unsigned int o = processor->chunk_len - processor->sample_window_len;
    int32_t m = 0;

    char *buffer = processor->output_buffer;

    while(i<o) {
        processor->fftw_buffer[i] = processor->fftw_buffer[o+i];
        processor->fftw_in[i] = processor->fftw_buffer[i];
        processor->fftw_in[i] *= processor->window[i];
        i++;
    }

    i=0;

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
        processor->fftw_buffer[o+i] = (double)m / processor->sample_max_val;
        processor->fftw_in[o+i] = processor->fftw_buffer[o+i] * processor->window[i+o];

        i++;
    }
}


static void stereo_downmix(audio_processor *processor) {
    unsigned int i = 0;
    unsigned int s = 0;
    unsigned int o = processor->chunk_len - processor->sample_window_len;
    int32_t l = 0;
    int32_t r = 0;
    int32_t m = 0;
    int64_t t = 0;

    char *buffer = processor->output_buffer;

    while(i+processor->sample_window_len < processor->chunk_len) {
        processor->fftw_buffer[i] = processor->fftw_buffer[processor->sample_window_len+i];
        processor->fftw_in[i] = processor->fftw_buffer[i];
        processor->fftw_in[i] *= processor->window[i];
        i++;
    }

    i=0;

    while(i<processor->sample_window_len) {
        s = i * processor->samplesize * 2;
        switch(processor->samplesize) {
            case 1: {
                l = buffer[s];
                r = buffer[s+1];
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
        t = l;
        t += r;
        m = t / 2;
        processor->fftw_buffer[o+i] = (double)m / processor->sample_max_val;
        processor->fftw_in[o+i] = processor->fftw_buffer[o+i] * processor->window[i+o];
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
        processor->plan = fftw_plan_dft_r2c_1d(processor->chunk_len,processor->fftw_in,processor->fftw_out,FFTW_MEASURE);
    }

    fftw_execute(processor->plan);

    for(i=0;i<processor->spectrum_len;i++) {
        processor->spectrum_cur[i].amp = find_amplitude_max(processor->fftw_out,processor->spectrum_cur[i].first_bin,processor->spectrum_cur[i].last_bin,processor->chunk_len);

        if(!isfinite(processor->spectrum_cur[i].amp)) {
            processor->spectrum_cur[i].amp = -999.0f; /* filtered out next line */
        }

        processor->spectrum_cur[i].amp += processor->spectrum_cur[i].boost;

        if(processor->spectrum_cur[i].amp <= -AMP_MIN) {
            processor->spectrum_cur[i].amp = -AMP_MIN;
        }

        processor->spectrum_cur[i].amp += AMP_MIN;

        if(processor->spectrum_cur[i].amp > AMP_MAX) {
            processor->spectrum_cur[i].amp = AMP_MAX;
        }

        processor->spectrum_cur[i].amp /= AMP_MAX;

        processor->spectrum_cur[i].amp *= AMP_BOOST; /* i seem to rarely get results near 1.0, let's give this a boost */

        if(processor->spectrum_cur[i].amp > 1.0f) {
            processor->spectrum_cur[i].amp = 1.0f;
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
    double bin_size = 0.0f;
    double freq_min = 50.0f;
    double freq_max = processor->samplerate / 2;
    if(freq_max > 10000.0f) {
        freq_max = 10000.0f;
    }
    double octaves = ceil(log2(freq_max / freq_min));
    double interval = 1.0f / (octaves / (double)processor->spectrum_len);
    /*
    if(ceil(interval) - interval <= 0.5) {
        interval = ceil(interval);
    } else {
        interval = floor(interval);
    }
    */

    if(processor->channels > 2) {
        strerr_warn1x("error: too many channels, max is 2");
        return 0;
    }
    if(processor->samplesize > 3) {
        strerr_warn1x("error: too big sample size, max is 3");
        return 0;
    }

    processor->chunk_len = 4096;
    processor->samples_available = 0;

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

    processor->samples = ringbuf_new(processor->chunk_len * processor->samplesize * processor->channels, NULL, NULL, NULL, NULL);
    if(!processor->samples) {
        return audio_processor_free(processor);
    }

    processor->window = (double *)malloc(sizeof(double) * processor->chunk_len);
    if(!processor->window) {
        return audio_processor_free(processor);
    }
    for(i=0;i<processor->chunk_len;i++) {
        processor->window[i] = window_blackman_harris(i,processor->chunk_len);
    }

    processor->fftw_buffer = (double *)fftw_malloc(sizeof(double) * processor->chunk_len);
    if(!processor->fftw_buffer) {
        return audio_processor_free(processor);
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
    memset(processor->fftw_buffer,0,sizeof(double) * processor->chunk_len);
    memset(processor->fftw_in,0,sizeof(double) * processor->chunk_len);

    processor->spectrum_cur = (frange *)malloc(sizeof(frange) * (processor->spectrum_len + 1));
    if(!processor->spectrum_cur) {
        return audio_processor_free(processor);
    }


    for(i=0;i<processor->spectrum_len+1;i++) {
        processor->spectrum_cur[i].amp = 0.0f;
        processor->spectrum_cur[i].prevamp = 0.0f;
        if(i==0) {
            processor->spectrum_cur[i].freq = freq_min;
        }
        else {
            /* see http://www.zytrax.com/tech/audio/calculator.html#centers_calc */
            processor->spectrum_cur[i].freq = processor->spectrum_cur[i-1].freq * pow(10, 3 / (10 * interval));
        }

        /* fudging this a bit to avoid overlap */
        double upper_freq = processor->spectrum_cur[i].freq * pow(10, (3 * 1) / (10 * 2 * floor(interval)));
        double lower_freq = processor->spectrum_cur[i].freq / pow(10, (3 * 1) / (10 * 2 * ceil(interval)));

        processor->spectrum_cur[i].first_bin = (unsigned int)floor(lower_freq / bin_size);
        processor->spectrum_cur[i].last_bin = (unsigned int)floor(upper_freq / bin_size);

        if(processor->spectrum_cur[i].last_bin > processor->chunk_len / 2) {
            processor->spectrum_cur[i].last_bin = processor->chunk_len / 2;
        }
        /* figure out the ITU-R 468 weighting to apply */
        processor->spectrum_cur[i].boost = itur_468(processor->spectrum_cur[i].freq);

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
    if(processor->fftw_buffer) fftw_free(processor->fftw_buffer);
    if(processor->spectrum_cur) free(processor->spectrum_cur);
    fftw_cleanup();
    return 0;
}


#ifdef __cplusplus
}
#endif


