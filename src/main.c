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
#define dienolib() strerr_die1x(1,"Unable to load library")
#define dienofunc(f) strerr_die2x(1,"Unable to load function: ",f)

typedef int (*vis_init_ptr)(
      visualizer *,
      unsigned int,
      unsigned int,
      unsigned int,
      unsigned int,
      unsigned int,
      unsigned int,
      unsigned int,
      const char *,
      const char *,
      const char *);

typedef int (*vis_func_ptr)(visualizer *);

int load_libs( char *path, void **so_handle , vis_init_ptr *vis_init, vis_func_ptr *vis_loop, vis_func_ptr *vis_clean, vis_func_ptr *vis_reload , vis_func_ptr *vis_unload) {
    fprintf(stderr,"loading %s\n",path);

    *so_handle = dlopen(path,RTLD_NOW);
    if(*so_handle == NULL) {
        dienolib();
    }

    *vis_init = (vis_init_ptr)dlsym(*so_handle,"visualizer_init");
    if(*vis_init == NULL) {
        dienofunc("visualizer_init");
    }

    *vis_loop = (vis_func_ptr)dlsym(*so_handle,"visualizer_loop");
    if(*vis_loop == NULL) {
        dienofunc("visualizer_loop");
    }

    *vis_clean = (vis_func_ptr)dlsym(*so_handle,"visualizer_cleanup");
    if(*vis_clean == NULL) {
        dienofunc("visualizer_cleanup");
    }

    *vis_reload = (vis_func_ptr)dlsym(*so_handle,"visualizer_reload");
    if(*vis_reload == NULL) {
        dienofunc("visualizer_reload");
    }

    *vis_unload = (vis_func_ptr)dlsym(*so_handle,"visualizer_unload");
    if(*vis_unload == NULL) {
        dienofunc("visualizer_unload");
    }

    return 1;
}

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
    int path_len = 0;
    int dir_len = 0;
    char *path = NULL;
    char *lib_path = NULL;

    const char *input_path  = NULL;
    const char *output_path = NULL;
    const char *lua_folder  = NULL;
    int loopres = 0;

    void *so_handle = NULL;
    vis_init_ptr vis_init = NULL;
    vis_func_ptr vis_loop = NULL;
    vis_func_ptr vis_clean = NULL;
    vis_func_ptr vis_reload = NULL;
    vis_func_ptr vis_unload = NULL;

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

    path_len = wai_getExecutablePath(NULL,0,NULL);
    path = (char *)malloc(path_len + 1);
    if(path == NULL) {
        diemem();
    }
    path_len = wai_getExecutablePath(path,path_len,&dir_len);
    path[dir_len] = '\0';

    dir_len += strlen("/" SO_PREFIX "visualizer" SO_EXT);

    lib_path = (char *)malloc(dir_len + 1);
    if(lib_path == NULL) {
        diemem();
    }

    strcpy(lib_path,path);
    strcat(lib_path,"/" SO_PREFIX "visualizer" SO_EXT);

    load_libs(lib_path, &so_handle, &vis_init, &vis_loop, &vis_clean, &vis_reload, &vis_unload);

    if(!(vis_init)(vis,
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

    while((loopres = (vis_loop(vis))) != -1) {
        if(vis->reload) {
            (vis_unload)(vis);
            dlclose(so_handle);
            load_libs(lib_path, &so_handle, &vis_init, &vis_loop, &vis_clean, &vis_reload, &vis_unload);
            (vis_reload)(vis);
            vis->reload = 0;
        }
    }

    (vis_clean)(vis);

    free(path);
    free(lib_path);
    return 0;

}
