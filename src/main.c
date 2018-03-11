#include <stdio.h>
#include <stdlib.h>
#include <skalibs/skalibs.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
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
               "  -l /path/to/lua/scripts\n" \
               "  -m (1|0) enable/disable mpd\n" \
               "Following options only valid when -m=0\n" \
               "  -t title\n" \
               "  -a artist\n" \
               "  -A album\n" \
               "  -F filename\n" \
               "  -T totaltime (in seconds)\n"

#define dieusage() strerr_die1x(1, USAGE)
#define diemem() strerr_die1x(1, "Unable to malloc memory")


int main(int argc, char const *const *argv) {
    visualizer _vis = VISUALIZER_ZERO;
    visualizer *vis = &_vis;

    int loopres = 0;
    char opt = 0;
    unsigned int totaltime = 0;

    subgetopt_t l = SUBGETOPT_ZERO;

    while((opt = subgetopt_r(argc,argv,"w:h:f:r:c:s:b:i:o:l:m:t:a:A:F:T:",&l)) != -1 ) {
        switch(opt) {
            case 'w': {
                if(!uint_scan(l.arg,&(vis->video_width))) dieusage();
                break;
            }
            case 'h': {
                if(!uint_scan(l.arg,&(vis->video_height))) dieusage();
                break;
            }
            case 'f': {
                if(!uint_scan(l.arg,&(vis->framerate))) dieusage();
                break;
            }
            case 'r': {
                if(!uint_scan(l.arg,&(vis->samplerate))) dieusage();
                break;
            }
            case 'c': {
                if(!uint_scan(l.arg,&(vis->channels))) dieusage();
                break;
            }
            case 'b': {
                if(!uint_scan(l.arg,&(vis->bars))) dieusage();
                break;
            }
            case 's': {
                if(!uint_scan(l.arg,&(vis->samplesize))) dieusage();
                break;
            }
            case 'm': {
                if(!uint_scan(l.arg,&(vis->mpd))) dieusage();
                if(vis->mpd > 1) dieusage();
                break;
            }
            case 'i': {
                vis->input_fifo = l.arg;
                break;
            }
            case 'o': {
                vis->output_fifo = l.arg;
                break;
            }
            case 'l': {
                vis->lua_folder = l.arg;
                break;
            }
            case 't': {
                vis->title = l.arg;
                break;
            }
            case 'a': {
                vis->artist = l.arg;
                break;
            }
            case 'A': {
                vis->album = l.arg;
                break;
            }
            case 'F': {
                vis->filename = l.arg;
                break;
            }
            case 'T': {
                if(!uint_scan(l.arg,&totaltime)) dieusage();
                vis->totaltime = totaltime;
                break;
            }
            default: dieusage();
        }
    }

    if(!visualizer_init(vis)) dieusage();

    while((loopres = visualizer_loop(vis)) != -1) {
    }

    visualizer_cleanup(vis);

    return 0;

}
