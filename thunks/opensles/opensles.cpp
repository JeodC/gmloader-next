#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "so_util.h"
#include "thunk_gen.h"

#define SL_API_DEPRECATED(level)
#include "SLES/OpenSLES.h"
#include "opensles/opensles_android.h"

#define DEFINE_IID(name) \
    static const struct SLInterfaceID_ name##_impl = { __LINE__ }; \
    extern "C" const SLInterfaceID name = &name##_impl;
DEFINE_IID(SL_IID_ENGINE)
DEFINE_IID(SL_IID_PLAY)
DEFINE_IID(SL_IID_VOLUME)
DEFINE_IID(SL_IID_BUFFERQUEUE)
DEFINE_IID(SL_IID_RECORD)
DEFINE_IID(SL_IID_ANDROIDSIMPLEBUFFERQUEUE)
DEFINE_IID(SL_IID_ANDROIDCONFIGURATION)

static const char *iid_name(SLInterfaceID iid)
{
    if (iid == SL_IID_ENGINE)                   return "SL_IID_ENGINE";
    if (iid == SL_IID_PLAY)                     return "SL_IID_PLAY";
    if (iid == SL_IID_VOLUME)                   return "SL_IID_VOLUME";
    if (iid == SL_IID_BUFFERQUEUE)              return "SL_IID_BUFFERQUEUE";
    if (iid == SL_IID_RECORD)                   return "SL_IID_RECORD";
    if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) return "SL_IID_ANDROIDSIMPLEBUFFERQUEUE";
    if (iid == SL_IID_ANDROIDCONFIGURATION)     return "SL_IID_ANDROIDCONFIGURATION";
    return "unknown";
}

#define ITF_OBJ(itf, type, member) ((type *)((char *)(itf) - offsetof(type, member)))
#define QUEUE_DEPTH 16   // buffer-queue depth; Oboe primes with two

enum sl_kind { KIND_ENGINE, KIND_OUTPUTMIX, KIND_PLAYER };

struct sl_object {
    const struct SLObjectItf_ *itf;
    sl_kind kind;
};

struct sl_engine {
    sl_object obj;
    const struct SLEngineItf_ *engine_itf;
};

struct sl_outputmix {
    sl_object obj;
};

struct sl_buffer {
    const SLuint8 *data;
    SLuint32 size;
    SLuint32 pos;
};

struct sl_player {
    sl_object obj;
    const struct SLPlayItf_ *play_itf;
    const struct SLAndroidSimpleBufferQueueItf_ *bq_itf;
    const struct SLVolumeItf_ *volume_itf;
    const struct SLAndroidConfigurationItf_ *config_itf;

    SDL_AudioDeviceID device;
    SDL_mutex *lock;

    sl_buffer ring[QUEUE_DEPTH];
    int head, count;

    slAndroidSimpleBufferQueueCallback callback;
    void *context;
};

// SDL wants a full period per call, and Oboe hands us small burst-sized buffers.
// Drain the queue and, whenever we run dry mid-period, call back into Oboe for
// the next burst.
static void SDLCALL player_feed(void *userdata, Uint8 *stream, int len)
{
    sl_player *player = (sl_player *)userdata;
    int filled = 0;

    while (filled < len) {
        SDL_LockMutex(player->lock);
        while (filled < len && player->count > 0) {
            sl_buffer *buffer = &player->ring[player->head];
            SLuint32 chunk = buffer->size - buffer->pos;
            if ((int)chunk > len - filled)
                chunk = len - filled;
            memcpy(stream + filled, buffer->data + buffer->pos, chunk);
            buffer->pos += chunk;
            filled += chunk;
            if (buffer->pos == buffer->size) {
                player->head = (player->head + 1) % QUEUE_DEPTH;
                player->count--;
            }
        }
        int remaining = player->count;
        SDL_UnlockMutex(player->lock);

        if (filled >= len || !player->callback)
            break;

        player->callback(&player->bq_itf, player->context);

        SDL_LockMutex(player->lock);
        bool produced = player->count > remaining;
        SDL_UnlockMutex(player->lock);
        if (!produced)
            break;
    }

    if (filled < len)
        memset(stream + filled, 0, len - filled);
}

