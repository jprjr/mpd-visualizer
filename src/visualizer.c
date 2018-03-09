#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <skalibs/skalibs.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "audio.h"
#include "visualizer.h"
#include "stream.lua.lh"
#include "font.lua.lh"
#include "tinydir.h"
#include "image.h"
#include "lua-audio.h"
#include "lua-image.h"
#include "lua-file.h"


#define func_list_len(g) genalloc_len(lua_func_list,g)
#define func_list_s(g) genalloc_s(lua_func_list,g)
#define func_list_free(g) genalloc_free(lua_func_list,g)
#define func_list_append(g, p) genalloc_append(lua_func_list,g, p)

static visualizer *global_vis;

typedef struct lua_func_list {
    int frame_ref;
    int reload_ref;
    time_t mtime;
    stralloc filename;
} lua_func_list;

#define LUA_FUNC_LIST_ZERO { \
  .frame_ref = -1, \
  .reload_ref = -1, \
  .mtime = -1, \
  .filename = STRALLOC_ZERO, \
}

#ifdef __cplusplus
extern "C" {
#endif

static lua_func_list *
lua_func_list_find(genalloc *list, const char *filename) {
    lua_func_list *func = NULL;
    unsigned int i = 0;

    for(i=0;i<func_list_len(list);i++) {
        if(strcmp(
            filename,
            func_list_s(list)[i].filename.s) == 0) {
            func = &(func_list_s(list)[i]);
            break;
        }
    }
    return func;
}

void
visualizer_set_image_cb(void (*lua_image_cb)(lua_State *L, intptr_t table_ref, unsigned int frames, uint8_t *image)) {
    global_vis->lua_image_cb = lua_image_cb;
}

static void lua_func_list_free(visualizer *vis, genalloc *list) {
    unsigned long i;
    for(i=0;i<func_list_len(list);i++) {
        if(func_list_s(list)[i].frame_ref != -1) {
            luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].frame_ref );
        }
        if(func_list_s(list)[i].reload_ref != -1) {
            luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].reload_ref );
        }
        luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].reload_ref );
        stralloc_free(&(func_list_s(list)[i].filename));
    }
    func_list_free(list);
}

void visualizer_load_scripts(visualizer *vis) {
    tinydir_dir dir;
    unsigned long int i;

    if(tinydir_open_sorted(&dir,vis->lua_folder) == -1) return;

    for(i=0; i < dir.n_files; i++) {
        tinydir_file file;
        tinydir_readfile_n(&dir,&file,i);
        struct stat st;

        if(file.is_dir) {
            continue;
        }
        stat(file.path,&st);

        lua_func_list new = LUA_FUNC_LIST_ZERO;
        lua_func_list *cur = lua_func_list_find(&(vis->lua_funcs),file.path);

        if(cur == NULL) {
            stralloc_copys(&(new.filename),file.path);
            stralloc_0(&(new.filename));
            func_list_append(&(vis->lua_funcs),&new);
            cur = &(func_list_s(&(vis->lua_funcs))[func_list_len(&(vis->lua_funcs)) - 1]);
        }

        if(luaL_loadfile(vis->Lua,file.path)) {
            strerr_warn4x("Failed to load ",file.path, ": ",lua_tostring(vis->Lua,-1));
            lua_settop(vis->Lua,0);
            continue;
        }
        if(lua_pcall(vis->Lua,0,1,0)) {
            strerr_warn4x("Failed to run ",file.path, ": ",lua_tostring(vis->Lua,-1));
            lua_settop(vis->Lua,0);
            continue;
        }
        if(lua_isfunction(vis->Lua,-1)) {
            if(cur->frame_ref != -1) {
                luaL_unref(vis->Lua,LUA_REGISTRYINDEX,cur->frame_ref);
            }
            cur->frame_ref = luaL_ref(vis->Lua,LUA_REGISTRYINDEX);
            cur->mtime = st.st_mtime;
        }
        else if(lua_istable(vis->Lua,-1)) {
            lua_getfield(vis->Lua,-1,"onframe");
            if(lua_isfunction(vis->Lua,-1)) {
                if(cur->frame_ref != -1) {
                    luaL_unref(vis->Lua,LUA_REGISTRYINDEX,cur->frame_ref);
                }
                cur->frame_ref = luaL_ref(vis->Lua,LUA_REGISTRYINDEX);
            }
            else {
                lua_pop(vis->Lua,1);
            }

            lua_getfield(vis->Lua,-1,"onreload");
            if(lua_isfunction(vis->Lua,-1)) {
                if(cur->reload_ref != -1) {
                    luaL_unref(vis->Lua,LUA_REGISTRYINDEX,cur->reload_ref);
                }
                cur->reload_ref = luaL_ref(vis->Lua,LUA_REGISTRYINDEX);
            }
            else {
                lua_pop(vis->Lua,1);
            }

            lua_getfield(vis->Lua,-1,"onload");
            if(lua_isfunction(vis->Lua,-1) && cur->mtime == -1) {
                if(lua_pcall(vis->Lua,0,0,0)) {
                    strerr_warn4x("Error calling ",file.path,":onload, ",lua_tostring(vis->Lua,-1));
                }
            }
            else {
                lua_pop(vis->Lua,1);
            }

            if(cur->reload_ref != -1 && cur->mtime != -1) {
                lua_rawgeti(vis->Lua,LUA_REGISTRYINDEX,cur->reload_ref);
                if(lua_pcall(vis->Lua,0,0,0)) {
                    strerr_warn4x("Error calling ",file.path,"onreload, ",lua_tostring(vis->Lua,-1));
                }
            }
            cur->mtime = st.st_mtime;

        }
        lua_settop(vis->Lua,0);
    }

    tinydir_close(&dir);

}

