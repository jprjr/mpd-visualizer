#ifndef VISUALIZER_INT_H
#define VISUALIZER_INT_H

#include "visualizer.h"

#ifdef __cplusplus
extern "C" {

#endif

static inline int
visualizer_grab_audio(visualizer *vis, int fd);

static inline int
visualizer_make_frames(visualizer *vis);

static int
visualizer_free(visualizer *vis);

static void
visualizer_load_scripts(visualizer *vis);


#ifdef __cplusplus
}
#endif

#endif
