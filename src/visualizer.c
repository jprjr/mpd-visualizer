#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <skalibs/skalibs.h>
#include <skalibs/buffer.h>
#include <skalibs/bytestr.h>
#include <skalibs/ip46.h>
#include <skalibs/webipc.h>
#include <skalibs/types.h>
#include <s6-dns/s6dns.h>
#include <stdlib.h>
#include <errno.h>
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
#include "ringbuf.h"

#define func_list_len(g) genalloc_len(lua_func_list,g)
#define func_list_s(g) genalloc_s(lua_func_list,g)
#define func_list_free(g) genalloc_free(lua_func_list,g)
#define func_list_append(g, p) genalloc_append(lua_func_list,g, p)

#define dienomem() strerr_die1x(1,"error: out of memory")
#define VIS_MIN(a,b) ( a < b ? a : b )

static stralloc mpd_songid   = STRALLOC_ZERO;
static stralloc mpd_elapsed  = STRALLOC_ZERO;
static stralloc mpd_duration = STRALLOC_ZERO;
static stralloc mpd_time     = STRALLOC_ZERO;
static stralloc mpd_file     = STRALLOC_ZERO;
static stralloc mpd_title    = STRALLOC_ZERO;
static stralloc mpd_album    = STRALLOC_ZERO;
static stralloc mpd_artist   = STRALLOC_ZERO;

typedef struct lua_func_list {
    int frame_ref;
    int reload_ref;
    int change_ref;
    time_t mtime;
    stralloc filename;
} lua_func_list;

#define LUA_FUNC_LIST_ZERO { \
  .frame_ref = -1, \
  .reload_ref = -1, \
  .change_ref = -1, \
  .mtime = -1, \
  .filename = STRALLOC_ZERO, \
}