int
visualizer_free(visualizer *vis) {
    lua_func_list_free(vis, &(vis->lua_funcs));
    thread_queue_term(&(vis->image_queue));
    avi_stream_free(&(vis->stream));
    audio_processor_free(&(vis->processor));

    if(vis->buffer) free(vis->buffer);
    if(vis->mpd_stat) mpd_status_free(vis->mpd_stat);
    if(vis->cur_song) mpd_song_free(vis->cur_song);
    if(vis->cur_msg) mpd_message_free(vis->cur_msg);
    if(vis->mpd_conn) mpd_connection_free(vis->mpd_conn);
    if(vis->tain_offset) free(vis->tain_offset);
    if(vis->tain_cur) free(vis->tain_cur);
    if(vis->tain_last) free(vis->tain_last);
    if(vis->Lua) {
        luaclose_image();
        lua_close(vis->Lua);
    }
    return 0;
}

int visualizer_grab_audio(visualizer *vis, int fd) {
    int bytes_read = 0;
    int samples_read = 0;
    bytes_read = fd_read(fd,vis->buffer,vis->bytes_to_read);
    if(bytes_read <= 0) {
        strerr_warn1sys("Problem grabbing audio data: ");
        return bytes_read;
    }

    cbuffer_put(&(vis->processor.samples),vis->buffer,bytes_read);

    samples_read = bytes_read / (vis->processor.channels * vis->processor.samplesize);

    vis->processor.samples_available += samples_read;

    return samples_read;
}


static void visualizer_free_metadata(visualizer *vis) {
    if(vis->mpd_stat) {
        mpd_status_free(vis->mpd_stat);
        vis->mpd_stat = NULL;
    }
    if(vis->cur_song) {
        mpd_song_free(vis->cur_song);
        vis->cur_song = NULL;
    }
    if(vis->cur_msg) {
        mpd_message_free(vis->cur_msg);
        vis->cur_msg = NULL;
    }
}

static void visualizer_get_metadata(visualizer *vis) {
    if(!vis->mpd_conn) {
        return;
    }

    const char *title = NULL;
    const char *album = NULL;
    const char *artist = NULL;
    const char *file = NULL;
    const char *message = NULL;

    if(!mpd_send_status(vis->mpd_conn)) {
        strerr_warn2x("Error sending status: ",mpd_connection_get_error_message(vis->mpd_conn));
        return;
    }
    vis->mpd_stat = mpd_recv_status(vis->mpd_conn);
    if(!vis->mpd_stat) {
        strerr_warn2x("Error receiving status: ",mpd_connection_get_error_message(vis->mpd_conn));
        return;
    }
    mpd_response_finish(vis->mpd_conn);

    lua_getglobal(vis->Lua,"song");
    lua_pushinteger(vis->Lua,mpd_status_get_song_id(vis->mpd_stat));
    lua_setfield(vis->Lua,-2,"id");

    lua_pushinteger(vis->Lua,mpd_status_get_elapsed_time(vis->mpd_stat));
    lua_setfield(vis->Lua,-2,"elapsed");

    lua_pushinteger(vis->Lua,mpd_status_get_total_time(vis->mpd_stat));
    lua_setfield(vis->Lua,-2,"total");

    if(!mpd_send_current_song(vis->mpd_conn)) {
        strerr_warn2x("Error sending currentsong: ",mpd_connection_get_error_message(vis->mpd_conn));
        return;
    }
    vis->cur_song = mpd_recv_song(vis->mpd_conn);
    if(!vis->cur_song) {
        strerr_warn2x("Error receiving currentsong: ",mpd_connection_get_error_message(vis->mpd_conn));
        return;
    }
    mpd_response_finish(vis->mpd_conn);

    title = mpd_song_get_tag(vis->cur_song,MPD_TAG_TITLE,0);
    album    = mpd_song_get_tag(vis->cur_song,MPD_TAG_ALBUM,0);
    artist   = mpd_song_get_tag(vis->cur_song,MPD_TAG_ARTIST,0);
    file     = mpd_song_get_uri(vis->cur_song);

    if(title) {
        lua_pushlstring(vis->Lua,title,strlen(title));
    }
    else {
        lua_pushnil(vis->Lua);
    }
    lua_setfield(vis->Lua,-2,"title");

    if(album) {
        lua_pushlstring(vis->Lua,album,strlen(album));
    }
    else {
        lua_pushnil(vis->Lua);
    }
    lua_setfield(vis->Lua,-2,"album");

    if(artist) {
        lua_pushlstring(vis->Lua,artist,strlen(artist));
    }
    else {
        lua_pushnil(vis->Lua);
    }
    lua_setfield(vis->Lua,-2,"artist");

    lua_pushlstring(vis->Lua,file,strlen(file));
    lua_setfield(vis->Lua,-2,"file");

    if(!mpd_send_read_messages(vis->mpd_conn)) {
        strerr_warn2x("Error sending readmessages: ",mpd_connection_get_error_message(vis->mpd_conn));
        return;
    }

    vis->cur_msg = mpd_recv_message(vis->mpd_conn);
    if(vis->cur_msg) {
        message = mpd_message_get_text(vis->cur_msg);
        lua_pushlstring(vis->Lua,message,strlen(message));
    }
    else {
        lua_pushnil(vis->Lua);
    }
    lua_setfield(vis->Lua,-2,"message");

    mpd_response_finish(vis->mpd_conn);
}

