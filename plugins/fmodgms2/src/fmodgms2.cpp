// fmodgms2 bindings. Clean-room reimplementation of the public Windows
// extension's DLL exports against the documented FMOD Studio API.

#include "fmod.hpp"
#include "fmod_studio.hpp"
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>

#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"

namespace fs = std::filesystem;

static FMOD::System*         fmod_system = nullptr;
static FMOD::Studio::System* fmod_studio_system = nullptr;
static std::vector<FMOD::Studio::EventInstance *> event_instances = {};
static std::vector<FMOD::Studio::EventInstance *> event_instances_oneshot = {};
static std::unordered_map<std::string, FMOD::Studio::EventDescription *> event_desc_list = {};

extern "C" void FMOD_SDL_Register(FMOD_SYSTEM *system);
extern std::string g_bank_dir;
extern bool g_dbg;

// fmod_setDistanceScale stores here; positions are pre-multiplied before being
// handed to FMOD. FMOD's own distancefactor only scales Doppler, not rolloff.
static float g_distance_scale = 1.0f;

static const char *AccessYYString(const RValue *args, int index)
{
    if (args[index].kind == VALUE_STRING)
        return (const char *)args[index].rvalue.str->m_thing;
    return "";
}

static FMOD_3D_ATTRIBUTES read_3d_attrs(RValue *args, int base);

// Real-returning helpers use the upstream fmodgms2 convention: 0.0 == FMOD_OK.
static void fmod_update(RValue *ret, void *, void *, int, RValue *)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    if (fmod_studio_system->update() != FMOD_OK) return;

    size_t i = 0;
    while (i < event_instances_oneshot.size()) {
        auto it = event_instances_oneshot.begin() + i;
        FMOD_STUDIO_PLAYBACK_STATE state;
        if ((*it)->getPlaybackState(&state) == FMOD_OK && (state & FMOD_STUDIO_PLAYBACK_STOPPED)) {
            (*it)->release();
            event_instances_oneshot.erase(it);
            continue;
        }
        i++;
    }
    ret->rvalue.val = 0.0;
}

