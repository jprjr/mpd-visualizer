#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <skalibs/skalibs.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>

#include "audio.h"
#include "visualizer-int.h"
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

static void
lua_func_list_free(visualizer *vis, genalloc *list) {
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

static void
visualizer_load_scripts(visualizer *vis) {
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

static int
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
    if(vis->Lua) {
        luaclose_image();
        lua_close(vis->Lua);
    }
    return 0;
}

static int
visualizer_grab_audio(visualizer *vis, int fd) {
    int bytes_read = 0;
    int samples_read = 0;

    bytes_read = fd_read(fd,vis->buffer,8192);
    if(bytes_read <= 0) {
        strerr_warn1sys("Problem grabbing audio data: ");
        return bytes_read;
    }
    ringbuf_memcpy_into(
        vis->processor.samples,
        vis->buffer,
        bytes_read);

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
    if(!vis->mpd) {
        lua_getglobal(vis->Lua,"song");
        lua_pushinteger(vis->Lua,vis->elapsed_ms / 1000);
        lua_setfield(vis->Lua,-2,"elapsed");
        return;
    }

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

    vis->elapsed_ms = mpd_status_get_elapsed_ms(vis->mpd_stat);

    lua_getglobal(vis->Lua,"song");
    lua_pushinteger(vis->Lua,mpd_status_get_song_id(vis->mpd_stat));
    lua_setfield(vis->Lua,-2,"id");

    lua_pushinteger(vis->Lua,vis->elapsed_ms / 1000);
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


static int
visualizer_make_frames(visualizer *vis) {
    int frames = 0;
    unsigned long i = 0;
    image_q *q = NULL;

    while(vis->processor.samples_available >= vis->processor.sample_window_len) {
        vis->elapsed_ms += vis->ms_per_frame;
        audio_processor_fftw(&(vis->processor));
        if(vis->processor.firstflag == 0) {
            vis->processor.firstflag = 1;
        }

        memcpy(vis->stream.audio_frame,
               vis->processor.output_buffer,
               vis->stream.audio_frame_len);

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

        lua_getglobal(vis->Lua,"song");
        lua_pushinteger(vis->Lua,vis->elapsed_ms / 1000);
        lua_setfield(vis->Lua,-2,"elapsed");

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

        lua_gc(vis->Lua,LUA_GCCOLLECT,0);

        wake_queue();

        if(ringbuf_bytes_free(vis->stream.frames) < vis->stream.frame_len) {
            fprintf(stderr,"Warning: overwriting old video frame\n");
        }
        ringbuf_memcpy_into(vis->stream.frames,vis->stream.input_frame,vis->stream.frame_len);

        vis->processor.samples_available -= vis->processor.sample_window_len;
        frames++;
    }

    return frames;
}

int
visualizer_reload(visualizer *vis) {
    vis->fds[0].fd = selfpipe_init();
    if(selfpipe_trap(SIGINT) < 0) {
        strerr_die1sys(1,"Unable to trap SIGINT: ");
    }
    if(selfpipe_trap(SIGTERM) < 0) {
        strerr_die1sys(1,"Unable to trap SIGTERM: ");
    }
    if(selfpipe_trap(SIGPIPE) < 0) {
        strerr_die1sys(1,"Unable to trap SIGPIPE: ");
    }
    if(selfpipe_trap(SIGUSR1) < 0) {
        strerr_die1sys(1,"Unable to trap SIGUSR1: ");
    }
    if(selfpipe_trap(SIGUSR2) < 0) {
        strerr_die1sys(1,"Unable to trap SIGUSR2: ");
    }
    global_vis = vis;
    luaimage_setup_threads(&(vis->image_queue));
    audio_processor_reload(&(vis->processor));
    return 1;
}

int
visualizer_unload(visualizer *vis) {
    (void)(vis);
    selfpipe_untrap(SIGINT);
    selfpipe_untrap(SIGTERM);
    selfpipe_untrap(SIGPIPE);
    selfpipe_untrap(SIGUSR1);
    selfpipe_untrap(SIGUSR2);
    selfpipe_finish();
    luaimage_stop_threads();
    return 1;
}

int
visualizer_init(visualizer *vis) {
    if(!vis) return 0;
    if(
        !vis->video_width ||
        !vis->video_height ||
        !vis->framerate ||
        !vis->samplerate ||
        !vis->channels ||
        !vis->samplesize ||
        !vis->bars ||
        !vis->input_fifo ||
        !vis->output_fifo ) return 0;

    global_vis = vis;

    pthread_t this_thread = pthread_self();
    struct sched_param sparams;
    struct stat st;

    sparams.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(this_thread,SCHED_FIFO,&sparams);

    visualizer_set_image_cb(lua_load_image_cb);

    thread_queue_init(&(vis->image_queue),100,(void **)&(vis->images),0);

    if(!avi_stream_init(
        &(vis->stream),
        vis->video_width,
        vis->video_height,
        vis->framerate,
        vis->samplerate,
        vis->channels,
        vis->samplesize)) {
        fprintf(stderr,"Error initializing AVI stream\n");
        return visualizer_free(vis);
    }

    vis->processor.framerate    = vis->framerate;
    vis->processor.channels     = vis->channels;
    vis->processor.samplerate   = vis->samplerate;
    vis->processor.samplesize   = vis->samplesize;
    vis->processor.spectrum_len = vis->bars;

    if(!audio_processor_init(&(vis->processor))) {
        fprintf(stderr,"Error initializing audio processor\n");
        return visualizer_free(vis);
    }

    vis->buffer_len = vis->samplerate * vis->channels * vis->samplesize * 4;

    vis->buffer = (char *)malloc(vis->buffer_len);
    if(!vis->buffer) {
        fprintf(stderr,"Error mallocing buffer\n");
        visualizer_free(vis);
        return 0;
    }

    vis->fds[0].fd = selfpipe_init();
    vis->fds[0].events = IOPAUSE_READ | IOPAUSE_EXCEPT;

    if(vis->fds[0].fd < 0) {
        strerr_die1sys(1,"Unable to create selfpipe");
    }
    if(selfpipe_trap(SIGINT) < 0) {
        strerr_die1sys(1,"Unable to trap SIGINT: ");
    }
    if(selfpipe_trap(SIGTERM) < 0) {
        strerr_die1sys(1,"Unable to trap SIGTERM: ");
    }
    if(selfpipe_trap(SIGPIPE) < 0) {
        strerr_die1sys(1,"Unable to trap SIGPIPE: ");
    }
    if(selfpipe_trap(SIGUSR1) < 0) {
        strerr_die1sys(1,"Unable to trap SIGUSR1: ");
    }
    if(selfpipe_trap(SIGUSR2) < 0) {
        strerr_die1sys(1,"Unable to trap SIGUSR2: ");
    }



    if(vis->mpd) {
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
        vis->fds[3].fd = mpd_connection_get_fd(vis->mpd_conn);
        vis->fds[3].events = IOPAUSE_READ | IOPAUSE_EXCEPT;
    }

    vis->Lua = 0;
    vis->Lua = luaL_newstate();
    if(!vis->Lua) {
        fprintf(stderr,"Error loading Lua\n");
        visualizer_free(vis);
        return 0;
    }

    luaL_openlibs(vis->Lua);
    luaopen_image(vis->Lua);
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
    if(!vis->mpd) {
        if(vis->title) {
            lua_pushlstring(vis->Lua,vis->title,strlen(vis->title));
            lua_setfield(vis->Lua,-2,"title");
        }
        if(vis->album) {
            lua_pushlstring(vis->Lua,vis->album,strlen(vis->album));
            lua_setfield(vis->Lua,-2,"album");
        }
        if(vis->artist) {
            lua_pushlstring(vis->Lua,vis->artist,strlen(vis->artist));
            lua_setfield(vis->Lua,-2,"artist");
        }
        if(vis->filename) {
            lua_pushlstring(vis->Lua,vis->filename,strlen(vis->filename));
            lua_setfield(vis->Lua,-2,"file");
        }
        if(vis->totaltime > -1) {
            lua_pushinteger(vis->Lua,vis->totaltime);
            lua_setfield(vis->Lua,-2,"total");
        }

    }
    lua_setglobal(vis->Lua,"song");
    lua_settop(vis->Lua,0);

    lua_getglobal(vis->Lua,"image");
    lua_getfield(vis->Lua,-1,"new");
    lua_pushnil(vis->Lua);
    lua_pushinteger(vis->Lua,vis->video_width);
    lua_pushinteger(vis->Lua,vis->video_height);
    lua_pushinteger(vis->Lua,3);
    if(lua_pcall(vis->Lua,4,1,0)) {
        strerr_die2x(1,"Error creating stream image: ",lua_tostring(vis->Lua,-1));
    }

    lua_getfield(vis->Lua,-1,"frames");
    lua_rawgeti(vis->Lua,-1,1);

    lua_pushinteger(vis->Lua,vis->framerate);
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

    luaimage_setup_threads(&(vis->image_queue));

    if(strcmp(vis->output_fifo,"-") != 0) {
        vis->own_fifo = mkfifo(vis->output_fifo,
          S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

        if(vis->own_fifo < 0) {
            if(stat(vis->output_fifo,&st) == 0) {
                if(!S_ISFIFO(st.st_mode)) {
                    strerr_die3x(1,"Output path ",vis->output_fifo," exists and is not a fifo");
                }
                else {
                    vis->own_fifo = 0;
                }
            }
        }
        else {
            vis->own_fifo = 1;
        }
        fd_close(fileno(stdout));
    }
    else {
        vis->own_fifo = 0;
        if(vis->argc) {
            if(child_spawn1_pipe(vis->argv[0], vis->argv, (char const *const *)environ, &(vis->fds[2].fd), 0) <= 0) {
                strerr_die1x(1,"Problem spawning child process");
            }
            fprintf(stderr,"fd: %d\n",vis->fds[2].fd);
        }
        else {
            vis->fds[2].fd = fileno(stdout);
        }
        ndelay_off(vis->fds[2].fd);
        avi_stream_write_header(&(vis->stream),vis->fds[2].fd);
    }

    if(vis->own_fifo < 0) {
        strerr_die3x(1,"Problem opening ", vis->output_fifo," for file writing");
    }

    if(strcmp(vis->input_fifo,"-") != 0) {
        vis->fds[1].fd = open_read(vis->input_fifo);
        if(vis->fds[1].fd == -1) {
            strerr_die3sys(1,"Problem opening ",vis->input_fifo,": ");
        }
        ndelay_on(vis->fds[1].fd);
        fd_close(fileno(stdin));
    }
    else {
        vis->fds[1].fd = fileno(stdin);
        ndelay_on(vis->fds[1].fd);
    }
    vis->fds[1].events = IOPAUSE_READ;

    vis->ms_per_frame = (vis->processor.sample_window_len) / (vis->samplerate / 1000);

    if(vis->mpd_conn) {
        visualizer_get_metadata(vis);

        if(!mpd_send_idle_mask(vis->mpd_conn, MPD_IDLE_PLAYER | MPD_IDLE_MESSAGE)) {
            strerr_die2x(1,"Error idling: ",mpd_connection_get_error_message(vis->mpd_conn));
        }
    }

    return 1;

}

int
visualizer_loop(visualizer *vis) {
    int events = 0;
    int signal = 0;

    if(vis->fds[2].fd == -1) {
        vis->fds[2].fd = open_write(vis->output_fifo);
        if(vis->fds[2].fd > -1) {
            fprintf(stderr,"Opened fifo\n");
            vis->fds[2].revents = 0;
            vis->fds[2].events = IOPAUSE_EXCEPT;
            ndelay_off(vis->fds[2].fd);
            avi_stream_write_header(&(vis->stream),vis->fds[2].fd);
        }
    }

    events = iopause_stamp(vis->fds,4,0,0);

    if(events && vis->fds[0].revents & IOPAUSE_READ) {
        signal = selfpipe_read();
        switch(signal) {
            case SIGINT: return -1;
            case SIGTERM: return -1;
            case SIGUSR1: {
                strerr_warn1x("Reloading images/scripts");
                visualizer_load_scripts(vis);
                break;

            }
            case SIGUSR2: {
                vis->reload = 1;
                break;
            }
        }
    }

    if(events && vis->fds[2].revents & IOPAUSE_WRITE) {
        int rem = ringbuf_bytes_used(vis->stream.frames);
        fprintf(stderr,"frame_bytes: %d\n");
        if( rem > 0 ) {
            int b = ringbuf_write(vis->fds[2].fd,vis->stream.frames,rem);
            fprintf(stderr,"b: %d\n",b);
            if( b == -1) {
                goto closefifo;
            }
            rem -= b;
        }

        if( rem == 0 ) {
            vis->fds[2].events = IOPAUSE_EXCEPT;
        }

    }

    if(events && vis->fds[1].revents & IOPAUSE_READ) {
        if(visualizer_grab_audio(vis,vis->fds[1].fd) <= 0) return -1;
        visualizer_make_frames(vis);
        if(ringbuf_bytes_used(vis->stream.frames)) {
            if(vis->fds[2].fd == -1) {
                ringbuf_reset(vis->stream.frames);
            }
            else {
                vis->fds[2].events = IOPAUSE_WRITE | IOPAUSE_EXCEPT;
            }
        }
    }


    if(events && vis->fds[2].revents & IOPAUSE_EXCEPT) {
        closefifo:
        fprintf(stderr,"Closing fifo\n");
        fd_close(vis->fds[2].fd);
        vis->fds[2].revents = 0;
        vis->fds[2].events = 0;
        vis->stream.output_frame_rem = 0;
        if(vis->fds[2].fd == fileno(stdout)) {
            return -1;
        }
        vis->fds[2].fd = -1;
    }

    if(events && vis->fds[3].revents & IOPAUSE_READ) {
        mpd_recv_idle(vis->mpd_conn,0);
        mpd_response_finish(vis->mpd_conn);
        visualizer_get_metadata(vis);
        mpd_send_idle_mask(vis->mpd_conn, MPD_IDLE_PLAYER | MPD_IDLE_MESSAGE);
    }

    return 0;
}

int visualizer_cleanup(visualizer *vis) {

    visualizer_make_frames(vis);

    int rem = ringbuf_bytes_used(vis->stream.frames);
    int n = 0;

    if(vis->fds[2].fd != -1) {
        while(rem > 0) {
            n = ringbuf_write(vis->fds[2].fd,vis->stream.frames,rem);
            if(n<0) {
                rem = 0;
                n = 0;
            }
            rem -= n;
        }
        fd_close(vis->fds[2].fd);
    }

    fd_close(vis->fds[0].fd);
    fd_close(vis->fds[1].fd);
    if(vis->fds[3].fd != -1) {
        fd_close(vis->fds[3].fd);
    }
    visualizer_free(vis);
    if(vis->own_fifo) unlink(vis->output_fifo);

    return 0;
}

#ifdef __cplusplus
}
#endif
