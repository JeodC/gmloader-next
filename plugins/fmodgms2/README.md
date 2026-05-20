# fmodgms2

Loadable plugin for gmloader-next implementing the `fmodgms2` GML extension
API (~90 functions). Clean-room reimplementation: function names taken from
the public Windows DLL's PE exports, bodies map onto the documented FMOD
Studio API. CamelCase class-style: `studioSystem_initialize`,
`eventInstance_start`, `bus_setVolume`, etc. Distinct from `fmod-gamemaker`
which uses snake_case flat names.

## Build

Run from the gmloader-next root after extracting the FMOD Studio API SDK
into `3rdparty/fmod/`:

```sh
make -C plugins/fmodgms2 ARCH=aarch64-linux-gnu
```

Outputs `build/libfmodgms2.so`. Ship in the port's `libs/` directory next
to a matching `libfmod.so.N` / `libfmodstudio.so.N` pair -- see
[`../README.md`](../README.md) for SONAME compatibility notes. Verified
against FMOD Studio API 2.02.34.

## Config

```json
{
    "plugins": [
        {
            "path": "libfmodgms2.so",
            "config": {
                "bank_dir": "fmodBanks"
            }
        }
    ]
}
```

`bank_dir` is optional. When set, the plugin scans the directory at
`studioSystem_initialize` time and loads every metadata `*.bank` file
(master.strings.bank + master.bank first; `*.assets.bank` are skipped
because FMOD pulls them in implicitly when an event needs samples). When
unset, the game's GML is expected to call `studioSystem_loadBankFile`
directly.
