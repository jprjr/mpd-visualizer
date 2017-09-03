#ifndef VIDEO_H
#define VIDEO_H

extern const char avi_header[326];

typedef struct avi_stream {
    unsigned int width;
    unsigned int height;
    unsigned int framerate;

    unsigned int samplerate;
    unsigned int channels;
    unsigned int samplesize;

    uint8_t avi_header[326];

    uint8_t video_frame_header[8];
    uint8_t audio_frame_header[8];

    unsigned int video_frame_len;
    unsigned int audio_frame_len;

    uint8_t *video_frame;
    uint8_t *audio_frame;
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
  .video_frame = NULL, \
  .audio_frame = NULL, \
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

int
avi_stream_write_header(int fd, avi_stream *stream);

int
avi_stream_write_frame(int fd, avi_stream *stream);

int
avi_stream_free(avi_stream *stream);

#ifdef __cplusplus
}
#endif

#endif
