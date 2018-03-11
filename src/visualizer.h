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
visualizer_grab_audio(visualizer *vis, int fd);

int
visualizer_make_frames(visualizer *vis);

int
visualizer_write_frame(visualizer *vis, int fd);

int
visualizer_free(visualizer *vis);

void
visualizer_load_scripts(visualizer *vis);

void
visualizer_set_image_cb(void (*lua_image_cb)(lua_State *L, intptr_t table_ref, unsigned int frames, uint8_t *image));

int
visualizer_loop(visualizer *vis);

#ifdef __cplusplus
extern "C" {
#endif

#endif