int visualizer_write_frames(visualizer *vis, int fd) {
    int bytes = 0;

    if(cbuffer_len(&(vis->stream.frames)) < vis->stream.frame_len) {
        return 0;
    }

    if(vis->stream.output_frame_len == 0) {
        cbuffer_get(&(vis->stream.frames),vis->stream.output_frame,vis->stream.frame_len);
        vis->stream.output_frame_len = vis->stream.frame_len;
    }

    bytes = fd_write(fd,vis->stream.output_frame + (vis->stream.frame_len - vis->stream.output_frame_len),vis->stream.output_frame_len);

    if(bytes < 0) {
        vis->stream.output_frame_len = 0;
        return -1;
    }

    vis->stream.output_frame_len -= (unsigned int)bytes;

    return 1;
}


int visualizer_make_frames(visualizer *vis) {
    int frames = 0;
    unsigned long i = 0;
    image_q *q = NULL;

    while(vis->processor.samples_available >= vis->processor.sample_window_len && vis->processor.samples_available > 0) {
        audio_processor_fftw(&(vis->processor));
        if(vis->processor.firstflag == 0) {
            vis->processor.firstflag = 1;
        }

        memcpy(vis->stream.audio_frame,
               vis->processor.output_buffer,
               vis->stream.audio_frame_len);

        visualizer_get_metadata(vis);
        memset(vis->stream.video_frame,0,vis->stream.video_frame_len);

        while(thread_queue_count(&(vis->image_queue)) > 0) {
            q = thread_queue_consume(&(vis->image_queue));
            if(q != NULL) {
                vis->lua_image_cb(vis->Lua,q->table_ref,q->frames,q->image);

                luaL_unref(vis->Lua,LUA_REGISTRYINDEX,q->table_ref);
                free(q->filename);
                free(q);
                q = NULL;
            }
        }

        for(i=0;i<func_list_len(&(vis->lua_funcs));i++) {
            if(func_list_s(&(vis->lua_funcs))[i].frame_ref != -1) {
                lua_rawgeti(vis->Lua,LUA_REGISTRYINDEX,func_list_s(&(vis->lua_funcs))[i].frame_ref);
                if(lua_isfunction(vis->Lua,-1)) {
                    if(lua_pcall(vis->Lua,0,0,0)) {
                        strerr_warn3x(func_list_s(&(vis->lua_funcs))[i].filename.s,": ",lua_tostring(vis->Lua,-1));
                    }
                }
            }

            lua_settop(vis->Lua,0);
        }

        visualizer_free_metadata(vis);

        wake_queue();

        cbuffer_put(&(vis->stream.frames),vis->stream.input_frame,vis->stream.frame_len);

        vis->processor.samples_available -= vis->processor.sample_window_len;
        frames++;
    }

    return frames;
}

