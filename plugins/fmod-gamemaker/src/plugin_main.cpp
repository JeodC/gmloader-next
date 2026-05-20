// Plugin entry: populates the libyoyo function pointers from the host's API,
// then registers every fmod-gamemaker GML binding.

#include "platform.h"
#include "gml_plugin_api.h"

ABI_ATTR fct_add_t Function_Add = nullptr;
ABI_ATTR int32_t (*YYGetInt32)(RValue *val, int idx) = nullptr;
ABI_ATTR int64_t (*YYGetInt64)(RValue *val, int idx) = nullptr;
ABI_ATTR double  (*YYGetReal)(RValue *val, int idx) = nullptr;
ABI_ATTR void    (*YYCreateString)(RValue *val, const char *str) = nullptr;

// Forward declarations for the binding implementations (in fmod-gamemaker.cpp).
#define BIND(name) \
    ABI_ATTR void name(RValue *, void *, void *, int, RValue *);

BIND(fmod_init) BIND(fmod_destroy) BIND(fmod_bank_load) BIND(fmod_update)
BIND(fmod_event_create_instance) BIND(fmod_event_instance_play)
BIND(fmod_event_instance_stop) BIND(fmod_event_instance_release)
BIND(fmod_event_instance_set_3d_attributes) BIND(fmod_set_listener_attributes)
BIND(fmod_set_num_listeners) BIND(fmod_event_instance_set_parameter)
BIND(fmod_event_instance_get_parameter) BIND(fmod_set_parameter)
BIND(fmod_get_parameter) BIND(fmod_event_instance_set_paused)
BIND(fmod_event_instance_get_paused) BIND(fmod_event_instance_set_paused_all)
BIND(fmod_event_one_shot) BIND(fmod_event_one_shot_3d)
BIND(fmod_event_instance_is_playing) BIND(fmod_event_instance_get_timeline_pos)
BIND(fmod_event_instance_set_timeline_pos) BIND(fmod_bank_load_sample_data)
BIND(fmod_event_get_length)

#undef BIND

extern "C" __attribute__((visibility("default")))
int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json)
{
    (void)config_json;
    if (!api || api->version != GML_PLUGIN_API_VERSION)
        return -1;

    Function_Add   = api->Function_Add;
    YYGetInt32     = api->YYGetInt32;
    YYGetInt64     = api->YYGetInt64;
    YYGetReal      = api->YYGetReal;
    YYCreateString = api->YYCreateString;

    Function_Add("fmod_init",                              fmod_init,                              1, 0);
    Function_Add("fmod_destroy",                           fmod_destroy,                           0, 0);
    Function_Add("fmod_bank_load",                         fmod_bank_load,                         1, 0);
    Function_Add("fmod_update",                            fmod_update,                            0, 0);
    Function_Add("fmod_event_create_instance",             fmod_event_create_instance,             1, 0);
    Function_Add("fmod_event_instance_play",               fmod_event_instance_play,               0, 0);
    Function_Add("fmod_event_instance_stop",               fmod_event_instance_stop,               0, 0);
    Function_Add("fmod_event_instance_release",            fmod_event_instance_release,            0, 0);
    Function_Add("fmod_event_instance_set_3d_attributes",  fmod_event_instance_set_3d_attributes,  3, 0);
    Function_Add("fmod_set_listener_attributes",           fmod_set_listener_attributes,           3, 0);
    Function_Add("fmod_set_num_listeners",                 fmod_set_num_listeners,                 1, 0);
    Function_Add("fmod_event_instance_set_parameter",      fmod_event_instance_set_parameter,      3, 0);
    Function_Add("fmod_event_instance_get_parameter",      fmod_event_instance_get_parameter,      1, 0);
    Function_Add("fmod_set_parameter",                     fmod_set_parameter,                     2, 0);
    Function_Add("fmod_get_parameter",                     fmod_get_parameter,                     1, 0);
    Function_Add("fmod_event_instance_set_paused",         fmod_event_instance_set_paused,         2, 0);
    Function_Add("fmod_event_instance_get_paused",         fmod_event_instance_get_paused,         0, 0);
    Function_Add("fmod_event_instance_set_paused_all",     fmod_event_instance_set_paused_all,     1, 0);
    Function_Add("fmod_event_one_shot",                    fmod_event_one_shot,                    1, 0);
    Function_Add("fmod_event_one_shot_3d",                 fmod_event_one_shot_3d,                 0, 0);
    Function_Add("fmod_event_instance_is_playing",         fmod_event_instance_is_playing,         0, 0);
    Function_Add("fmod_event_instance_get_timeline_pos",   fmod_event_instance_get_timeline_pos,   0, 0);
    Function_Add("fmod_event_instance_set_timeline_pos",   fmod_event_instance_set_timeline_pos,   2, 0);
    Function_Add("fmod_bank_load_sample_data",             fmod_bank_load_sample_data,             1, 0);
    Function_Add("fmod_event_get_length",                  fmod_event_get_length,                  1, 0);

    return 0;
}
