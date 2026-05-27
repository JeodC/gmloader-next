// Plugin entry: wires libyoyo function pointers from the host, parses our
// slice of gmloader.json, registers every fmodgms2 binding.

#include <string>
#include <nlohmann/json.hpp>
#include "platform.h"
#include "gml_plugin_api.h"

ABI_ATTR fct_add_t Function_Add = nullptr;
ABI_ATTR int32_t (*YYGetInt32)(RValue *val, int idx) = nullptr;
ABI_ATTR int64_t (*YYGetInt64)(RValue *val, int idx) = nullptr;
ABI_ATTR double  (*YYGetReal)(RValue *val, int idx) = nullptr;
ABI_ATTR void    (*YYCreateString)(RValue *val, const char *str) = nullptr;

std::string g_bank_dir;
bool g_dbg = false;  // gated by FMODGMS2_DEBUG env var

#define BIND(name) \
    ABI_ATTR void fmodgms2_##name(RValue *, void *, void *, int, RValue *);

BIND(fmod_final) BIND(fmod_getApiVersion) BIND(fmod_isLoaded)
BIND(fmod_releaseAllEventInstances) BIND(fmod_setDistanceScale)

BIND(studioSystem_initialize) BIND(studioSystem_unloadAll) BIND(studioSystem_update)
BIND(studioSystem_flushCommands) BIND(studioSystem_flushSampleLoading)
BIND(studioSystem_setNumListeners) BIND(studioSystem_getNumListeners)
BIND(studioSystem_setListener3DAttributes) BIND(studioSystem_getListener3DAttributes)
BIND(studioSystem_setListenerWeight) BIND(studioSystem_getListenerWeight)
BIND(studioSystem_setParameterByName) BIND(studioSystem_getParameterByName)
BIND(studioSystem_setParameterById) BIND(studioSystem_getParameterId)
BIND(studioSystem_startCommandCapture) BIND(studioSystem_stopCommandCapture)
BIND(studioSystem_loadCommandReplay) BIND(studioSystem_loadBankFile)
BIND(studioSystem_getBank) BIND(studioSystem_getBus) BIND(studioSystem_getVCA)
BIND(studioSystem_getEvent)

BIND(bank_isValid) BIND(bank_unload) BIND(bank_loadSampleData)
BIND(bank_unloadSampleData) BIND(bank_getLoadingState)
BIND(bank_getSampleLoadingState) BIND(bank_getPath)

BIND(bus_isValid) BIND(bus_getPath) BIND(bus_getVolume) BIND(bus_setVolume)
BIND(bus_getMute) BIND(bus_setMute) BIND(bus_getPaused) BIND(bus_setPaused)
BIND(bus_stopAllEvents)

BIND(vca_isValid) BIND(vca_getPath) BIND(vca_getVolume) BIND(vca_setVolume)

BIND(eventDescription_isValid) BIND(eventDescription_getPath)
BIND(eventDescription_getLength) BIND(eventDescription_is3D)
BIND(eventDescription_isOneshot) BIND(eventDescription_isSnapshot)
BIND(eventDescription_isStream) BIND(eventDescription_hasCue)
BIND(eventDescription_getParameterId) BIND(eventDescription_createInstance)
BIND(eventDescription_releaseAllInstances) BIND(eventDescription_loadSampleData)
BIND(eventDescription_unloadSampleData) BIND(eventDescription_getSampleLoadingState)

BIND(eventInstance_isValid) BIND(eventInstance_isVirtual) BIND(eventInstance_release)
BIND(eventInstance_start) BIND(eventInstance_stop) BIND(eventInstance_triggerCue)
BIND(eventInstance_getPlaybackState) BIND(eventInstance_getDescription)
BIND(eventInstance_getVolume) BIND(eventInstance_setVolume)
BIND(eventInstance_getPitch) BIND(eventInstance_setPitch)
BIND(eventInstance_getPaused) BIND(eventInstance_setPaused)
BIND(eventInstance_getTimelinePosition) BIND(eventInstance_setTimelinePosition)
BIND(eventInstance_get3DAttributes) BIND(eventInstance_set3DAttributes)
BIND(eventInstance_getParameterByName) BIND(eventInstance_setParameterByName)
BIND(eventInstance_setParameterById) BIND(eventInstance_setCallback)