int
visualizer_init(visualizer *vis,
                unsigned int video_width,
                unsigned int video_height,
                unsigned int framerate,
                unsigned int samplerate,
                unsigned int channels,
                unsigned int samplesize,
                unsigned int bars,
                const char *output_fifo,
                const char *lua_folder) {
    if(!vis) return 0;

    global_vis = vis;

    vis->lua_image_cb = &lua_load_image_cb;

    vis->output_fifo = output_fifo;
    vis->lua_folder = lua_folder;

    thread_queue_init(&(vis->image_queue),100,(void **)&(vis->images),0);

    if(!avi_stream_init(
        &(vis->stream),
        video_width,
        video_height,
        framerate,
        samplerate,
        channels,
        samplesize)) {
        return visualizer_free(vis);
    }

    vis->processor.framerate = framerate;
    vis->processor.channels = channels;
    vis->processor.samplerate = samplerate;
    vis->processor.samplesize = samplesize;
    vis->processor.spectrum_len = bars;

    if(!audio_processor_init(&(vis->processor))) {
        return visualizer_free(vis);
    }

    vis->buffer_len = sizeof(uint8_t) * samplerate * channels * samplesize * 4;

    vis->buffer = (char *)malloc(vis->buffer_len);
    if(!vis->buffer) {
        visualizer_free(vis);
        return 0;
    }

    vis->bytes_to_read = (samplerate / framerate) * channels * samplesize * 4;

    vis->mpd_conn = mpd_connection_new(0,0,0);
    if(!vis->mpd_conn) {
        visualizer_free(vis);
        return 0;
    }
    if(mpd_connection_get_error(vis->mpd_conn) != MPD_ERROR_SUCCESS) {
        mpd_connection_free(vis->mpd_conn);
        vis->mpd_conn = 0;
    }
    mpd_run_subscribe(vis->mpd_conn,"visualizer");

    vis->tain_offset = (tain_t *)malloc(sizeof(tain_t));
    if(!vis->tain_offset) return visualizer_free(vis);

    vis->tain_cur = (tain_t *)malloc(sizeof(tain_t));
    if(!vis->tain_cur) return visualizer_free(vis);

    vis->tain_last = (tain_t *)malloc(sizeof(tain_t));
    if(!vis->tain_last) return visualizer_free(vis);

    vis->Lua = 0;
    vis->Lua = luaL_newstate();
    if(!vis->Lua) {
        visualizer_free(vis);
        return 0;
    }

    luaL_openlibs(vis->Lua);
    luaopen_image(vis->Lua,&(vis->image_queue));
    luaopen_file(vis->Lua);

    if(luaL_loadbuffer(vis->Lua,font_lua,font_lua_length-1,"font.lua")) {
        strerr_die2x(1,"Error loading font.lua: ",lua_tostring(vis->Lua,-1));
    }

    if(lua_pcall(vis->Lua,0,1,0)) {
        strerr_die2x(1,"Error running font.lua: ",lua_tostring(vis->Lua,-1));
    }

    lua_setglobal(vis->Lua,"font");
    lua_settop(vis->Lua,0);

    lua_newtable(vis->Lua);
    lua_setglobal(vis->Lua,"song");
    lua_settop(vis->Lua,0);

    lua_getglobal(vis->Lua,"image");
    lua_getfield(vis->Lua,-1,"new");
    lua_pushnil(vis->Lua);
    lua_pushinteger(vis->Lua,video_width);
    lua_pushinteger(vis->Lua,video_height);
    lua_pushinteger(vis->Lua,3);
    if(lua_pcall(vis->Lua,4,1,0)) {
        strerr_die2x(1,"Error creating stream image: ",lua_tostring(vis->Lua,-1));
    }

    lua_getfield(vis->Lua,-1,"frames");
    lua_rawgeti(vis->Lua,-1,1);

    lua_pushinteger(vis->Lua,framerate);
    lua_setfield(vis->Lua,-2,"framerate");

    lua_pushlightuserdata(vis->Lua,vis->stream.video_frame);
    lua_setfield(vis->Lua,-2,"image");

    lua_newtable(vis->Lua);
    lua_pushvalue(vis->Lua,-2);
    lua_setfield(vis->Lua,-2,"video");

    luaopen_audio(vis->Lua,&(vis->processor));
    lua_setfield(vis->Lua,-2,"audio");

    lua_setglobal(vis->Lua,"stream");
    lua_settop(vis->Lua,0);

    if(luaL_loadbuffer(vis->Lua,stream_lua,stream_lua_length - 1,"stream.lua")) {
        strerr_die2x(1,"Error loading stream.lua: ",lua_tostring(vis->Lua,-1));
    }

    if(lua_pcall(vis->Lua,0,0,0)) {
        strerr_die2x(1,"Error calling stream.lua: ",lua_tostring(vis->Lua,-1));
    }

    if(vis->lua_folder) visualizer_load_scripts(vis);

    return 1;

}

#ifdef __cplusplus
}
#endif
