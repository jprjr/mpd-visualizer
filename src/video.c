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

    if(stream->video_frame) {
        free(stream->video_frame);
    }
    if(stream->audio_frame) {
        free(stream->audio_frame);
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
        strerr_warn3x("avi_stream_new: bad width ",width_str,", (width * 3) / 4 must be a whole number");
    }

    if( (height * 3) % 4 != 0) {
        uint_fmt(height_str,height);
        doit = 0;
        strerr_warn3x("avi_stream_new: bad height ",height_str,", (height * 3) / 4 must be a whole number");
    }

    if(!doit) {
        return 0;
    }

    stream->video_frame_len = sizeof(uint8_t) * width * height * 3;
    stream->video_frame = (uint8_t *)malloc(stream->video_frame_len);
    if(!stream->video_frame) return avi_stream_free(stream);

    stream->audio_frame_len = sizeof(uint8_t) * (samplerate / framerate) * channels * samplesize;
    stream->audio_frame = (uint8_t *)malloc(stream->audio_frame_len);
    if(!stream->audio_frame) return avi_stream_free(stream);

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

int
avi_stream_write_header(int fd, avi_stream *stream) {
    return fd_write(fd,(char *)stream->avi_header,326);
}

int
avi_stream_write_frame(int fd, avi_stream *stream) {
    int vh_test = 0;
    int vf_test = 0;
    int ah_test = 0;
    int af_test = 0;

    vh_test = fd_write(fd,(char *)stream->video_frame_header,8);
    if(vh_test == -1) {
        return -1;
    }

    vf_test = fd_write(fd,(char *)stream->video_frame,stream->video_frame_len);
    if(vf_test == -1) {
        return -1;
    }

    ah_test = fd_write(fd,(char *)stream->audio_frame_header,8);
    if(ah_test == -1) {
        return -1;
    }

    af_test = fd_write(fd,(char *)stream->audio_frame,stream->audio_frame_len);
    if(af_test == -1) {
        return -1;
    }
    return vh_test + vf_test + ah_test + af_test;

}

#ifdef __cplusplus
}
#endif
