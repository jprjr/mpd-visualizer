#include <stdio.h>
#include <stdlib.h>
#include <skalibs/skalibs.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include "visualizer.h"
#include "whereami.h"

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
#define diemem() strerr_die1x(1, "Unable to malloc memory")


int main(int argc, char const *const *argv) {
    visualizer _vis = VISUALIZER_ZERO;
    visualizer *vis = &_vis;

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
    int loopres = 0;

    char opt = 0;

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
        !input_path ||
        !output_path ) dieusage();


    if(!visualizer_init(vis,
                        video_width,
                        video_height,
                        framerate,
                        samplerate,
                        channels,
                        samplesize,
                        bars,
                        input_path,
                        output_path,
                        lua_folder)) {
        strerr_die1x(1,"Unable to create visualizer");
    }

    while((loopres = visualizer_loop(vis)) != -1) {
    }

    visualizer_cleanup(vis);

    return 0;

}
