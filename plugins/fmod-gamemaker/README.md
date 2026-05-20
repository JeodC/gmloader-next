# fmod-gamemaker

Loadable plugin for gmloader-next that implements the `fmod-gamemaker` GML
extension surface. Function names match the Windows DLL's public exports
(`FMOD_Init`, `FMOD_Bank_Load`, `FMOD_Event_CreateInstance`, etc.) per the
embedded PDB path `D:\fmod-gamemaker\src\fmod-vs\x64\Release\fmod-gamemaker.pdb`.

API style is snake_case flat functions: `fmod_init`, `fmod_event_create_instance`,
`fmod_set_parameter`, `fmod_event_one_shot`. Distinct from `fmodgms2` which
uses camelCase class-prefixed names.

## Build

Run from the gmloader-next root once the FMOD Studio API SDK is extracted
into `3rdparty/fmod/`:

```sh
make -C plugins/fmod-gamemaker ARCH=aarch64-linux-gnu
```

Outputs `build/libfmod-gamemaker.so`. Ship in the port's `libs/` directory
next to a matching `libfmod.so.N` / `libfmodstudio.so.N` pair -- see
[`../README.md`](../README.md) for SONAME compatibility notes. Verified
against FMOD Studio API 2.02.34.

Reference from `gmloader.json`:

```json
{
    "plugins": [
        { "path": "libfmod-gamemaker.so" }
    ]
}
```

The plugin takes no config keys.
