#include <stdio.h>
#include <stdlib.h>
#include <skalibs/skalibs.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "visualizer.h"

#define USAGE  "Usage: visualizer (options)\n" \
               "Options:\n" \
               "  -w width\n" \
               "  -h height\n" \
               "  -f framerate\n" \
               "  -r samplerate\n" \
               "  -c channels\n" \
               "  -s samplesize (in bytes)\n" \
               "  -b number of visualizer bars to calculate\n" \
               "  -i /path/to/input\n" \
               "  -o /path/to/output\n" \
               "  -l /path/to/lua/scripts\n"

#define dieusage() strerr_die1x(1, USAGE)

int main(int argc, char const *const *argv) {
    visualizer _vis = VISUALIZER_ZERO;
    visualizer *vis = &_vis;

    int own_fifo = -1;

    unsigned int video_width = 0;
    unsigned int video_height = 0;
    unsigned int framerate = 0;
    unsigned int samplerate = 0;
    unsigned int channels = 0;
    unsigned int samplesize = 0;
    unsigned int bars = 0;

    const char *input_path  = NULL;
    const char *output_path = NULL;
    const char *lua_folder  = NULL;

    struct stat st;
    int events = 0;
    int signal = 0;

    unsigned int frames_written = 0;
    unsigned int frame_counter = 0;

    tain_t now = TAIN_ZERO;
    tain_t diff= TAIN_ZERO;

    char opt = 0;

    double average_fps = 0.0f;
    double desired_fps = 0.0f;

    iopause_fd fds[2];

    fds[0].fd = selfpipe_init();
    fds[0].events = IOPAUSE_READ;

    if(fds[0].fd < 0) {
        strerr_die1sys(1,"Unable to create selfpipe");
    }
    if(selfpipe_trap(SIGINT) < 0)
        strerr_warn1sys("Unable to trap SIGINT: ");
    if(selfpipe_trap(SIGTERM) < 0)
        strerr_warn1sys("Unable to trap SIGTERM: ");
    if(selfpipe_trap(SIGPIPE) < 0)
        strerr_warn1sys("Unable to trap SIGPIPE: ");
    if(selfpipe_trap(SIGUSR1) < 0)
        strerr_warn1sys("Unable to trap SIGUSR1: ");

    fds[1].fd = -1;
    fds[1].events = 0;

    subgetopt_t l = SUBGETOPT_ZERO;

    while((opt = subgetopt_r(argc,argv,"w:h:f:r:c:s:b:i:o:l:",&l)) != -1 ) {
        switch(opt) {
            case 'w': {
                if(!uint_scan(l.arg,&video_width)) dieusage();
                break;
            }
            case 'h': {
                if(!uint_scan(l.arg,&video_height)) dieusage();
                break;
            }
            case 'f': {
                if(!uint_scan(l.arg,&framerate)) dieusage();
                break;
            }
            case 'r': {
                if(!uint_scan(l.arg,&samplerate)) dieusage();
                break;
            }
            case 'c': {
                if(!uint_scan(l.arg,&channels)) dieusage();
                break;
            }
            case 'b': {
                if(!uint_scan(l.arg,&bars)) dieusage();
                break;
            }
            case 's': {
                if(!uint_scan(l.arg,&samplesize)) dieusage();
                break;
            }
            case 'i': {
                input_path = l.arg;
                break;
            }
            case 'o': {
                output_path = l.arg;
                break;
            }
            case 'l': {
                lua_folder = l.arg;
                break;
            }
            default: dieusage();
        }
    }
    if(
        !video_width ||
        !video_height ||
        !framerate ||
        !samplerate ||
        !channels ||
        !samplesize ||
        !input_path ||
        !bars ||
        !output_path ) dieusage();
    average_fps = (double)framerate;
    desired_fps = 1000.0f * average_fps;

    if(!visualizer_init(vis,
                        video_width,
                        video_height,
                        framerate,
                        samplerate,
                        channels,
                        samplesize,
                        bars,
                        output_path,
                        lua_folder)) {
        strerr_die1x(1,"Unable to create visualizer");
    }

    own_fifo = mkfifo(output_path,
      S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

    if(own_fifo < 0) {
        if(stat(output_path,&st) == 0) {
            if(!S_ISFIFO(st.st_mode)) {
                strerr_die3x(1,"Output path ",output_path," exists and is not a fifo");
            }
            else {
                own_fifo = 0;
            }
        }
    }
    else {
        own_fifo = 1;
    }

    if(own_fifo < 0) {
        strerr_die3x(1,"Problem opening ", output_path," for file writing");
    }

    fds[1].fd = open_read(input_path);
    if(fds[1].fd == -1) {
        strerr_die3sys(1,"Problem opening ",input_path,": ");
    }
    fds[1].events = IOPAUSE_READ;

    tain_clockmon_init(vis->tain_offset);
    tain_clockmon(vis->tain_last,vis->tain_offset);

    while( 1 ) {
        /* select()-based iopause returns IOPAUSE_EXCEPT
         * if the data read was incomplete */
        events = iopause_stamp(fds,2,0,&now);
        if(events && fds[0].revents & IOPAUSE_READ) {
            signal = selfpipe_read();
            switch(signal) {
                case SIGINT: goto cleanup;
                case SIGTERM: goto cleanup;
                case SIGUSR1: {
                    strerr_warn1x("Reloading images/scripts");
                    visualizer_load_scripts(vis);
                    break;

                }
            }
        }

        if(events && fds[1].revents & IOPAUSE_READ) {
            if(visualizer_grab_audio(vis,fds[1].fd) <= 0) goto breakout;
            do {
                frames_written = visualizer_write_frames(vis);
                frame_counter += frames_written;
            } while (frames_written > 0);
        }
        if(frame_counter >= framerate) {
            tain_clockmon(vis->tain_cur,vis->tain_offset);
            tain_sub(&diff,vis->tain_cur,vis->tain_last);
            average_fps += desired_fps / (double)tain_to_millisecs(&diff);
            average_fps /= 2;
            frame_counter = 0;
            if(average_fps < framerate - 1) {
                fprintf(stderr,"WARNING: average fps too low, want %d, currently %f\n",framerate,average_fps);
            }
            tain_clockmon(vis->tain_last,vis->tain_offset);
        }
        lua_gc(vis->Lua,LUA_GCCOLLECT,0);
    }

    breakout:
    visualizer_write_frames(vis);

    cleanup:
    fd_close(fds[0].fd);
    fd_close(fds[1].fd);
    visualizer_free(vis);
    if(own_fifo) unlink(output_path);

    return 0;
}