#ifdef __cplusplus
extern "C" {
#endif

static size_t fd_read_wrapper(uint8_t *dest, size_t count, void *ctx)
{
    int *fd = ctx;
    return fd_read(*fd,(char *)dest,count);
}

static size_t fd_write_wrapper(uint8_t *src, size_t count, void *ctx)
{
    int *fd = ctx;
    return fd_write(*fd, (char *)src, count);
}

static inline int
resolve_dns(genalloc *ips, char const *name) {
    stralloc ip4 = STRALLOC_ZERO;
    stralloc ip6 = STRALLOC_ZERO;
    ip46_t ip = IP46_ZERO;
    tain_t dl = TAIN_ZERO;
    tain_t now = TAIN_ZERO;
    int ip4_res = 0;
    int ip6_res = 0;
    int r = 0;
    unsigned int i = 0;

    tain_now(&now);
    tain_addsec(&dl,&now,5);
    ip4_res = s6dns_resolve_a_g(&ip4,name,strlen(name),0,&dl);
    tain_now(&now);
    tain_addsec(&dl,&now,5);
    ip6_res = s6dns_resolve_aaaa_g(&ip6,name,strlen(name),0,&dl);

    if(ip4_res > 0) {
        r = 1;
        for(i=0;i<ip4.len;i+=4) {
            ip46_from_ip4(&ip,ip4.s+i);
            genalloc_append(ip46_t,ips,&ip);
        }
    }
    if(ip6_res > 0) {
        r += 2;
        for(i=0;i<ip6.len;i+=16) {
            ip46_from_ip6(&ip,ip6.s+i);
            genalloc_append(ip46_t,ips,&ip);
        }
    }

    stralloc_free(&ip4);
    stralloc_free(&ip6);

    return r;

}

static inline lua_func_list *
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
visualizer_set_image_cb(visualizer *vis, void (*lua_image_cb)(lua_State *L, intptr_t table_ref, unsigned int frames, uint8_t *image)) {
    vis->lua_image_cb = lua_image_cb;
}

static inline void
lua_func_list_free(visualizer *vis, genalloc *list) {
    unsigned long i;
    for(i=0;i<func_list_len(list);i++) {
        if(func_list_s(list)[i].frame_ref != -1) {
            luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].frame_ref );
        }
        if(func_list_s(list)[i].reload_ref != -1) {
            luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].reload_ref );
        }
        if(func_list_s(list)[i].change_ref != -1) {
            luaL_unref(vis->Lua, LUA_REGISTRYINDEX, func_list_s(list)[i].change_ref );
        }
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
            strerr_warn2x("warning: ",lua_tostring(vis->Lua,-1));
            lua_settop(vis->Lua,0);
            continue;
        }
        if(lua_pcall(vis->Lua,0,1,0)) {
            strerr_warn2x("warning: ",lua_tostring(vis->Lua,-1));
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

            lua_getfield(vis->Lua,-1,"onchange");
            if(lua_isfunction(vis->Lua,-1)) {
                if(cur->change_ref != -1) {
                    luaL_unref(vis->Lua,LUA_REGISTRYINDEX,cur->change_ref);
                }
                cur->change_ref = luaL_ref(vis->Lua,LUA_REGISTRYINDEX);
            }
            else {
                lua_pop(vis->Lua,1);
            }

            lua_getfield(vis->Lua,-1,"onload");
            if(lua_isfunction(vis->Lua,-1) && cur->mtime == -1) {
                if(lua_pcall(vis->Lua,0,0,0)) {
                    strerr_warn2x("warning: ",lua_tostring(vis->Lua,-1));
                }
            }
            else {
                lua_pop(vis->Lua,1);
            }

            if(cur->reload_ref != -1 && cur->mtime != -1) {
                lua_rawgeti(vis->Lua,LUA_REGISTRYINDEX,cur->reload_ref);
                if(lua_pcall(vis->Lua,0,0,0)) {
                    strerr_warn2x("warning: ",lua_tostring(vis->Lua,-1));
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
    stralloc_free(&mpd_songid);
    stralloc_free(&mpd_elapsed);
    stralloc_free(&mpd_duration);
    stralloc_free(&mpd_time);
    stralloc_free(&mpd_file);
    stralloc_free(&mpd_title);
    stralloc_free(&mpd_album);
    stralloc_free(&mpd_artist);
    genalloc_free(ip46_t,&(vis->iplist));
    lua_func_list_free(vis, &(vis->lua_funcs));
    thread_queue_term(&(vis->image_queue));
    avi_stream_free(&(vis->stream));
    audio_processor_free(&(vis->processor));
    s6dns_finish();
    if(vis->mpd_conn != NULL) {
        free(vis->mpd_conn);
        vis->mpd_conn = NULL;
    }

    if(vis->Lua) {
        luaclose_image();
        lua_close(vis->Lua);
    }
    return 0;
}

static inline int
visualizer_grab_audio(visualizer *vis) {
    int bytes_read = 0;

    bytes_read = ringbuf_fill(vis->processor.samples);
    if(bytes_read <= 0) {
        strerr_warn1sys("warning: problem grabbing audio data: ");
        return -1;
    }
    vis->processor.samples_available = ringbuf_bytes_used(vis->processor.samples) / (vis->processor.channels * vis->processor.samplesize);

    return vis->processor.samples_available;
}

#if 0
    /* quick sanity check */
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_PING);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_PING);
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_SUBSCRIBE);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_SUBSCRIBE);
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_STATUS);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_STATUS);
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_MESSAGE);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_MESSAGE);
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_CURRENTSONG);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_CURRENTSONG);
    ringbuf_append(vis->mpd_q,VIS_MPD_SEND_IDLE);
    ringbuf_append(vis->mpd_q,VIS_MPD_READ_IDLE);
#endif


static int vis_mpdc_write(void *ctx, const uint8_t *buf, unsigned int len) {
    visualizer *vis = (visualizer *)ctx;

    return fd_send(vis->fds[3].fd,(const char *)buf,len,0);
}

static int vis_mpdc_read(void *ctx, uint8_t *buf, unsigned int len) {
    visualizer *vis = (visualizer *)ctx;

    return fd_read(vis->fds[3].fd,(char *)buf,len);
}

