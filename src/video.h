#ifndef VIDEO_H
#define VIDEO_H
#include "ringbuf.h"

extern const char avi_header[326];

typedef struct avi_stream {
    unsigned int width;
    unsigned int height;
    unsigned int framerate;

    unsigned int samplerate;
    unsigned int channels;
    unsigned int samplesize;

    uint8_t avi_header[326];

    unsigned int video_frame_len;
    unsigned int audio_frame_len;
    unsigned int frame_len;
    unsigned int output_frame_rem;

    uint8_t *video_frame;
    uint8_t *audio_frame;
    uint8_t *video_frame_header;
    uint8_t *audio_frame_header;

    char *input_frame;
    char *output_frame;
    ringbuf_t frames;
} avi_stream;

#define AVI_STREAM_ZERO { \
  .width = 0, \
  .height = 0, \
  .framerate = 0, \
  .samplerate = 0, \
  .channels = 0, \
  .samplesize = 0, \
  .video_frame_len = 0, \
  .audio_frame_len = 0, \
  .frame_len = 0, \
  .video_frame = NULL, \
  .audio_frame = NULL, \
  .output_frame = NULL, \
  .video_frame_header = NULL, \
  .audio_frame_header = NULL, \
  .input_frame = NULL, \
  .frames = NULL, \
  .output_frame_rem = 0, \
}

#ifdef __cplusplus
extern "C" {
#endif

int
avi_stream_init(
  avi_stream *stream,
  unsigned int width,
  unsigned int height,
  unsigned int framerate,
  unsigned int samplerate,
  unsigned int channels,
  unsigned int samplesize);

size_t
avi_stream_write_header(avi_stream *stream, void *ctx, int(*w)(uint8_t *buf, size_t size, void *ctx));

int
avi_stream_free(avi_stream *stream);

#ifdef __cplusplus
}
#endif

#endif
