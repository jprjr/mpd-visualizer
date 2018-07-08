#ifndef VISUALIZER_H
#define VISUALIZER_H

#include "audio.h"
#include "lua-image.h"
#include "image.h"
#include "video.h"
#include "thread.h"
#include "ringbuf.h"
#include <skalibs/skalibs.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct visualizer {
    avi_stream stream;
    audio_processor processor;
    unsigned int video_width;
    unsigned int video_height;
    unsigned int framerate;
    unsigned int samplerate;
    unsigned int channels;
    unsigned int samplesize;
    unsigned int bars;
    unsigned int mpd;
    unsigned int ms_per_frame;
    uint64_t elapsed_ms;
    uint32_t delay;
    int delay_active;
    const char *lua_folder;
    const char *input_fifo;
    const char *output_fifo;
    genalloc lua_funcs;
    lua_State *Lua;
    thread_queue_t image_queue;
    image_q images[100];
    void (*lua_image_cb)(lua_State *L, intptr_t table_ref, unsigned int image_len, uint8_t *image);
    iopause_fd fds[4];
    int own_fifo;
    int reload;
    const char *title;
    const char *artist;
    const char *album;
    const char *filename;
    char const *const *argv;
    int argc;
    int totaltime;
    ringbuf_t mpd_buf;
    ringbuf_t mpd_q;
    unsigned int mpd_state;
} visualizer;

#define VIS_MPD_READ_IDLE 0
#define VIS_MPD_READ_STATUS 1
#define VIS_MPD_READ_CURRENTSONG 2
#define VIS_MPD_READ_MESSAGE 3
#define VIS_MPD_SEND_IDLE 4
#define VIS_MPD_SEND_STATUS 5
#define VIS_MPD_SEND_CURRENTSONG 6
#define VIS_MPD_SEND_MESSAGE 7

#define VISUALIZER_ZERO { \
  .argv = NULL, \
  .argc = 0, \
  .stream = AVI_STREAM_ZERO, \
  .processor = AUDIO_PROCESSOR_ZERO, \
  .lua_folder = NULL, \
  .output_fifo = NULL, \
  .lua_funcs = GENALLOC_ZERO, \
  .Lua = NULL, \
  .lua_image_cb = NULL, \
  .fds = { \
    { .fd = -1, .events = 0, .revents = 0 }, \
    { .fd = -1, .events = 0, .revents = 0 }, \
    { .fd = -1, .events = 0, .revents = 0 }, \
    { .fd = -1, .events = 0, .revents = 0 }, \
  }, \
  .own_fifo = -1, \
  .ms_per_frame = 0 , \
  .reload = 0, \
  .video_width = 0, \
  .video_height = 0, \
  .framerate = 0, \
  .samplerate = 0, \
  .channels = 0, \
  .samplesize = 0, \
  .bars = 0, \
  .mpd = 1, \
  .totaltime = -1, \
  .elapsed_ms = 0, \
  .delay = 0, \
  .delay_active = 0, \
  .mpd_buf = NULL, \
  .mpd_q = NULL, \
  .mpd_state = VIS_MPD_READ_IDLE, \
}

#ifdef __cplusplus
extern "C" {
#endif

int
visualizer_init(visualizer *vis);

int
visualizer_loop(visualizer *vis);

int
visualizer_cleanup(visualizer *vis);

int
visualizer_reload(visualizer *vis);

int
visualizer_unload(visualizer *vis);

#ifdef __cplusplus
}
#endif

#endif