BIND(commandreplay_release) BIND(commandreplay_start) BIND(commandreplay_stop)
BIND(commandreplay_setPaused) BIND(commandreplay_setBankPath)
BIND(commandreplay_seekToCommand) BIND(commandreplay_seekToTime)

#undef BIND

extern "C" __attribute__((visibility("default")))
int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json)
{
    if (!api)
        return -1;

    Function_Add   = api->Function_Add;
    YYGetInt32     = api->YYGetInt32;
    YYGetInt64     = api->YYGetInt64;
    YYGetReal      = api->YYGetReal;
    YYCreateString = api->YYCreateString;

    try {
        auto cfg = nlohmann::json::parse(config_json ? config_json : "{}");
        if (cfg.contains("bank_dir") && cfg["bank_dir"].is_string())
            g_bank_dir = cfg["bank_dir"].get<std::string>();
    } catch (...) {}
    if (const char *env = getenv("FMODGMS2_DEBUG"))
        g_dbg = (env[0] == '1' || env[0] == 'y' || env[0] == 'Y');
    warning("[fmodgms2] register: bank_dir=\"%s\" debug=%d\n", g_bank_dir.c_str(), g_dbg);

    // fmod_*
    Function_Add("fmod_final",                     fmodgms2_fmod_final,                     0, 0);
    Function_Add("fmod_getApiVersion",             fmodgms2_fmod_getApiVersion,             0, 0);
    Function_Add("fmod_isLoaded",                  fmodgms2_fmod_isLoaded,                  0, 0);
    Function_Add("fmod_releaseAllEventInstances",  fmodgms2_fmod_releaseAllEventInstances,  0, 0);
    Function_Add("fmod_setDistanceScale",          fmodgms2_fmod_setDistanceScale,          1, 0);
    // Symbol-resolution stub: referenced by FUNC_Load but never called.
    Function_Add("fmod_event_type",                fmodgms2_fmod_isLoaded,                  0, 0);

    // studioSystem_*
    Function_Add("studioSystem_initialize",                fmodgms2_studioSystem_initialize,                9, 0);
    Function_Add("studioSystem_unloadAll",                 fmodgms2_studioSystem_unloadAll,                 0, 0);
    Function_Add("studioSystem_update",                    fmodgms2_studioSystem_update,                    0, 0);
    Function_Add("studioSystem_flushCommands",             fmodgms2_studioSystem_flushCommands,             0, 0);
    Function_Add("studioSystem_flushSampleLoading",        fmodgms2_studioSystem_flushSampleLoading,        0, 0);
    Function_Add("studioSystem_setNumListeners",           fmodgms2_studioSystem_setNumListeners,           1, 0);
    Function_Add("studioSystem_getNumListeners",           fmodgms2_studioSystem_getNumListeners,           0, 0);
    Function_Add("studioSystem_setListener3DAttributes",   fmodgms2_studioSystem_setListener3DAttributes,  16, 0);
    Function_Add("studioSystem_getListener3DAttributes",   fmodgms2_studioSystem_getListener3DAttributes,   1, 0);
    Function_Add("studioSystem_setListenerWeight",         fmodgms2_studioSystem_setListenerWeight,         2, 0);
    Function_Add("studioSystem_getListenerWeight",         fmodgms2_studioSystem_getListenerWeight,         1, 0);
    Function_Add("studioSystem_setParameterByName",        fmodgms2_studioSystem_setParameterByName,        3, 0);
    Function_Add("studioSystem_getParameterByName",        fmodgms2_studioSystem_getParameterByName,        1, 0);
    Function_Add("studioSystem_setParameterById",          fmodgms2_studioSystem_setParameterById,          4, 0);
    Function_Add("studioSystem_getParameterId",            fmodgms2_studioSystem_getParameterId,            1, 0);
    Function_Add("studioSystem_startCommandCapture",       fmodgms2_studioSystem_startCommandCapture,       1, 0);
    Function_Add("studioSystem_stopCommandCapture",        fmodgms2_studioSystem_stopCommandCapture,        0, 0);
    Function_Add("studioSystem_loadCommandReplay",         fmodgms2_studioSystem_loadCommandReplay,         2, 0);
    Function_Add("studioSystem_loadBankFile",              fmodgms2_studioSystem_loadBankFile,              3, 0);
    Function_Add("studioSystem_getBank",                   fmodgms2_studioSystem_getBank,                   1, 0);
    Function_Add("studioSystem_getBus",                    fmodgms2_studioSystem_getBus,                    1, 0);
    Function_Add("studioSystem_getVCA",                    fmodgms2_studioSystem_getVCA,                    1, 0);
    Function_Add("studioSystem_getEvent",                  fmodgms2_studioSystem_getEvent,                  1, 0);

    // bank_*
    Function_Add("bank_isValid",                fmodgms2_bank_isValid,                1, 0);
    Function_Add("bank_unload",                 fmodgms2_bank_unload,                 1, 0);
    Function_Add("bank_loadSampleData",         fmodgms2_bank_loadSampleData,         1, 0);
    Function_Add("bank_unloadSampleData",       fmodgms2_bank_unloadSampleData,       1, 0);
    Function_Add("bank_getLoadingState",        fmodgms2_bank_getLoadingState,        1, 0);
    Function_Add("bank_getSampleLoadingState",  fmodgms2_bank_getSampleLoadingState,  1, 0);
    Function_Add("bank_getPath",                fmodgms2_bank_getPath,                1, 0);

    // bus_*
    Function_Add("bus_isValid",       fmodgms2_bus_isValid,       1, 0);
    Function_Add("bus_getPath",       fmodgms2_bus_getPath,       1, 0);
    Function_Add("bus_getVolume",     fmodgms2_bus_getVolume,     1, 0);
    Function_Add("bus_setVolume",     fmodgms2_bus_setVolume,     2, 0);
    Function_Add("bus_getMute",       fmodgms2_bus_getMute,       1, 0);
    Function_Add("bus_setMute",       fmodgms2_bus_setMute,       2, 0);
    Function_Add("bus_getPaused",     fmodgms2_bus_getPaused,     1, 0);
    Function_Add("bus_setPaused",     fmodgms2_bus_setPaused,     2, 0);
    Function_Add("bus_stopAllEvents", fmodgms2_bus_stopAllEvents, 2, 0);

    // vca_*
    Function_Add("vca_isValid",   fmodgms2_vca_isValid,   1, 0);
    Function_Add("vca_getPath",   fmodgms2_vca_getPath,   1, 0);
    Function_Add("vca_getVolume", fmodgms2_vca_getVolume, 1, 0);
    Function_Add("vca_setVolume", fmodgms2_vca_setVolume, 2, 0);

    // eventDescription_*
    Function_Add("eventDescription_isValid",               fmodgms2_eventDescription_isValid,               1, 0);
    Function_Add("eventDescription_getPath",               fmodgms2_eventDescription_getPath,               1, 0);
    Function_Add("eventDescription_getLength",             fmodgms2_eventDescription_getLength,             1, 0);
    Function_Add("eventDescription_is3D",                  fmodgms2_eventDescription_is3D,                  1, 0);
    Function_Add("eventDescription_isOneshot",             fmodgms2_eventDescription_isOneshot,             1, 0);
    Function_Add("eventDescription_isSnapshot",            fmodgms2_eventDescription_isSnapshot,            1, 0);
    Function_Add("eventDescription_isStream",              fmodgms2_eventDescription_isStream,              1, 0);
    Function_Add("eventDescription_hasCue",                fmodgms2_eventDescription_hasCue,                1, 0);
    Function_Add("eventDescription_getParameterId",        fmodgms2_eventDescription_getParameterId,        2, 0);
    Function_Add("eventDescription_createInstance",        fmodgms2_eventDescription_createInstance,        1, 0);
    Function_Add("eventDescription_releaseAllInstances",   fmodgms2_eventDescription_releaseAllInstances,   1, 0);
    Function_Add("eventDescription_loadSampleData",        fmodgms2_eventDescription_loadSampleData,        1, 0);
    Function_Add("eventDescription_unloadSampleData",      fmodgms2_eventDescription_unloadSampleData,      1, 0);
    Function_Add("eventDescription_getSampleLoadingState", fmodgms2_eventDescription_getSampleLoadingState, 1, 0);

    // eventInstance_*
    Function_Add("eventInstance_isValid",             fmodgms2_eventInstance_isValid,             1, 0);
    Function_Add("eventInstance_isVirtual",           fmodgms2_eventInstance_isVirtual,           1, 0);
    Function_Add("eventInstance_release",             fmodgms2_eventInstance_release,             1, 0);
    Function_Add("eventInstance_start",               fmodgms2_eventInstance_start,               1, 0);
    Function_Add("eventInstance_stop",                fmodgms2_eventInstance_stop,                2, 0);
    Function_Add("eventInstance_triggerCue",          fmodgms2_eventInstance_triggerCue,          1, 0);
    Function_Add("eventInstance_getPlaybackState",    fmodgms2_eventInstance_getPlaybackState,    1, 0);
    Function_Add("eventInstance_getDescription",      fmodgms2_eventInstance_getDescription,      1, 0);
    Function_Add("eventInstance_getVolume",           fmodgms2_eventInstance_getVolume,           1, 0);
    Function_Add("eventInstance_setVolume",           fmodgms2_eventInstance_setVolume,           2, 0);
    Function_Add("eventInstance_getPitch",            fmodgms2_eventInstance_getPitch,            1, 0);
    Function_Add("eventInstance_setPitch",            fmodgms2_eventInstance_setPitch,            2, 0);
    Function_Add("eventInstance_getPaused",           fmodgms2_eventInstance_getPaused,           1, 0);
    Function_Add("eventInstance_setPaused",           fmodgms2_eventInstance_setPaused,           2, 0);
    Function_Add("eventInstance_getTimelinePosition", fmodgms2_eventInstance_getTimelinePosition, 1, 0);
    Function_Add("eventInstance_setTimelinePosition", fmodgms2_eventInstance_setTimelinePosition, 2, 0);
    Function_Add("eventInstance_get3DAttributes",     fmodgms2_eventInstance_get3DAttributes,     1, 0);
    Function_Add("eventInstance_set3DAttributes",     fmodgms2_eventInstance_set3DAttributes,    13, 0);
    Function_Add("eventInstance_getParameterByName",  fmodgms2_eventInstance_getParameterByName,  2, 0);
    Function_Add("eventInstance_setParameterByName",  fmodgms2_eventInstance_setParameterByName,  4, 0);
    Function_Add("eventInstance_setParameterById",    fmodgms2_eventInstance_setParameterById,    5, 0);
    Function_Add("eventInstance_setCallback",         fmodgms2_eventInstance_setCallback,         2, 0);

    // commandreplay_*
    Function_Add("commandreplay_release",       fmodgms2_commandreplay_release,       1, 0);
    Function_Add("commandreplay_start",         fmodgms2_commandreplay_start,         1, 0);
    Function_Add("commandreplay_stop",          fmodgms2_commandreplay_stop,          1, 0);
    Function_Add("commandreplay_setPaused",     fmodgms2_commandreplay_setPaused,     2, 0);
    Function_Add("commandreplay_setBankPath",   fmodgms2_commandreplay_setBankPath,   2, 0);
    Function_Add("commandreplay_seekToCommand", fmodgms2_commandreplay_seekToCommand, 2, 0);
    Function_Add("commandreplay_seekToTime",    fmodgms2_commandreplay_seekToTime,    2, 0);

    return 0;
}
