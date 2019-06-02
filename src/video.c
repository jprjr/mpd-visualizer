#include <skalibs/strerr.h>
#include <skalibs/types.h>
#include <skalibs/allreadwrite.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "video.h"

#define format_dword(b,n) \
    *(b+0) = n; \
    *(b+1) = n >> 8; \
    *(b+2) = n >> 16; \
    *(b+3) = n >> 24;

#define format_long(b,n) format_dword(b,n)

#define format_word(b,n) \
    *(b+0) = n; \
    *(b+1) = n >> 8;

#ifdef __cplusplus
extern "C" {
#endif

int
avi_stream_free(avi_stream *stream) {
    if(!stream) return 0;

    if(stream->input_frame) {
        free(stream->input_frame);
    }

    if(stream->output_frame) {
        free(stream->output_frame);
    }

    if(stream->frames) {
        ringbuf_free(&(stream->frames));
    }

    return 0;
}

int
avi_stream_init(
  avi_stream *stream,
  unsigned int width,
  unsigned int height,
  unsigned int framerate,
  unsigned int samplerate,
  unsigned int channels,
  unsigned int samplesize) {

    char width_str[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    char height_str[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int doit = 1;

    if(!stream) return 0;

    if( (width * 3) % 4 != 0) {
        uint_fmt(width_str,width);
        doit = 0;
        strerr_warn3x("error: bad width ",width_str,", (width * 3) / 4 must be a whole number");
    }

    if( (height * 3) % 4 != 0) {
        uint_fmt(height_str,height);
        doit = 0;
        strerr_warn3x("error: bad height ",height_str,", (height * 3) / 4 must be a whole number");
    }

    if(!doit) {
        return 0;
    }

    stream->framerate = framerate;
    stream->video_frame_len = sizeof(uint8_t) * width * height * 3;
    stream->audio_frame_len = sizeof(uint8_t) * (samplerate / framerate) * channels * samplesize;
    stream->frame_len = stream->video_frame_len + stream->audio_frame_len + 16;

    stream->input_frame = (char *)malloc(stream->frame_len);
    if(!stream->input_frame) return avi_stream_free(stream);

    stream->output_frame = (char *)malloc(stream->frame_len);
    if(!stream->output_frame) return avi_stream_free(stream);
    memset(stream->output_frame,0,stream->frame_len);

    stream->video_frame_header = (uint8_t *)stream->input_frame;
    stream->video_frame = stream->video_frame_header + 8;
    stream->audio_frame_header = stream->video_frame + stream->video_frame_len;
    stream->audio_frame = stream->audio_frame_header + 8;
    memset(stream->input_frame,0,stream->frame_len);

    stream->frames = ringbuf_new(framerate * stream->frame_len);
    if(!stream->frames) {
        return avi_stream_free(stream);
    }

    memcpy(stream->avi_header,avi_header,326);

    format_dword(stream->avi_header + 32,1000000 / framerate);
    format_dword(stream->avi_header + 36,stream->video_frame_len);
    format_dword(stream->avi_header + 60,stream->video_frame_len);
    format_dword(stream->avi_header + 64,width);
    format_dword(stream->avi_header + 68,height);
    format_dword(stream->avi_header + 132,framerate);
    format_dword(stream->avi_header + 144,stream->video_frame_len);
    format_long(stream->avi_header + 176,width);
    format_long(stream->avi_header + 180,height);
    format_long(stream->avi_header + 192,stream->video_frame_len);
    format_dword(stream->avi_header + 256,samplerate);
    format_dword(stream->avi_header + 268,samplerate * samplesize * channels);
    format_dword(stream->avi_header + 276,samplesize * channels);
    format_dword(stream->avi_header + 300,samplerate);
    format_dword(stream->avi_header + 304,samplerate * samplesize * channels);
    format_word(stream->avi_header + 308,samplesize * channels);
    format_word(stream->avi_header + 310,samplesize * 8);

    memcpy(stream->video_frame_header,"00db",4);
    format_dword(stream->video_frame_header+4,stream->video_frame_len);

    memcpy(stream->audio_frame_header,"01wb",4);
    format_dword(stream->audio_frame_header+4,stream->audio_frame_len);

    return 1;
}

size_t
avi_stream_write_header(avi_stream *stream, void *ctx, int(*w)(uint8_t *buf, size_t size, void *ctx)) {
    return w(stream->avi_header,326,ctx);
}

#ifdef __cplusplus
}
#endif
