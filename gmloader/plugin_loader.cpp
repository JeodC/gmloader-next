#include <dlfcn.h>
#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"
#include "configuration.h"
#include "gml_plugin_api.h"

static void populate_api(gml_plugin_api_t *api)
{
    api->version        = GML_PLUGIN_API_VERSION;
    api->Function_Add   = Function_Add;
    api->YYGetInt32     = YYGetInt32;
    api->YYGetInt64     = YYGetInt64;
    api->YYGetReal      = YYGetReal;
    api->YYCreateString = YYCreateString;
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