static ABI_ATTR SLresult bq_Enqueue(SLAndroidSimpleBufferQueueItf self, const void *buffer, SLuint32 size)
{
    sl_player *player = ITF_OBJ(self, sl_player, bq_itf);
    SLresult result = SL_RESULT_BUFFER_INSUFFICIENT;

    SDL_LockMutex(player->lock);
    if (player->count < QUEUE_DEPTH) {
        player->ring[(player->head + player->count) % QUEUE_DEPTH] = { (const SLuint8 *)buffer, size, 0 };
        player->count++;
        result = SL_RESULT_SUCCESS;
    }
    SDL_UnlockMutex(player->lock);
    return result;
}

static ABI_ATTR SLresult bq_Clear(SLAndroidSimpleBufferQueueItf self)
{
    sl_player *player = ITF_OBJ(self, sl_player, bq_itf);
    SDL_LockMutex(player->lock);
    player->head = player->count = 0;
    SDL_UnlockMutex(player->lock);
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult bq_GetState(SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState *state)
{
    sl_player *player = ITF_OBJ(self, sl_player, bq_itf);
    if (state) {
        state->count = player->count;
        state->index = 0;
    }
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult bq_RegisterCallback(SLAndroidSimpleBufferQueueItf self,
                                    slAndroidSimpleBufferQueueCallback callback, void *context)
{
    sl_player *player = ITF_OBJ(self, sl_player, bq_itf);
    player->callback = callback;
    player->context = context;
    return SL_RESULT_SUCCESS;
}

static const struct SLAndroidSimpleBufferQueueItf_ bq_vtable = {
    bq_Enqueue, bq_Clear, bq_GetState, bq_RegisterCallback,
};

static ABI_ATTR SLresult play_SetPlayState(SLPlayItf self, SLuint32 state)
{
    sl_player *player = ITF_OBJ(self, sl_player, play_itf);
    if (player->device)
        SDL_PauseAudioDevice(player->device, state == SL_PLAYSTATE_PLAYING ? 0 : 1);
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult play_GetPlayState(SLPlayItf self, SLuint32 *state)
{
    sl_player *player = ITF_OBJ(self, sl_player, play_itf);
    if (state)
        *state = (player->device && SDL_GetAudioDeviceStatus(player->device) == SDL_AUDIO_PLAYING)
                     ? SL_PLAYSTATE_PLAYING : SL_PLAYSTATE_STOPPED;
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult play_ok(SLPlayItf) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult play_ok_u32(SLPlayItf, SLuint32) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult play_ok_millisec(SLPlayItf, SLmillisecond) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult play_ok_cb(SLPlayItf, slPlayCallback, void *) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult play_get_ms(SLPlayItf, SLmillisecond *out) { if (out) *out = SL_TIME_UNKNOWN; return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult play_get_u32(SLPlayItf, SLuint32 *out) { if (out) *out = 0; return SL_RESULT_SUCCESS; }

static const struct SLPlayItf_ play_vtable = {
    play_SetPlayState, play_GetPlayState, play_get_ms, play_get_ms, play_ok_cb,
    play_ok_u32, play_get_u32, play_ok_millisec, play_ok, play_get_ms,
    play_ok_u32, play_get_u32,
};

// Volume and configuration are accepted and ignored; GameMaker sets levels itself.
static ABI_ATTR SLresult vol_ok_s16(SLVolumeItf, SLmillibel) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult vol_get_s16(SLVolumeItf, SLmillibel *out) { if (out) *out = 0; return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult vol_ok_bool(SLVolumeItf, SLboolean) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult vol_get_bool(SLVolumeItf, SLboolean *out) { if (out) *out = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult vol_ok_perm(SLVolumeItf, SLpermille) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult vol_get_perm(SLVolumeItf, SLpermille *out) { if (out) *out = 0; return SL_RESULT_SUCCESS; }

static const struct SLVolumeItf_ volume_vtable = {
    vol_ok_s16, vol_get_s16, vol_get_s16, vol_ok_bool, vol_get_bool,
    vol_ok_bool, vol_get_bool, vol_ok_perm, vol_get_perm,
};

static ABI_ATTR SLresult cfg_SetConfiguration(SLAndroidConfigurationItf, const SLchar *, const void *, SLuint32)
{
    return SL_RESULT_SUCCESS;
}
static ABI_ATTR SLresult cfg_GetConfiguration(SLAndroidConfigurationItf, const SLchar *, SLuint32 *, void *)
{
    return SL_RESULT_SUCCESS;
}
static ABI_ATTR SLresult cfg_AcquireJavaProxy(SLAndroidConfigurationItf, SLuint32, jobject *)
{
    return SL_RESULT_FEATURE_UNSUPPORTED;
}
static ABI_ATTR SLresult cfg_ReleaseJavaProxy(SLAndroidConfigurationItf, SLuint32)
{
    return SL_RESULT_SUCCESS;
}

static const struct SLAndroidConfigurationItf_ config_vtable = {
    cfg_SetConfiguration, cfg_GetConfiguration, cfg_AcquireJavaProxy, cfg_ReleaseJavaProxy,
};

// The object interface is shared by the engine, output mix and player.
static ABI_ATTR SLresult obj_Realize(SLObjectItf self, SLboolean)
{
    (void)self;
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult obj_GetState(SLObjectItf, SLuint32 *state)
{
    if (state)
        *state = SL_OBJECT_STATE_REALIZED;
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult obj_GetInterface(SLObjectItf self, const SLInterfaceID iid, void *pInterface)
{
    sl_object *object = (sl_object *)self;
    if (!pInterface)
        return SL_RESULT_PARAMETER_INVALID;

    if (object->kind == KIND_ENGINE && iid == SL_IID_ENGINE) {
        *(void **)pInterface = &((sl_engine *)object)->engine_itf;
        return SL_RESULT_SUCCESS;
    }
    if (object->kind == KIND_PLAYER) {
        sl_player *player = (sl_player *)object;
        if (iid == SL_IID_PLAY)                     { *(void **)pInterface = &player->play_itf;   return SL_RESULT_SUCCESS; }
        if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE ||
            iid == SL_IID_BUFFERQUEUE)              { *(void **)pInterface = &player->bq_itf;     return SL_RESULT_SUCCESS; }
        if (iid == SL_IID_VOLUME)                   { *(void **)pInterface = &player->volume_itf; return SL_RESULT_SUCCESS; }
        if (iid == SL_IID_ANDROIDCONFIGURATION)     { *(void **)pInterface = &player->config_itf; return SL_RESULT_SUCCESS; }
    }
    warning("opensles: GetInterface(%s) unsupported on object kind %d\n", iid_name(iid), object->kind);
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

static ABI_ATTR void obj_Destroy(SLObjectItf self)
{
    sl_object *object = (sl_object *)self;
    if (object->kind == KIND_PLAYER) {
        sl_player *player = (sl_player *)object;
        if (player->device)
            SDL_CloseAudioDevice(player->device);
        if (player->lock)
            SDL_DestroyMutex(player->lock);
    }
    free(object);
}

static ABI_ATTR SLresult obj_ok_bool(SLObjectItf, SLboolean) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult obj_ok_cb(SLObjectItf, slObjectCallback, void *) { return SL_RESULT_SUCCESS; }
static ABI_ATTR void obj_void(SLObjectItf) {}
static ABI_ATTR SLresult obj_ok_prio(SLObjectItf, SLint32, SLboolean) { return SL_RESULT_SUCCESS; }
static ABI_ATTR SLresult obj_get_prio(SLObjectItf, SLint32 *out_priority, SLboolean *out_preemptable)
{
    if (out_priority) *out_priority = 0;
    if (out_preemptable) *out_preemptable = SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}
static ABI_ATTR SLresult obj_ok_ids(SLObjectItf, SLint16, SLInterfaceID *, SLboolean) { return SL_RESULT_SUCCESS; }

static const struct SLObjectItf_ object_vtable = {
    obj_Realize, obj_ok_bool, obj_GetState, obj_GetInterface, obj_ok_cb,
    obj_void, obj_Destroy, obj_ok_prio, obj_get_prio, obj_ok_ids,
};

static ABI_ATTR SLresult eng_CreateOutputMix(SLEngineItf, SLObjectItf *pMix, SLuint32,
                                    const SLInterfaceID *, const SLboolean *)
{
    sl_outputmix *mix = (sl_outputmix *)calloc(1, sizeof(*mix));
    if (!mix)
        return SL_RESULT_MEMORY_FAILURE;
    mix->obj.itf = &object_vtable;
    mix->obj.kind = KIND_OUTPUTMIX;
    *pMix = (SLObjectItf)&mix->obj.itf;
    return SL_RESULT_SUCCESS;
}

static ABI_ATTR SLresult eng_CreateAudioPlayer(SLEngineItf, SLObjectItf *pPlayer, SLDataSource *source,
                                      SLDataSink *, SLuint32, const SLInterfaceID *, const SLboolean *)
{
    SLDataFormat_PCM *format = source ? (SLDataFormat_PCM *)source->pFormat : NULL;
    if (!format)
        return SL_RESULT_PARAMETER_INVALID;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = format->samplesPerSec / 1000;
    want.channels = format->numChannels;
    want.samples = 1024;

    if (format->formatType == SL_ANDROID_DATAFORMAT_PCM_EX &&
        ((SLAndroidDataFormat_PCM_EX *)format)->representation == SL_ANDROID_PCM_REPRESENTATION_FLOAT)
        want.format = AUDIO_F32SYS;
    else
        want.format = format->bitsPerSample == 8 ? AUDIO_U8 : AUDIO_S16SYS;

    sl_player *player = (sl_player *)calloc(1, sizeof(*player));
    if (!player)
        return SL_RESULT_MEMORY_FAILURE;
    player->obj.itf = &object_vtable;
    player->obj.kind = KIND_PLAYER;
    player->play_itf = &play_vtable;
    player->bq_itf = &bq_vtable;
    player->volume_itf = &volume_vtable;
    player->config_itf = &config_vtable;
    player->lock = SDL_CreateMutex();

    want.callback = player_feed;
    want.userdata = player;
    player->device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!player->device) {
        warning("opensles: SDL_OpenAudioDevice failed (%d Hz, %d ch): %s\n",
                want.freq, want.channels, SDL_GetError());
        SDL_DestroyMutex(player->lock);
        free(player);
        return SL_RESULT_RESOURCE_ERROR;
    }

    *pPlayer = (SLObjectItf)&player->obj.itf;
    return SL_RESULT_SUCCESS;
}

// Oboe only creates an output mix and an audio player; the rest stays null.
static const struct SLEngineItf_ engine_vtable = {
    .CreateAudioPlayer = eng_CreateAudioPlayer,
    .CreateOutputMix = eng_CreateOutputMix,
};

extern "C" ABI_ATTR SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32, const SLEngineOption *,
                                   SLuint32, const SLInterfaceID *, const SLboolean *)
{
    if (!pEngine)
        return SL_RESULT_PARAMETER_INVALID;

    sl_engine *engine = (sl_engine *)calloc(1, sizeof(*engine));
    if (!engine)
        return SL_RESULT_MEMORY_FAILURE;
    engine->obj.itf = &object_vtable;
    engine->obj.kind = KIND_ENGINE;
    engine->engine_itf = &engine_vtable;
    *pEngine = (SLObjectItf)&engine->obj.itf;
    warning("opensles: engine created (SDL audio backend)\n");
    return SL_RESULT_SUCCESS;
}

DynLibFunction symtable_opensles[] = {
    NO_THUNK("slCreateEngine", (uintptr_t)&slCreateEngine),
    NO_THUNK("SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE),
    NO_THUNK("SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY),
    NO_THUNK("SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME),
    NO_THUNK("SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE),
    NO_THUNK("SL_IID_RECORD", (uintptr_t)&SL_IID_RECORD),
    NO_THUNK("SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE),
    NO_THUNK("SL_IID_ANDROIDCONFIGURATION", (uintptr_t)&SL_IID_ANDROIDCONFIGURATION),
    NULL,
};
