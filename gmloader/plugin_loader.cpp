#include <algorithm>
#include <dlfcn.h>
#include <string>
#include <vector>
#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"
#include "configuration.h"
#include "gml_plugin_api.h"
#include "plugin_loader.h"

static const gml_video_backend_t *g_video_backend = nullptr;

static void register_video_backend(const gml_video_backend_t *backend)
{
    g_video_backend = backend;
}

const gml_video_backend_t *get_video_backend()
{
    return g_video_backend;
}

struct FuncRef {
    const char *name;
    int32_t id;
};

struct extension_t {
    FuncRef *ref;
    routine_t func;
};

extern so_module *libyoyo;

static std::vector<extension_t> extensions;

ABI_ATTR static RValue *call_extension(void *self, void *other, RValue *ret, int argc, int id, RValue **args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0;
    for (const auto &e : extensions) {
        if (e.ref->id != id)
            continue;
        RValue copy[16] = {};
        argc = std::min(argc, 16);
        for (int i = 0; i < argc; i++)
            copy[i] = *args[i];
        e.func(ret, self, other, argc, copy);
        break;
    }
    return ret;
}

// YYC calls extension functions by ID, so Function_Add only works for VM games
static void Extension_Add(const char *name, routine_t func, int argc)
{
    auto ref = (FuncRef *)so_symbol(libyoyo, ("g_FUNC_EXT_" + std::string(name)).c_str());
    if (!ref) {
        Function_Add(name, func, argc, 1);
        return;
    }
    if (gmloader_config.disable_extensions) {
        warning("%s needs \"disable_extensions\": false\n", name);
        return;
    }
    if (extensions.empty())
        hook_symbol(libyoyo, "_Z27YYGML_CallExtensionFunctionP9CInstanceS0_R8YYRValueiiPPS1_", (uintptr_t)call_extension, 1);
    extensions.push_back({ref, func});
}

static void populate_api(gml_plugin_api_t *api)
{
    api->Function_Add           = Function_Add;
    api->YYGetInt32             = YYGetInt32;
    api->YYGetInt64             = YYGetInt64;
    api->YYGetReal              = YYGetReal;
    api->YYCreateString         = YYCreateString;
    api->register_video_backend = register_video_backend;
    api->Extension_Add          = Extension_Add;
}

void load_plugins()
{
    if (gmloader_config.plugins.empty())
        return;

    gml_plugin_api_t api;
    populate_api(&api);

    for (const auto &p : gmloader_config.plugins) {
        void *h = dlopen(p.path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            warning("Plugin %s not found: %s\n", p.path.c_str(), dlerror());
            continue;
        }

        auto reg = (gml_plugin_register_fn)dlsym(h, "gml_plugin_register");
        if (!reg) {
            warning("Plugin %s missing gml_plugin_register, skipping.\n", p.path.c_str());
            dlclose(h);
            continue;
        }

        int r = reg(&api, p.config_json.c_str());
        if (r != 0)
            warning("Plugin %s gml_plugin_register returned %d.\n", p.path.c_str(), r);
    }
}
