#pragma once

// ABI between gmloader-next and external GML extension plugins.

#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"

struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gml_video_backend {
    int    (*init)(struct SDL_Window *win, const char *save_dir);
    void   (*process)(void);

    void   (*open)(const char *path);
    void   (*close)(void);
    int    (*draw)(void *buffer);
    void   (*set_volume)(double volume);
    void   (*seek_to)(double time);
    void   (*enable_loop)(double loop);
    void   (*pause)(void);
    void   (*resume)(void);
    double (*status)(void);
    double (*get_status)(void);
    double (*get_format)(void);
    double (*get_width)(void);
    double (*get_height)(void);
    double (*get_duration)(void);
    double (*get_position)(void);
    double (*get_volume)(void);
    double (*is_looping)(void);
} gml_video_backend_t;

typedef struct gml_plugin_api {
    void     (*Function_Add)(const char *name, routine_t func, int argc, char reg);
    int32_t  (*YYGetInt32)(RValue *val, int idx);
    int64_t  (*YYGetInt64)(RValue *val, int idx);
    double   (*YYGetReal)(RValue *val, int idx);
    void     (*YYCreateString)(RValue *val, const char *str);

    void     (*register_video_backend)(const gml_video_backend_t *backend);
} gml_plugin_api_t;

typedef int (*gml_plugin_register_fn)(const gml_plugin_api_t *api, const char *config_json);

#ifdef __cplusplus
}
#endif