static void fmod_set_parameter(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    const char *name = AccessYYString(args, 0);
    float value = YYGetReal(args, 1);
    float ignoreseek = YYGetInt32(args, 2);
    ret->rvalue.val = (fmod_studio_system->setParameterByName(name, value, ignoreseek) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_get_parameter(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    const char *name = AccessYYString(args, 0);
    float value;
    if (fmod_studio_system->getParameterByName(name, &value) == FMOD_OK)
        ret->rvalue.val = value;
}

static void fmod_set_num_listeners(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    int n = YYGetInt32(args, 0);
    ret->rvalue.val = (fmod_studio_system->setNumListeners(n) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_set_listener_attributes(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    int idx = YYGetInt32(args, 0);
    float posX = YYGetReal(args, 1);
    float posY = YYGetReal(args, 2);
    FMOD_3D_ATTRIBUTES attr = { { posX, posY, 0.0f }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
    ret->rvalue.val = (fmod_studio_system->setListenerAttributes(idx, &attr) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_set_3d_attributes(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    float posX = YYGetReal(args, 1);
    float posY = YYGetReal(args, 2);
    FMOD_3D_ATTRIBUTES attr = { { posX, posY, 0.0f }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
    ret->rvalue.val = (inst->set3DAttributes(&attr) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_release(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    auto it = std::find(event_instances.begin(), event_instances.end(), inst);
    if (it != event_instances.end()) event_instances.erase(it);
    it = std::find(event_instances_oneshot.begin(), event_instances_oneshot.end(), inst);
    if (it != event_instances_oneshot.end()) event_instances_oneshot.erase(it);
    if (inst) ret->rvalue.val = (inst->release() == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_play(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) ret->rvalue.val = (inst->start() == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_stop(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    float immediate = YYGetReal(args, 1);
    FMOD_RESULT r = inst->stop((immediate != 0.0f) ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT);
    ret->rvalue.val = (r == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_get_paused(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    bool flag = false;
    inst->getPaused(&flag);
    ret->rvalue.val = flag ? 1.0 : 0.0;
}

static void fmod_event_instance_set_paused(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    double flag = YYGetReal(args, 1);
    ret->rvalue.val = (inst->setPaused(flag != 0.0) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_get_timeline_pos(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    int pos = 0;
    if (inst->getTimelinePosition(&pos) == FMOD_OK)
        ret->rvalue.val = static_cast<double>(pos);
}

static void fmod_event_instance_set_timeline_pos(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    int pos = static_cast<int>(YYGetInt32(args, 1));
    ret->rvalue.val = (inst->setTimelinePosition(pos) == FMOD_OK) ? 0.0 : 1.0;
}

static void fmod_event_instance_get_parameter(RValue *ret, void *, void *, int, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    const char *name = AccessYYString(args, 1);
    float value;
    if (inst->getParameterByName(name, &value) == FMOD_OK)
        ret->rvalue.val = value;
}

static void fmod_event_instance_set_parameter(RValue *ret, void *, void *, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst) return;
    const char *name = AccessYYString(args, 1);
    float value = YYGetReal(args, 2);
    bool ignoreseek = (argc >= 4) ? (YYGetReal(args, 3) != 0.0) : false;
    ret->rvalue.val = (inst->setParameterByName(name, value, ignoreseek) == FMOD_OK) ? 0.0 : 1.0;
}

// ---- fmod_* ----

ABI_ATTR void fmodgms2_fmod_final(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    if (fmod_studio_system)
    {
        fmod_studio_system->release();
        fmod_studio_system = nullptr;
        fmod_system = nullptr;
    }
    event_instances.clear();
    event_instances_oneshot.clear();
    event_desc_list.clear();
}

ABI_ATTR void fmodgms2_fmod_getApiVersion(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = 0;
    if (fmod_system)
    {
        unsigned int version = 0;
        if (fmod_system->getVersion(&version) == FMOD_OK)
            ret->rvalue.v64 = (long long)version;
    }
}

ABI_ATTR void fmodgms2_fmod_isLoaded(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = (fmod_studio_system != nullptr) ? 1 : 0;
}

ABI_ATTR void fmodgms2_fmod_releaseAllEventInstances(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    for (auto inst : event_instances) {
        if (inst) inst->release();
    }
    event_instances.clear();
    for (auto inst : event_instances_oneshot) {
        if (inst) inst->release();
    }
    event_instances_oneshot.clear();
    ret->rvalue.val = 0.0;
}

ABI_ATTR void fmodgms2_fmod_setDistanceScale(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_system) return;
    g_distance_scale = (float)YYGetReal(args, 0);
    if (g_dbg) warning("[fmodgms2] fmod_setDistanceScale(%g)\n", g_distance_scale);
    ret->rvalue.val = 0.0;
}

// ---- studioSystem_* ----

static void fmodgms2_autoload_banks()
{
    if (g_bank_dir.empty()) return;
    std::error_code ec;
    if (!fs::exists(g_bank_dir, ec)) {
        warning("[FMOD]: bank_dir '%s' missing, skipping auto-load.\n", g_bank_dir.c_str());
        return;
    }
    const std::string &scan_path = g_bank_dir;

    int ok = 0, fail = 0;
    auto load_one = [&](const std::string &path) {
        FMOD::Studio::Bank *bank = nullptr;
        FMOD_RESULT r = fmod_studio_system->loadBankFile(path.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
        if (r != FMOD_OK) { fail++; warning("[FMOD]: auto-load '%s' failed (%d).\n", path.c_str(), r); }
        else ok++;
    };

    // Split-bank projects need both <name>.bank and <name>.assets.bank loaded
    // or events resolve but play silence.
    std::vector<std::string> all_banks;
    for (auto &de : fs::directory_iterator(scan_path, ec)) {
        if (de.is_regular_file() && de.path().extension() == ".bank")
            all_banks.push_back(de.path().string());
    }

    auto load_match = [&](const std::string &needle) {
        for (auto it = all_banks.begin(); it != all_banks.end(); ) {
            if (it->find(needle) != std::string::npos) {
                load_one(*it);
                it = all_banks.erase(it);
            } else ++it;
        }
    };
    load_match("master.strings.bank");
    load_match("master.bank");
    for (auto &p : all_banks) load_one(p);
    warning("[fmodgms2] autoload from '%s': %d ok, %d failed.\n", scan_path.c_str(), ok, fail);
}

ABI_ATTR void fmodgms2_studioSystem_initialize(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT32;
    ret->rvalue.v32 = FMOD_OK;
    ret->flags = 0;

    if (fmod_system && fmod_studio_system)
        return;

    FMOD_RESULT res;
    if ((res = FMOD::Studio::System::create(&fmod_studio_system)) != FMOD_OK) {
        ret->rvalue.v32 = res; return;
    }
    if ((res = fmod_studio_system->getCoreSystem(&fmod_system)) != FMOD_OK) {
        ret->rvalue.v32 = res; return;
    }

    FMOD_SDL_Register((FMOD_SYSTEM *)fmod_system);
    if ((res = fmod_system->setSoftwareFormat(0, FMOD_SPEAKERMODE_STEREO, 0)) != FMOD_OK) {
        ret->rvalue.v32 = res; return;
    }
    if ((res = fmod_studio_system->initialize(256, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, 0)) != FMOD_OK) {
        ret->rvalue.v32 = res; return;
    }

    event_instances.reserve(2048);
    event_instances_oneshot.reserve(2048);
    event_desc_list.reserve(2048);

    fmodgms2_autoload_banks();
}

ABI_ATTR void fmodgms2_studioSystem_unloadAll(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    FMOD_RESULT r = fmod_studio_system->unloadAll();
    if (r == FMOD_OK) {
        event_instances.clear();
        event_instances_oneshot.clear();
        event_desc_list.clear();
        ret->rvalue.val = 0.0;
    }
}

ABI_ATTR void fmodgms2_studioSystem_update(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_update(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_studioSystem_flushCommands(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    ret->rvalue.val = (fmod_studio_system->flushCommands() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_flushSampleLoading(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    ret->rvalue.val = (fmod_studio_system->flushSampleLoading() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_setNumListeners(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_set_num_listeners(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_studioSystem_getNumListeners(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = 0;
    if (!fmod_studio_system) return;
    int n = 0;
    if (fmod_studio_system->getNumListeners(&n) == FMOD_OK)
        ret->rvalue.v64 = n;
}

ABI_ATTR void fmodgms2_studioSystem_setListener3DAttributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system || argc < 13) return;
    int idx = YYGetInt32(args, 0);
    FMOD_3D_ATTRIBUTES attr = read_3d_attrs(args, 1);

    // (0,0,0) attenuation means "caller didn't pass one" -- fall back to nullptr
    // so FMOD anchors attenuation at the listener instead of world origin.
    FMOD_VECTOR atten = {};
    const FMOD_VECTOR *atten_ptr = nullptr;
    if (argc >= 16) {
        float ax = (float)YYGetReal(args, 13);
        float ay = (float)YYGetReal(args, 14);
        float az = (float)YYGetReal(args, 15);
        if (ax != 0.0f || ay != 0.0f || az != 0.0f) {
            atten.x = ax * g_distance_scale;
            atten.y = ay * g_distance_scale;
            atten.z = az * g_distance_scale;
            atten_ptr = &atten;
        }
    }
    ret->rvalue.val = (fmod_studio_system->setListenerAttributes(idx, &attr, atten_ptr) == FMOD_OK) ? 0.0 : 1.0;
}

// Game-side json_parses this and reads .position.{x,y,z}, .forward, .up, .velocity.
static void attrs_to_json(const FMOD_3D_ATTRIBUTES &a, char *out, size_t cap)
{
    snprintf(out, cap,
        "{\"position\":{\"x\":%g,\"y\":%g,\"z\":%g},"
         "\"forward\":{\"x\":%g,\"y\":%g,\"z\":%g},"
         "\"up\":{\"x\":%g,\"y\":%g,\"z\":%g},"
         "\"velocity\":{\"x\":%g,\"y\":%g,\"z\":%g}}",
        a.position.x, a.position.y, a.position.z,
        a.forward.x,  a.forward.y,  a.forward.z,
        a.up.x,       a.up.y,       a.up.z,
        a.velocity.x, a.velocity.y, a.velocity.z);
}

ABI_ATTR void fmodgms2_studioSystem_getListener3DAttributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    if (!fmod_studio_system) { YYCreateString(ret, "{}"); return; }
    int idx = YYGetInt32(args, 0);
    FMOD_3D_ATTRIBUTES attrs = {};
    fmod_studio_system->getListenerAttributes(idx, &attrs, nullptr);
    char buf[320];
    attrs_to_json(attrs, buf, sizeof(buf));
    YYCreateString(ret, buf);
}

ABI_ATTR void fmodgms2_studioSystem_setListenerWeight(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    int idx = YYGetInt32(args, 0);
    float w = (float)YYGetReal(args, 1);
    ret->rvalue.val = (fmod_studio_system->setListenerWeight(idx, w) == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_getListenerWeight(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    if (!fmod_studio_system) return;
    int idx = YYGetInt32(args, 0);
    float w = 0.0f;
    if (fmod_studio_system->getListenerWeight(idx, &w) == FMOD_OK)
        ret->rvalue.val = w;
}

ABI_ATTR void fmodgms2_studioSystem_setParameterByName(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_set_parameter(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_studioSystem_getParameterByName(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_get_parameter(ret, self, other, argc, args);
}

// Caller json_parses this and reads decoded.parameterId.{result,data1,data2}.
// A flat {"data1":..} shape silently degrades to a (0,0) ID and no-ops every
// setParameterById call.
static void param_id_to_json(int result, const FMOD_STUDIO_PARAMETER_ID &id,
                             char *out, size_t cap)
{
    snprintf(out, cap, "{\"parameterId\":{\"result\":%d,\"data1\":%u,\"data2\":%u}}",
             result, id.data1, id.data2);
}
static bool json_to_param_id(const char *s, FMOD_STUDIO_PARAMETER_ID &out)
{
    if (!s) return false;
    unsigned int d1 = 0, d2 = 0;
    const char *p1 = strstr(s, "\"data1\"");
    const char *p2 = strstr(s, "\"data2\"");
    if (!p1 || !p2) return false;
    if (sscanf(p1, "\"data1\" : %u", &d1) != 1 && sscanf(p1, "\"data1\":%u", &d1) != 1) return false;
    if (sscanf(p2, "\"data2\" : %u", &d2) != 1 && sscanf(p2, "\"data2\":%u", &d2) != 1) return false;
    out.data1 = d1; out.data2 = d2;
    return true;
}

ABI_ATTR void fmodgms2_studioSystem_setParameterById(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system || argc < 4) return;
    FMOD_STUDIO_PARAMETER_ID id;
    id.data1 = (unsigned int)YYGetReal(args, 0);
    id.data2 = (unsigned int)YYGetReal(args, 1);
    float value = (float)YYGetReal(args, 2);
    bool ignoreseek = YYGetReal(args, 3) != 0.0;
    ret->rvalue.val = (fmod_studio_system->setParameterByID(id, value, ignoreseek) == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_getParameterId(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    if (!fmod_studio_system) { YYCreateString(ret, ""); return; }
    const char *name = AccessYYString(args, 0);
    FMOD_STUDIO_PARAMETER_DESCRIPTION desc = {};
    FMOD_RESULT r = fmod_studio_system->getParameterDescriptionByName(name, &desc);
    char buf[128];
    // Invert: caller treats result==1 as "found", but FMOD_OK is 0.
    param_id_to_json(r == FMOD_OK ? 1 : 0, desc.id, buf, sizeof(buf));
    YYCreateString(ret, buf);
}

ABI_ATTR void fmodgms2_studioSystem_startCommandCapture(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    int flags = (argc >= 2) ? YYGetInt32(args, 1) : FMOD_STUDIO_COMMANDCAPTURE_NORMAL;
    ret->rvalue.val = (fmod_studio_system->startCommandCapture(path, (FMOD_STUDIO_COMMANDCAPTURE_FLAGS)flags) == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_stopCommandCapture(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    if (!fmod_studio_system) return;
    ret->rvalue.val = (fmod_studio_system->stopCommandCapture() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_studioSystem_loadCommandReplay(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    int flags = (argc >= 2) ? YYGetInt32(args, 1) : FMOD_STUDIO_COMMANDREPLAY_NORMAL;
    FMOD::Studio::CommandReplay *cr = nullptr;
    if (fmod_studio_system->loadCommandReplay(path, (FMOD_STUDIO_COMMANDREPLAY_FLAGS)flags, &cr) == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)cr;
}

ABI_ATTR void fmodgms2_studioSystem_loadBankFile(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    int flags = (argc >= 2) ? YYGetInt32(args, 1) : FMOD_STUDIO_LOAD_BANK_NORMAL;
    FMOD::Studio::Bank *bank = nullptr;
    if (fmod_studio_system->loadBankFile(path, (FMOD_STUDIO_LOAD_BANK_FLAGS)flags, &bank) == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)bank;
}

ABI_ATTR void fmodgms2_studioSystem_getBank(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    FMOD::Studio::Bank *bank = nullptr;
    if (fmod_studio_system->getBank(path, &bank) == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)bank;
}

ABI_ATTR void fmodgms2_studioSystem_getBus(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    FMOD::Studio::Bus *bus = nullptr;
    if (fmod_studio_system->getBus(path, &bus) == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)bus;
}

ABI_ATTR void fmodgms2_studioSystem_getVCA(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    FMOD::Studio::VCA *vca = nullptr;
    if (fmod_studio_system->getVCA(path, &vca) == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)vca;
}

ABI_ATTR void fmodgms2_studioSystem_getEvent(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    if (!fmod_studio_system) return;
    const char *path = AccessYYString(args, 0);
    FMOD::Studio::EventDescription *desc = nullptr;
    FMOD_RESULT r = fmod_studio_system->getEvent(path, &desc);
    if (r == FMOD_OK)
        ret->rvalue.v64 = (long long)(uintptr_t)desc;
    if (g_dbg && r != FMOD_OK)
        warning("[fmodgms2] getEvent('%s') failed (%d)\n", path ? path : "(null)", (int)r);
}

// ---- bank_* ----

static inline void return_path_string(RValue *ret, FMOD_RESULT (*fn)(void *, char *, int, int *), void *obj)
{
    int retrieved = 0;
    FMOD_RESULT r = fn(obj, nullptr, 0, &retrieved);
    if ((r != FMOD_OK && r != FMOD_ERR_TRUNCATED) || retrieved <= 0) {
        YYCreateString(ret, "");
        return;
    }
    std::vector<char> buf(retrieved + 1);
    int got = 0;
    fn(obj, buf.data(), (int)buf.size(), &got);
    buf[retrieved] = '\0';
    YYCreateString(ret, buf.data());
}

ABI_ATTR void fmodgms2_bank_isValid(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    ret->rvalue.v64 = (bank && bank->isValid()) ? 1 : 0;
}

ABI_ATTR void fmodgms2_bank_unload(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (bank)
        ret->rvalue.val = (bank->unload() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_bank_loadSampleData(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (bank)
        ret->rvalue.val = (bank->loadSampleData() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_bank_unloadSampleData(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (bank)
        ret->rvalue.val = (bank->unloadSampleData() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_bank_getLoadingState(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)FMOD_STUDIO_LOADING_STATE_ERROR;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (bank) {
        FMOD_STUDIO_LOADING_STATE s = FMOD_STUDIO_LOADING_STATE_ERROR;
        bank->getLoadingState(&s);
        ret->rvalue.v64 = (long long)s;
    }
}

ABI_ATTR void fmodgms2_bank_getSampleLoadingState(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)FMOD_STUDIO_LOADING_STATE_ERROR;
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (bank) {
        FMOD_STUDIO_LOADING_STATE s = FMOD_STUDIO_LOADING_STATE_ERROR;
        bank->getSampleLoadingState(&s);
        ret->rvalue.v64 = (long long)s;
    }
}

ABI_ATTR void fmodgms2_bank_getPath(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *bank = (FMOD::Studio::Bank *)args[0].rvalue.v64;
    if (!bank) { YYCreateString(ret, ""); return; }
    int retrieved = 0;
    bank->getPath(nullptr, 0, &retrieved);
    if (retrieved <= 0) { YYCreateString(ret, ""); return; }
    std::vector<char> buf(retrieved + 1);
    bank->getPath(buf.data(), (int)buf.size(), &retrieved);
    buf[retrieved] = '\0';
    YYCreateString(ret, buf.data());
}

// ---- bus_* ----

ABI_ATTR void fmodgms2_bus_isValid(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    ret->rvalue.v64 = (bus && bus->isValid()) ? 1 : 0;
}

ABI_ATTR void fmodgms2_bus_getPath(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (!bus) { YYCreateString(ret, ""); return; }
    int retrieved = 0;
    bus->getPath(nullptr, 0, &retrieved);
    if (retrieved <= 0) { YYCreateString(ret, ""); return; }
    std::vector<char> buf(retrieved + 1);
    bus->getPath(buf.data(), (int)buf.size(), &retrieved);
    buf[retrieved] = '\0';
    YYCreateString(ret, buf.data());
}

ABI_ATTR void fmodgms2_bus_getVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        float v = 0.0f;
        if (bus->getVolume(&v) == FMOD_OK) ret->rvalue.val = v;
    }
}

ABI_ATTR void fmodgms2_bus_setVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        float v = (float)YYGetReal(args, 1);
        ret->rvalue.val = (bus->setVolume(v) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_bus_getMute(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        bool m = false;
        if (bus->getMute(&m) == FMOD_OK) ret->rvalue.v64 = m ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_bus_setMute(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        bool m = YYGetReal(args, 1) != 0.0;
        ret->rvalue.val = (bus->setMute(m) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_bus_getPaused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        bool p = false;
        if (bus->getPaused(&p) == FMOD_OK) ret->rvalue.v64 = p ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_bus_setPaused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        bool p = YYGetReal(args, 1) != 0.0;
        ret->rvalue.val = (bus->setPaused(p) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_bus_stopAllEvents(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *bus = (FMOD::Studio::Bus *)args[0].rvalue.v64;
    if (bus) {
        FMOD_STUDIO_STOP_MODE mode = FMOD_STUDIO_STOP_ALLOWFADEOUT;
        if (argc >= 2 && YYGetReal(args, 1) != 0.0)
            mode = FMOD_STUDIO_STOP_IMMEDIATE;
        ret->rvalue.val = (bus->stopAllEvents(mode) == FMOD_OK) ? 0.0 : 1.0;
    }
}

// ---- vca_* ----

ABI_ATTR void fmodgms2_vca_isValid(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    auto *vca = (FMOD::Studio::VCA *)args[0].rvalue.v64;
    ret->rvalue.v64 = (vca && vca->isValid()) ? 1 : 0;
}

ABI_ATTR void fmodgms2_vca_getPath(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *vca = (FMOD::Studio::VCA *)args[0].rvalue.v64;
    if (!vca) { YYCreateString(ret, ""); return; }
    int retrieved = 0;
    vca->getPath(nullptr, 0, &retrieved);
    if (retrieved <= 0) { YYCreateString(ret, ""); return; }
    std::vector<char> buf(retrieved + 1);
    vca->getPath(buf.data(), (int)buf.size(), &retrieved);
    buf[retrieved] = '\0';
    YYCreateString(ret, buf.data());
}

ABI_ATTR void fmodgms2_vca_getVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *vca = (FMOD::Studio::VCA *)args[0].rvalue.v64;
    if (vca) {
        float v = 0.0f;
        if (vca->getVolume(&v) == FMOD_OK) ret->rvalue.val = v;
    }
}

ABI_ATTR void fmodgms2_vca_setVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *vca = (FMOD::Studio::VCA *)args[0].rvalue.v64;
    if (vca) {
        float v = (float)YYGetReal(args, 1);
        ret->rvalue.val = (vca->setVolume(v) == FMOD_OK) ? 0.0 : 1.0;
    }
}

// ---- eventDescription_* ----

ABI_ATTR void fmodgms2_eventDescription_isValid(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    ret->rvalue.v64 = (desc && desc->isValid()) ? 1 : 0;
}

ABI_ATTR void fmodgms2_eventDescription_getPath(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (!desc) { YYCreateString(ret, ""); return; }
    int retrieved = 0;
    desc->getPath(nullptr, 0, &retrieved);
    if (retrieved <= 0) { YYCreateString(ret, ""); return; }
    std::vector<char> buf(retrieved + 1);
    desc->getPath(buf.data(), (int)buf.size(), &retrieved);
    buf[retrieved] = '\0';
    YYCreateString(ret, buf.data());
}

ABI_ATTR void fmodgms2_eventDescription_getLength(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        int len = 0;
        if (desc->getLength(&len) == FMOD_OK) ret->rvalue.val = (double)len;
    }
}

ABI_ATTR void fmodgms2_eventDescription_is3D(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        bool b = false;
        if (desc->is3D(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_eventDescription_isOneshot(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        bool b = false;
        if (desc->isOneshot(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_eventDescription_isSnapshot(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        bool b = false;
        if (desc->isSnapshot(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_eventDescription_isStream(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        bool b = false;
        if (desc->isStream(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_eventDescription_hasCue(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        bool b = false;
        // FMOD 2.02 renamed hasCue -> hasSustainPoint; try both via SDK guard.
#if FMOD_VERSION >= 0x00020200
        if (desc->hasSustainPoint(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
#else
        if (desc->hasCue(&b) == FMOD_OK) ret->rvalue.v64 = b ? 1 : 0;
#endif
    }
}

ABI_ATTR void fmodgms2_eventDescription_getParameterId(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (!desc) { YYCreateString(ret, ""); return; }
    const char *name = AccessYYString(args, 1);
    FMOD_STUDIO_PARAMETER_DESCRIPTION pd = {};
    FMOD_RESULT r = desc->getParameterDescriptionByName(name, &pd);
    char buf[128];
    param_id_to_json(r == FMOD_OK ? 1 : 0, pd.id, buf, sizeof(buf));
    YYCreateString(ret, buf);
}

ABI_ATTR void fmodgms2_eventDescription_createInstance(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        FMOD::Studio::EventInstance *inst = nullptr;
        if (desc->createInstance(&inst) == FMOD_OK && inst) {
            event_instances.push_back(inst);
            ret->rvalue.v64 = (long long)(uintptr_t)inst;
        }
    }
}

ABI_ATTR void fmodgms2_eventDescription_releaseAllInstances(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc)
        ret->rvalue.val = (desc->releaseAllInstances() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_eventDescription_loadSampleData(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc)
        ret->rvalue.val = (desc->loadSampleData() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_eventDescription_unloadSampleData(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc)
        ret->rvalue.val = (desc->unloadSampleData() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_eventDescription_getSampleLoadingState(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)FMOD_STUDIO_LOADING_STATE_ERROR;
    auto *desc = (FMOD::Studio::EventDescription *)args[0].rvalue.v64;
    if (desc) {
        FMOD_STUDIO_LOADING_STATE s = FMOD_STUDIO_LOADING_STATE_ERROR;
        desc->getSampleLoadingState(&s);
        ret->rvalue.v64 = (long long)s;
    }
}

// ---- eventInstance_* ----

ABI_ATTR void fmodgms2_eventInstance_isValid(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    ret->rvalue.v64 = (inst && inst->isValid()) ? 1 : 0;
}

ABI_ATTR void fmodgms2_eventInstance_isVirtual(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_BOOL;
    ret->rvalue.v64 = 0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        bool v = false;
        if (inst->isVirtual(&v) == FMOD_OK) ret->rvalue.v64 = v ? 1 : 0;
    }
}

ABI_ATTR void fmodgms2_eventInstance_release(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_release(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_start(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_play(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_stop(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_stop(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_triggerCue(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
#if FMOD_VERSION >= 0x00020200
        ret->rvalue.val = (inst->keyOff() == FMOD_OK) ? 0.0 : 1.0;
#else
        ret->rvalue.val = (inst->triggerCue() == FMOD_OK) ? 0.0 : 1.0;
#endif
    }
}

ABI_ATTR void fmodgms2_eventInstance_getPlaybackState(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)FMOD_STUDIO_PLAYBACK_STOPPED;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        FMOD_STUDIO_PLAYBACK_STATE s = FMOD_STUDIO_PLAYBACK_STOPPED;
        inst->getPlaybackState(&s);
        ret->rvalue.v64 = (long long)s;
    }
}

ABI_ATTR void fmodgms2_eventInstance_getDescription(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_INT64;
    ret->rvalue.v64 = (long long)(uintptr_t)nullptr;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        FMOD::Studio::EventDescription *desc = nullptr;
        if (inst->getDescription(&desc) == FMOD_OK)
            ret->rvalue.v64 = (long long)(uintptr_t)desc;
    }
}

ABI_ATTR void fmodgms2_eventInstance_getVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        float v = 0.0f;
        if (inst->getVolume(&v) == FMOD_OK) ret->rvalue.val = v;
    }
}

ABI_ATTR void fmodgms2_eventInstance_setVolume(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        float v = (float)YYGetReal(args, 1);
        ret->rvalue.val = (inst->setVolume(v) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_eventInstance_getPitch(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        float v = 0.0f;
        if (inst->getPitch(&v) == FMOD_OK) ret->rvalue.val = v;
    }
}

ABI_ATTR void fmodgms2_eventInstance_setPitch(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (inst) {
        float v = (float)YYGetReal(args, 1);
        ret->rvalue.val = (inst->setPitch(v) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_eventInstance_getPaused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_get_paused(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_setPaused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_set_paused(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_getTimelinePosition(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_get_timeline_pos(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_setTimelinePosition(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_set_timeline_pos(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_get3DAttributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    FMOD_3D_ATTRIBUTES a = {};
    if (inst) inst->get3DAttributes(&a);
    char buf[320];
    attrs_to_json(a, buf, sizeof(buf));
    YYCreateString(ret, buf);
}

// 12 scalars in fmodgms2 order (not FMOD-native): pos.xyz, fwd.xyz, up.xyz,
// vel.xyz. Positions are pre-multiplied by g_distance_scale.
static FMOD_3D_ATTRIBUTES read_3d_attrs(RValue *args, int base)
{
    FMOD_3D_ATTRIBUTES a = {};
    float s = g_distance_scale;
    a.position.x = (float)YYGetReal(args, base + 0) * s;
    a.position.y = (float)YYGetReal(args, base + 1) * s;
    a.position.z = (float)YYGetReal(args, base + 2) * s;
    a.forward.x  = (float)YYGetReal(args, base + 3);
    a.forward.y  = (float)YYGetReal(args, base + 4);
    a.forward.z  = (float)YYGetReal(args, base + 5);
    a.up.x       = (float)YYGetReal(args, base + 6);
    a.up.y       = (float)YYGetReal(args, base + 7);
    a.up.z       = (float)YYGetReal(args, base + 8);
    a.velocity.x = (float)YYGetReal(args, base + 9);
    a.velocity.y = (float)YYGetReal(args, base + 10);
    a.velocity.z = (float)YYGetReal(args, base + 11);
    return a;
}

ABI_ATTR void fmodgms2_eventInstance_set3DAttributes(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst || argc < 13) return;
    FMOD_3D_ATTRIBUTES attr = read_3d_attrs(args, 1);
    ret->rvalue.val = (inst->set3DAttributes(&attr) == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_eventInstance_getParameterByName(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_get_parameter(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_setParameterByName(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    fmod_event_instance_set_parameter(ret, self, other, argc, args);
}

ABI_ATTR void fmodgms2_eventInstance_setParameterById(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *inst = (FMOD::Studio::EventInstance *)args[0].rvalue.v64;
    if (!inst || argc < 5) return;
    FMOD_STUDIO_PARAMETER_ID id;
    id.data1 = (unsigned int)YYGetReal(args, 1);
    id.data2 = (unsigned int)YYGetReal(args, 2);
    float v = (float)YYGetReal(args, 3);
    bool ignoreseek = YYGetReal(args, 4) != 0.0;
    ret->rvalue.val = (inst->setParameterByID(id, v, ignoreseek) == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_eventInstance_setCallback(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    // Callbacks aren't bridged back into GML; accept and no-op.
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 0.0;
}

// ---- commandreplay_* ----

ABI_ATTR void fmodgms2_commandreplay_release(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) ret->rvalue.val = (cr->release() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_commandreplay_start(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) ret->rvalue.val = (cr->start() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_commandreplay_stop(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) ret->rvalue.val = (cr->stop() == FMOD_OK) ? 0.0 : 1.0;
}

ABI_ATTR void fmodgms2_commandreplay_setPaused(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) {
        bool p = YYGetReal(args, 1) != 0.0;
        ret->rvalue.val = (cr->setPaused(p) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_commandreplay_setBankPath(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) {
        const char *path = AccessYYString(args, 1);
        ret->rvalue.val = (cr->setBankPath(path) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_commandreplay_seekToCommand(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) {
        int idx = YYGetInt32(args, 1);
        ret->rvalue.val = (cr->seekToCommand(idx) == FMOD_OK) ? 0.0 : 1.0;
    }
}

ABI_ATTR void fmodgms2_commandreplay_seekToTime(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = 1.0;
    auto *cr = (FMOD::Studio::CommandReplay *)args[0].rvalue.v64;
    if (cr) {
        float t = (float)YYGetReal(args, 1);
        ret->rvalue.val = (cr->seekToTime(t) == FMOD_OK) ? 0.0 : 1.0;
    }
}