static int vis_mpdc_write_notify(mpdc_connection *conn) {
    visualizer *vis = (visualizer *)conn->ctx;
    vis->fds[3].events = IOPAUSE_WRITE;
    return 1;
}

static int vis_mpdc_read_notify(mpdc_connection *conn) {
    visualizer *vis = (visualizer *)conn->ctx;
    vis->fds[3].events = IOPAUSE_READ;
    return 1;
}

static int vis_mpdc_resolve(mpdc_connection *conn, char *hostname) {
    ip46_t ip = IP46_ZERO;
    tain_t dl = TAIN_ZERO;
    tain_t now = TAIN_ZERO;
    visualizer *vis = (visualizer *)conn->ctx;
    if(hostname[0] == '/') return 1;

    if(ip46_scan(hostname,&ip) > 0) {
        if(!genalloc_copyb(ip46_t,&vis->iplist,&ip,1)) {
            dienomem();
        }
    } else {
        tain_now(&now);
        tain_addsec(&dl,&now,5);
        if(resolve_dns(&(vis->iplist),hostname) <= 0) {
            strerr_die2x(1,"error: unable to resolve hostname ",hostname);
        }
    }
    return 1;
}

static void vis_mpdc_disconnect(mpdc_connection *conn) {
    visualizer *vis = (visualizer *)conn->ctx;
    if(vis->fds[3].fd > -1) {
        fd_close(vis->fds[3].fd);
        vis->fds[3].fd = -1;
    }
}

static int vis_mpdc_connect(mpdc_connection *conn, char *hostname, uint16_t port) {
    visualizer *vis = (visualizer *)conn->ctx;

    ip46_t *ip;
    int r = 0;
    unsigned int i = 0;
    int connected = 0;

    if(vis->fds[3].fd > -1) {
        fd_close(vis->fds[3].fd);
        vis->fds[3].fd = -1;
    }

    if(hostname[0] == '/') {
        vis->fds[3].fd = ipc_stream_nb();
        if(vis->fds[3].fd == -1) {
            strerr_warn1sys("error: unable to open socket: ");
            return -1;
        }

        r = ipc_connect(vis->fds[3].fd,hostname);
        if(r == -1 && errno != EINPROGRESS) {
            strerr_warn1sys("error: unable to connect to socket: ");
            return -1;
        }
    } else {
        while(i++ < genalloc_len(ip46_t,&(vis->iplist))) {
            ip = genalloc_s(ip46_t,&(vis->iplist)) + (i-1);

            if(ip46_is6(ip)) {
                if(byte_equal(ip->ip,16,"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")) {
                    continue;
                }
            } else {
                if(byte_equal(ip->ip,4,"\0\0\0\0")) {
                    continue;
                }
            }
            vis->fds[3].fd = ip46_is6(ip) ? socket_tcp6_nb() : socket_tcp4_nb();
            if(vis->fds[3].fd == -1) {
                strerr_warn1sys("warning: unable to open socket: ");
                continue;
            }

            r = socket_connect46(vis->fds[3].fd,ip,port);
            if(r == -1 && errno != EINPROGRESS) {
                strerr_warn1sys("warning: unable to connect to ip: ");
                continue;
            }
            connected = 1;
            break;
        }
        if(connected == 0) {
            strerr_warn1x("error: unable to connect to mpd");
            return 0;
        }
    }

    vis->fds[3].events = 0;
    vis->fds[3].revents = 0;

    return 1;
}

