#pragma once

#include <GLES2/gl2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Movement since the last ds_touch() call, in second-screen pixels.
typedef struct ds_touch_state {
    double dx, dy;             // one-finger drag
    double zoom;               // pinch, in powers of two
    double x, y;               // latest position of the first finger
    double origin_x, origin_y; // where the first finger went down
    int tapped;                // the finger lifted without moving
} ds_touch_state;

// Open a fullscreen window on the display the game isn't using.
int ds_open(void);
int ds_ready(void);
void ds_size(int *w, int *h);

// Texture attached to the framebuffer the game is drawing to.
GLuint ds_bound_texture(void);

// Draw between begin and end.
int ds_begin(void);
void ds_draw(GLuint texture, int x, int y, int w, int h, float u, float v, float uw, float vh, float brightness, int blend);
void ds_end(void);

int ds_touch(ds_touch_state *out);

#ifdef __cplusplus
}
#endif
