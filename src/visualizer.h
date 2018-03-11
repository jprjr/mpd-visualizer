#ifndef VISUALIZER_H
#define VISUALIZER_H

#include "audio.h"
#include "lua-image.h"
#include "image.h"
#include "video.h"
#include "thread.h"
#include <skalibs/skalibs.h>
#include <mpd/client.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct visualizer {
    avi_stream stream;
    audio_processor processor;
    const char *lua_folder;
    const char *input_fifo;
    const char *output_fifo;
    char *buffer;
    int buffer_len;
    int bytes_to_read;
    struct mpd_connection *mpd_conn;
    struct mpd_status *mpd_stat;
    struct mpd_song *cur_song;
    struct mpd_message *cur_msg;
    genalloc lua_funcs;
    lua_State *Lua;
    thread_queue_t image_queue;
    image_q images[100];
    void (*lua_image_cb)(lua_State *L, intptr_t table_ref, unsigned int image_len, uint8_t *image);
    iopause_fd fds[3];
    int own_fifo;
    uint32_t nanosecs_per_frame;
    int reload;
} visualizer;

#define VISUALIZER_ZERO { \
  .stream = AVI_STREAM_ZERO, \
  .processor = AUDIO_PROCESSOR_ZERO, \
  .lua_folder = NULL, \
  .output_fifo = NULL, \
  .buffer = NULL, \
  .bytes_to_read = 0, \
  .mpd_conn = NULL, \
  .mpd_stat = NULL, \
  .cur_song = NULL, \
  .cur_msg = NULL, \
  .lua_funcs = GENALLOC_ZERO, \
  .Lua = NULL, \
  .lua_image_cb = NULL, \
  .fds = { \
    { .fd = -1, .events = 0, .revents = 0 }, \
    { .fd = -1, .events = 0, .revents = 0 }, \
    { .fd = -1, .events = 0, .revents = 0 }, \
  }, \
  .own_fifo = -1, \
  .nanosecs_per_frame = 1000000000 , \
  .reload = 0, \
}

#ifdef __cplusplus
extern "C" {
#endif

int
visualizer_init(visualizer *vis,
                unsigned int video_width,
                unsigned int video_height,
                unsigned int framerate,
                unsigned int samplerate,
                unsigned int channels,
                unsigned int samplesize,
                unsigned int bars,
                const char *input_fifo,
                const char *output_fifo,
                const char *lua_folder);

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