static void vis_mpdc_response(mpdc_connection *conn, const char *cmd, const char *key, const uint8_t *value, unsigned int length) {
    visualizer *vis = (visualizer *)conn->ctx;
    unsigned int lua_i = 0;

    if(strcmp(cmd,"status") == 0) {
        if(strcmp(key,"songid") == 0) {
            if(!stralloc_catb(&mpd_songid,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"elapsed") == 0) {
            if(!stralloc_catb(&mpd_elapsed,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"duration") == 0) {
            if(!stralloc_catb(&mpd_duration,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"time") == 0) {
            if(!stralloc_catb(&mpd_time,(const char *)value,length)) dienomem();
        }
    } else if(strcmp(cmd,"currentsong") ==0) {
        if(strcmp(key,"file") == 0) {
            if(!stralloc_catb(&mpd_file,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"title") == 0) {
            if(!stralloc_catb(&mpd_title,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"album") == 0) {
            if(!stralloc_catb(&mpd_album,(const char *)value,length)) dienomem();
        } else if(strcmp(key,"artist") == 0) {
            if(!stralloc_catb(&mpd_artist,(const char *)value,length)) dienomem();
        }
    }
    else if(strcmp(cmd,"readmessages") == 0) {
        if(strcmp(key,"message") == 0) {
            lua_getglobal(vis->Lua,"song");
            lua_pushlstring(vis->Lua,(const char *)value,length);
            lua_setfield(vis->Lua,-2,"message");

            for(lua_i=0;lua_i<func_list_len(&(vis->lua_funcs));lua_i++) {
                if(func_list_s(&(vis->lua_funcs))[lua_i].change_ref != -1) {
                    lua_rawgeti(vis->Lua,LUA_REGISTRYINDEX,func_list_s(&(vis->lua_funcs))[lua_i].change_ref);
                    if(lua_isfunction(vis->Lua,-1)) {
                        lua_pushstring(vis->Lua,"message");
                        if(lua_pcall(vis->Lua,1,0,0)) {
                            strerr_warn2x("error: ",lua_tostring(vis->Lua,-1));
                        }
                    }
                    else {
                        lua_pop(vis->Lua,1);
                    }
                }
            }
        }
    }
    else if(strcmp(cmd,"idle") == 0) {
        if(strcmp(key,"changed") == 0) {
            if(strcmp((const char *)value,"player") == 0) {
                mpdc_currentsong(conn);
            }
            else if(strcmp((const char *)value,"message") == 0) {
                mpdc_readmessages(conn);
            }
        }
    }
}

static void vis_mpdc_response_end(mpdc_connection *conn, const char *cmd, int ok) {
    unsigned int lua_i = 0;
    unsigned int t = 0;
    unsigned int j = 0;
    visualizer *vis = (visualizer *)conn->ctx;

    if(ok == -1) strerr_die1x(1,"error with mpd");
    if(strcmp(cmd,"idle") == 0) mpdc_idle(conn,MPDC_EVENT_PLAYER | MPDC_EVENT_MESSAGE);


    if(strcmp(cmd,"status") == 0 || strcmp(cmd,"currentsong") == 0) {
      lua_getglobal(vis->Lua,"song");

      if(strcmp(cmd,"status") == 0) {
        if(mpd_songid.len) {
            stralloc_0(&mpd_songid);
            uint32_scan(mpd_songid.s,&t);
            lua_pushinteger(vis->Lua,t);
        }
        else {
            lua_pushnil(vis->Lua);
        }

        lua_setfield(vis->Lua,-2,"id");

        mpd_songid.len = 0;

        if(mpd_elapsed.len) {
            stralloc_0(&mpd_elapsed);
            uint32_scan(mpd_elapsed.s,&t);
            vis->elapsed_ms = t * 1000;
            j = str_chr(mpd_elapsed.s,'.');
            if(mpd_elapsed.s[j]) {
                uint32_scan(mpd_elapsed.s + j + 1, &t);
                vis->elapsed_ms += t;
            }
        }
        else if (mpd_time.len) {
            stralloc_0(&mpd_time);
            uint32_scan(mpd_time.s,&t);
            vis->elapsed_ms = t * 1000;
        }
        /* Just let it keep counting up */

        mpd_elapsed.len = 0;

        if(mpd_duration.len) {
            stralloc_0(&mpd_duration);
            uint32_scan(mpd_duration.s,&t);
            lua_pushinteger(vis->Lua,t);
            lua_setfield(vis->Lua,-2,"total");
        }
        else if (mpd_time.len) {
            stralloc_0(&mpd_time);
            j = str_chr(mpd_time.s,':');
            if (mpd_time.s[j]) {
                uint32_scan(mpd_time.s + j + 1, &t);
                lua_pushinteger(vis->Lua,t);
            }
            lua_setfield(vis->Lua,-2,"total");
        }
        mpd_duration.len = 0;
        mpd_time.len = 0;
      }
      else if(strcmp(cmd,"currentsong") == 0) {
        mpd_file.len ? lua_pushlstring(vis->Lua,mpd_file.s,mpd_file.len) : lua_pushnil(vis->Lua);
        lua_setfield(vis->Lua,-2,"file");

        mpd_title.len ? lua_pushlstring(vis->Lua,mpd_title.s,mpd_title.len) : lua_pushnil(vis->Lua);
        lua_setfield(vis->Lua,-2,"title");

        mpd_artist.len ? lua_pushlstring(vis->Lua,mpd_artist.s,mpd_artist.len) : lua_pushnil(vis->Lua);
        lua_setfield(vis->Lua,-2,"artist");

        mpd_album.len ? lua_pushlstring(vis->Lua,mpd_album.s,mpd_album.len) : lua_pushnil(vis->Lua);
        lua_setfield(vis->Lua,-2,"album");

        mpd_file.len = 0;
        mpd_title.len = 0;
        mpd_artist.len = 0;
        mpd_album.len = 0;
      }

      lua_settop(vis->Lua,0);

      for(lua_i=0;lua_i<func_list_len(&(vis->lua_funcs));lua_i++) {
          if(func_list_s(&(vis->lua_funcs))[lua_i].change_ref != -1) {
              lua_rawgeti(vis->Lua,LUA_REGISTRYINDEX,func_list_s(&(vis->lua_funcs))[lua_i].change_ref);
              if(lua_isfunction(vis->Lua,-1)) {
                  lua_pushstring(vis->Lua,"player");
                  if(lua_pcall(vis->Lua,1,0,0)) {
                      strerr_warn2x("error: ",lua_tostring(vis->Lua,-1));
                  }
              }
              else {
                  lua_pop(vis->Lua,1);
              }
          }
      }

    }
}


static inline int
visualizer_make_frames(visualizer *vis) {
    int frames = 0;
    unsigned long i = 0;
    image_q *q = NULL;

    while(vis->processor.samples_available >= vis->processor.sample_window_len && ringbuf_bytes_free(vis->stream.frames) >= vis->stream.frame_len) {
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
                        strerr_warn2x("error: ",lua_tostring(vis->Lua,-1));
                    }
                }
            }

            lua_settop(vis->Lua,0);
        }

        lua_gc(vis->Lua,LUA_GCCOLLECT,0);

        wake_queue();

        ringbuf_memcpy_into(vis->stream.frames,vis->stream.input_frame,vis->stream.frame_len);

        vis->processor.samples_available = ringbuf_bytes_used(vis->processor.samples) / (vis->processor.channels * vis->processor.samplesize);
        frames++;
    }

    return frames;
}

int
visualizer_reload(visualizer *vis) {
    vis->fds[0].fd = selfpipe_init();
    if(selfpipe_trap(SIGINT) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGINT: ");
    }
    if(selfpipe_trap(SIGTERM) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGTERM: ");
    }
    if(selfpipe_trap(SIGPIPE) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGPIPE: ");
    }
    if(selfpipe_trap(SIGUSR1) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGUSR1: ");
    }
    if(selfpipe_trap(SIGUSR2) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGUSR2: ");
    }
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

    if(!s6dns_init()) {
        strerr_die1sys(1,"error: unable to init DNS: ");
    }
    if(!tain_init()) {
        strerr_die1sys(1,"error: unable to init timer: ");
    }

    vis->mpd_conn = (mpdc_connection *)malloc(sizeof(mpdc_connection));
    if(vis->mpd_conn == NULL) dienomem();

    vis->mpd_conn->ctx            = vis;
    vis->mpd_conn->write          = vis_mpdc_write;
    vis->mpd_conn->read           = vis_mpdc_read;
    vis->mpd_conn->write_notify   = vis_mpdc_write_notify;
    vis->mpd_conn->read_notify    = vis_mpdc_read_notify;
    vis->mpd_conn->resolve        = vis_mpdc_resolve;
    vis->mpd_conn->connect        = vis_mpdc_connect;
    vis->mpd_conn->disconnect     = vis_mpdc_disconnect;
    vis->mpd_conn->response       = vis_mpdc_response;
    vis->mpd_conn->response_begin = NULL;
    vis->mpd_conn->response_end   = vis_mpdc_response_end;

    pthread_t this_thread = pthread_self();
    struct sched_param sparams;
    struct stat st;

    stralloc realpath_lua = STRALLOC_ZERO;

    sparams.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(this_thread,SCHED_FIFO,&sparams);

    visualizer_set_image_cb(vis,lua_load_image_cb);

    thread_queue_init(&(vis->image_queue),100,(void **)&(vis->images),0);

    if(!avi_stream_init(
        &(vis->stream),
        vis->video_width,
        vis->video_height,
        vis->framerate,
        vis->samplerate,
        vis->channels,
        vis->samplesize)) {
        strerr_warn1x("error: unable to initialize AVI stream");
        return visualizer_free(vis);
    }

    vis->processor.framerate    = vis->framerate;
    vis->processor.channels     = vis->channels;
    vis->processor.samplerate   = vis->samplerate;
    vis->processor.samplesize   = vis->samplesize;
    vis->processor.spectrum_len = vis->bars;

    if(!audio_processor_init(&(vis->processor))) {
        strerr_warn1x("error: unable to initialize audio processor");
        return visualizer_free(vis);
    }

    vis->fds[0].fd = selfpipe_init();
    vis->fds[0].events = IOPAUSE_READ | IOPAUSE_EXCEPT;

    if(vis->fds[0].fd < 0) {
        strerr_die1sys(1,"error: unable to create selfpipe");
    }
    if(selfpipe_trap(SIGINT) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGINT: ");
    }
    if(selfpipe_trap(SIGTERM) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGTERM: ");
    }
    if(selfpipe_trap(SIGPIPE) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGPIPE: ");
    }
    if(selfpipe_trap(SIGUSR1) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGUSR1: ");
    }
    if(selfpipe_trap(SIGUSR2) < 0) {
        strerr_die1sys(1,"error: unable to trap SIGUSR2: ");
    }


    vis->Lua = NULL;
    vis->Lua = luaL_newstate();
    if(!vis->Lua) {
        strerr_warn1x("error: unable to load Lua");
        visualizer_free(vis);
        return 0;
    }

    luaL_openlibs(vis->Lua);
    luaopen_image(vis->Lua,vis);
    luaopen_file(vis->Lua);

    if(luaL_loadbuffer(vis->Lua,font_lua,font_lua_length-1,"font.lua")) {
        strerr_die2x(1,"error: ",lua_tostring(vis->Lua,-1));
    }

    if(lua_pcall(vis->Lua,0,1,0)) {
        strerr_die2x(1,"error: ",lua_tostring(vis->Lua,-1));
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
        strerr_die2x(1,"error: ",lua_tostring(vis->Lua,-1));
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
        strerr_die2x(1,"error: ",lua_tostring(vis->Lua,-1));
    }

    if(lua_pcall(vis->Lua,0,0,0)) {
        strerr_die2x(1,"error: ",lua_tostring(vis->Lua,-1));
    }

    if(vis->lua_folder) {
        if(!stralloc_cats(&realpath_lua,"package.path = '")) {
            strerr_warn1x("error: out of memory");
            visualizer_free(vis);
            return -1;
        }
        if(sarealpath(&realpath_lua,vis->lua_folder) != -1) {
            if(!stralloc_cats(&realpath_lua,"/?.lua;' .. package.path")) {
                strerr_warn1x("error: out of memory");
                visualizer_free(vis);
                return -1;
            }
            if(!stralloc_0(&realpath_lua)) {
                strerr_warn1x("error: out of memory");
                visualizer_free(vis);
                return -1;
            }
            if(luaL_dostring(vis->Lua,realpath_lua.s) != 0) {
                strerr_warn2x("error: ",lua_tostring(vis->Lua,-1));
                visualizer_free(vis);
                return -1;
            }
        }
        visualizer_load_scripts(vis);
    }
    stralloc_free(&realpath_lua);

    luaimage_setup_threads(&(vis->image_queue));

    if(strcmp(vis->output_fifo,"-") != 0) {
        vis->own_fifo = mkfifo(vis->output_fifo,
          S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

        if(vis->own_fifo < 0) {
            if(stat(vis->output_fifo,&st) == 0) {
                if(!S_ISFIFO(st.st_mode)) {
                    strerr_die3x(1,"error: output path ",vis->output_fifo," exists and is not a fifo");
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
                strerr_die1x(1,"error: unable to spawn child process");
            }
        }
        else {
            vis->fds[2].fd = fileno(stdout);
        }
        ndelay_off(vis->fds[2].fd);
        avi_stream_write_header(&(vis->stream),&(vis->fds[2].fd),fd_write_wrapper);
        ndelay_on(vis->fds[2].fd);
    }

    if(vis->own_fifo < 0) {
        strerr_die3x(1,"error: unable to open ", vis->output_fifo," for writing");
    }

    vis->stream.frames->write_context = &(vis->fds[2].fd);
    vis->stream.frames->write = fd_write_wrapper;

    if(strcmp(vis->input_fifo,"-") != 0) {
        vis->fds[1].fd = open_read(vis->input_fifo);
        if(vis->fds[1].fd == -1) {
            strerr_die3sys(1,"error: unable to open ",vis->input_fifo,": ");
        }
        ndelay_on(vis->fds[1].fd);
        fd_close(fileno(stdin));
    }
    else {
        vis->fds[1].fd = fileno(stdin);
        ndelay_on(vis->fds[1].fd);
    }
    vis->fds[1].events = IOPAUSE_READ;

    vis->processor.samples->read_context = &(vis->fds[1].fd);
    vis->processor.samples->read = fd_read_wrapper;


    vis->ms_per_frame = (vis->processor.sample_window_len) / (vis->samplerate / 1000);
    vis->delay = (1000 / (vis->framerate / 2)) * 1000000;

    if(vis->mpd) {
      if(!mpdc_init(vis->mpd_conn)) {
          visualizer_free(vis);
          return -1;
      }
      if(!mpdc_setup(vis->mpd_conn,getenv("MPD_HOST"),getenv("MPD_PORT"),0)) {
          visualizer_free(vis);
          return -1;
      }

      if(!mpdc_connect(vis->mpd_conn)) {
          visualizer_free(vis);
          return -1;
      }
      mpdc_subscribe(vis->mpd_conn,"visualizer");
      mpdc_status(vis->mpd_conn);
      mpdc_currentsong(vis->mpd_conn);
      mpdc_idle(vis->mpd_conn, MPDC_EVENT_PLAYER | MPDC_EVENT_MESSAGE);
    }


    return 1;

}

int
visualizer_loop(visualizer *vis) {
    while(1) {
    int events = 0;
    int signal = 0;

    if(vis->fds[2].fd == -1) {
        vis->fds[2].fd = open_write(vis->output_fifo);
        if(vis->fds[2].fd > -1) {
            vis->fds[2].revents = 0;
            vis->fds[2].events = IOPAUSE_EXCEPT;
            ndelay_off(vis->fds[2].fd);
            avi_stream_write_header(&(vis->stream),&(vis->fds[2].fd),fd_write_wrapper);
            ndelay_on(vis->fds[2].fd);
        }
    }

    visualizer_make_frames(vis);

    /* figure out what events we need */
    if(ringbuf_bytes_free(vis->processor.samples)) {
        vis->fds[1].events = IOPAUSE_EXCEPT | IOPAUSE_READ;
    }
    else {
        vis->fds[1].events = IOPAUSE_EXCEPT;
    }

    if(ringbuf_bytes_used(vis->stream.frames)) {
        if(vis->fds[2].fd == -1) {
            ringbuf_reset(vis->stream.frames);
        }
        else {
            vis->fds[2].events = IOPAUSE_EXCEPT | IOPAUSE_WRITE;
        }
    }


    events = iopause_stamp(vis->fds,4,NULL,NULL);

    if(events && vis->fds[0].revents & IOPAUSE_READ) {
        signal = selfpipe_read();
        switch(signal) {
            case SIGINT:
            /* fall through */
            case SIGTERM:
                strerr_warn1x("warning: caught INT/TERM, exiting");
                return -1;
                break;
            case SIGUSR1: {
                strerr_warn1x("info: eeloading images/scripts");
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
        unsigned int rem = ringbuf_bytes_used(vis->stream.frames);
        int b = ringbuf_write(vis->stream.frames, rem);
        /* int b = vis_ringbuf_write(vis->fds[2].fd,vis->stream.frames,rem); */

        if( b == -1) {
            goto closefifo;
        }
        rem -= b;

        if( rem == 0 ) {
            vis->fds[2].events = IOPAUSE_EXCEPT;
        }
    }

    if(events && vis->fds[1].revents & IOPAUSE_READ) {
        if(visualizer_grab_audio(vis) < 0) {
            strerr_warn1x("warning: no audio data received");
            return -1;
        }
    }

    if(events && vis->fds[1].revents & IOPAUSE_EXCEPT) {
        strerr_warn1sys("warning on input: ");
        return -1;
    }


    if(events && vis->fds[2].revents & IOPAUSE_EXCEPT) {
        closefifo:
        fd_close(vis->fds[2].fd);
        vis->fds[2].revents = 0;
        vis->fds[2].events = 0;
        vis->stream.output_frame_rem = 0;
        if(strcmp(vis->output_fifo,"-") == 0) {
            strerr_warn1x("warning: output pipe closed, exiting");
            return -1;
        }
        vis->fds[2].fd = -1;
    }

    if(events && vis->fds[3].revents & IOPAUSE_READ) {
        if(mpdc_receive(vis->mpd_conn) == -1) {
          mpdc_disconnect(vis->mpd_conn);
          if(!mpdc_connect(vis->mpd_conn)) strerr_die1x(1,"error: could not reconnect to mpd");
          mpdc_subscribe(vis->mpd_conn,"visualizer");
          mpdc_currentsong(vis->mpd_conn);
          mpdc_status(vis->mpd_conn);
          mpdc_idle(vis->mpd_conn, MPDC_EVENT_PLAYER | MPDC_EVENT_MESSAGE);
        }
    }

    if(events && vis->fds[3].revents & IOPAUSE_WRITE) {
        if(mpdc_send(vis->mpd_conn) == -1) {
          mpdc_disconnect(vis->mpd_conn);
          if(!mpdc_connect(vis->mpd_conn)) strerr_die1x(1,"error: could not reconnect to mpd");
          mpdc_subscribe(vis->mpd_conn,"visualizer");
          mpdc_currentsong(vis->mpd_conn);
          mpdc_status(vis->mpd_conn);
          mpdc_idle(vis->mpd_conn, MPDC_EVENT_PLAYER | MPDC_EVENT_MESSAGE);
        }
    }

    }

    return 0;
}

int visualizer_cleanup(visualizer *vis) {
    int draining = 0;
    if(vis->fds[2].fd != -1) {
        draining = 1;
        while(draining) {
            ndelay_off(vis->fds[2].fd);
            if(ringbuf_write(vis->stream.frames,ringbuf_bytes_used(vis->stream.frames)) < 0) {
                draining = 0;
            }
            visualizer_make_frames(vis);
            if(ringbuf_bytes_used(vis->processor.samples) < vis->processor.output_buffer_len &&
               ringbuf_bytes_used(vis->stream.frames) < vis->stream.frame_len) {
                draining = 0;
            }

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
