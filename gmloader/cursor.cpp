#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform.h"
#include "khronos/gles2.h"
#include "configuration.h"
#include "cursor.h"

// A common arrow pointer, drawn as a GLES2 textured quad on top of the game's
// frame.
static const int CUR_W = 12;
static const int CUR_H = 19;
static const char *CUR_PIX[CUR_H] = {
    "b...........",
    "bb..........",
    "bwb.........",
    "bwwb........",
    "bwwwb.......",
    "bwwwwb......",
    "bwwwwwb.....",
    "bwwwwwwb....",
    "bwwwwwwwb...",
    "bwwwwwwwwb..",
    "bwwwwwbbbbb.",
    "bwwbwwb.....",
    "bwb.bwwb....",
    "bb..bwwb....",
    "b....bwwb...",
    ".....bwwb...",
    "......bwwb..",
    "......bwwb..",
    ".......bb...",
};

static const char *cursor_vert_src =
    "#version 100\n"
    "varying vec2 vTexCoord;\n"
    "attribute vec2 aVertCoord;\n"
    "attribute vec2 aTexCoord;\n"
    "void main() {\n"
    "   vTexCoord = aTexCoord;\n"
    "   gl_Position = vec4(aVertCoord, 0.0, 1.0);\n"
    "}";

static const char *cursor_frag_src =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "   gl_FragColor = texture2D(uTex, vTexCoord);\n"
    "}\n";

static bool cursor_ready = false;
static bool cursor_failed = false;

static GLuint cur_prog = 0, cur_tex = 0, cur_vbo = 0;
static GLint cur_loc_vert = -1, cur_loc_uv = -1, cur_loc_tex = -1;
static float cur_u = 1.0f, cur_v = 1.0f;

static GLuint build_cursor_texture(void)
{
    uint8_t px[CUR_W * CUR_H * 4];
    for (int y = 0; y < CUR_H; y++) {
        for (int x = 0; x < CUR_W; x++) {
            uint8_t *p = &px[(y * CUR_W + x) * 4];
            char c = CUR_PIX[y][x];
            uint8_t lum = (c == 'w') ? 255 : 0;
            p[0] = p[1] = p[2] = lum;
            p[3] = (c == '.') ? 0 : 255;
        }
    }

    int tex_w = 1; while (tex_w < CUR_W) tex_w *= 2;
    int tex_h = 1; while (tex_h < CUR_H) tex_h *= 2;
    cur_u = (float)CUR_W / (float)tex_w;
    cur_v = (float)CUR_H / (float)tex_h;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CUR_W, CUR_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

static void cursor_init(void)
{
    cur_tex = build_cursor_texture();

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &cursor_vert_src, NULL);
    glCompileShader(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &cursor_frag_src, NULL);
    glCompileShader(frag);

    cur_prog = glCreateProgram();
    glAttachShader(cur_prog, vert);
    glAttachShader(cur_prog, frag);
    glLinkProgram(cur_prog);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = GL_FALSE;
    glGetProgramiv(cur_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        warning("Software cursor: shader program failed to link, disabling.\n");
        glDeleteProgram(cur_prog);
        glDeleteTextures(1, &cur_tex);
        cur_prog = cur_tex = 0;
        cursor_failed = true;
        return;
    }

    cur_loc_vert = glGetAttribLocation(cur_prog, "aVertCoord");
    cur_loc_uv   = glGetAttribLocation(cur_prog, "aTexCoord");
    cur_loc_tex  = glGetUniformLocation(cur_prog, "uTex");

    glGenBuffers(1, &cur_vbo);
    cursor_ready = true;
}

void cursor_render(void *sdl_win, int vp_w, int vp_h)
{
    (void)sdl_win;
    if (cursor_failed || vp_w <= 0 || vp_h <= 0)
        return;
    if (!cursor_ready) {
        cursor_init();
        if (!cursor_ready)
            return;
    }

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    // Keep the pointer a consistent on-screen size across resolutions.
    float base = vp_h / 360.0f;
    if (base < 1.0f) base = 1.0f;
    float user = gmloader_config.cursor_scale;
    if (user <= 0.0f) user = 1.0f;
    float scale = base * user;
    if (scale > 32.0f) scale = 32.0f;
    float cw = CUR_W * scale;
    float ch = CUR_H * scale;

    float lx = ((float)mx / vp_w) * 2.0f - 1.0f;
    float rx = ((float)(mx + cw) / vp_w) * 2.0f - 1.0f;
    float ty = 1.0f - ((float)my / vp_h) * 2.0f;
    float by = 1.0f - ((float)(my + ch) / vp_h) * 2.0f;

    const float verts[16] = {
        lx, by, 0.0f,   cur_v,
        lx, ty, 0.0f,   0.0f,
        rx, by, cur_u,  cur_v,
        rx, ty, cur_u,  0.0f,
    };

    // Save the slice of GL state we touch so the game's next frame is unaffected.
    GLint prev_prog = 0, prev_buf = 0, prev_tex0 = 0, prev_active = 0, prev_vp[4] = {0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_buf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex0);

    glViewport(0, 0, vp_w, vp_h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(cur_prog);
    glBindTexture(GL_TEXTURE_2D, cur_tex);
    glUniform1i(cur_loc_tex, 0);

    glBindBuffer(GL_ARRAY_BUFFER, cur_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(cur_loc_vert);
    glEnableVertexAttribArray(cur_loc_uv);
    glVertexAttribPointer(cur_loc_vert, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(0));
    glVertexAttribPointer(cur_loc_uv,   2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(cur_loc_vert);
    glDisableVertexAttribArray(cur_loc_uv);

    glBindBuffer(GL_ARRAY_BUFFER, prev_buf);
    glBindTexture(GL_TEXTURE_2D, prev_tex0);
    glActiveTexture(prev_active);
    glUseProgram(prev_prog);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
}
