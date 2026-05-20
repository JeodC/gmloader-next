#pragma once

// ABI between gmloader-next and external GML extension plugins. 
// The host populates a gml_plugin_api_t from libyoyo symbol pointers 
// and calls gml_plugin_register on the plugin so file.

#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"

#define GML_PLUGIN_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gml_plugin_api {
    int      version;

    // libyoyo accessors resolved at runtime by gmloader-next.
    void     (*Function_Add)(const char *name, routine_t func, int argc, char reg);
    int32_t  (*YYGetInt32)(RValue *val, int idx);
    int64_t  (*YYGetInt64)(RValue *val, int idx);
    double   (*YYGetReal)(RValue *val, int idx);
    void     (*YYCreateString)(RValue *val, const char *str);
} gml_plugin_api_t;

// Plugin entry point. Return 0 on success, nonzero on failure.
typedef int (*gml_plugin_register_fn)(const gml_plugin_api_t *api, const char *config_json);

#ifdef __cplusplus
}
#endif
