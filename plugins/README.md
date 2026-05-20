# Plugins

Optional shared-library extensions loaded by gmloader-next at runtime. Each plugin is a standard aarch64 `libfoo.so` that the host opens with `dlopen` after `libyoyo.so` is patched. Plugins register GameMaker function bindings via the host-provided ABI in [`../gmloader/gml_plugin_api.h`](../gmloader/gml_plugin_api.h).

## Loading

Ports enable plugins via `gmloader.json`:

```json
{
    "plugins": [
        { "path": "libfmodgms2.so", "config": { "bank_dir": "fmodBanks" } },
        { "path": "libfmod-gamemaker.so" }
    ]
}
```

`path` is resolved through `LD_LIBRARY_PATH` (which the port launcher sets to include `$GAMEDIR/libs`). `config` is optional; the host serializes it and hands the JSON string to the plugin's `gml_plugin_register` entry.

## ABI

Plugins export one symbol:

```c
extern "C" int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json);
```

`api` carries function pointers for `Function_Add`, `YYGetInt32/64/Real`, and `YYCreateString` resolved out of libyoyo. Return 0 on success.

## Existing plugins

- [`fmod-gamemaker/`](fmod-gamemaker) -- the `fmod-gamemaker` GML extension (snake_case flat API: `fmod_init`, `fmod_event_one_shot`, ...).
- [`fmodgms2/`](fmodgms2) -- the community `fmodgms2` extension (camelCase class-style: `studioSystem_initialize`, `eventInstance_start`, ...).

Each links its own copy of `FMOD_SDL` and the FMOD Studio API libraries.

## FMOD version compatibility

The FMOD plugins must be built against FMOD Studio API headers whose SONAME matches the libfmod / libfmodstudio used by the game. The shipped library SONAME has to match the runtime SONAME the bank files were authored against.

If you need an older FMOD generation, override `FMOD_DIR` per plugin to point at a sibling SDK extraction at that version, and ship the matching `libfmod.so.N` / `libfmodstudio.so.N`.
