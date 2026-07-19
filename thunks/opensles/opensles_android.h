// The Android OpenSL ES extensions the shim implements, declared from the ABI
// instead of vendored. Only what opensles.cpp binds is here; the core types
// still come from the Khronos headers.
//
// These carry ABI_ATTR where the originals do not: the guest calls in through
// these pointers, so armv7 hardfp needs the aapcs PCS on them.

#pragma once

#include "platform.h"
#include "jni.h"
#include "SLES/OpenSLES.h"

#define SL_ANDROID_PCM_REPRESENTATION_FLOAT ((SLuint32) 0x00000003)
#define SL_ANDROID_DATAFORMAT_PCM_EX        ((SLuint32) 0x00000004)

// SLDataFormat_PCM with a representation field appended; how Oboe asks for float.
typedef struct SLAndroidDataFormat_PCM_EX_ {
    SLuint32 formatType;
    SLuint32 numChannels;
    SLuint32 sampleRate;
    SLuint32 bitsPerSample;
    SLuint32 containerSize;
    SLuint32 channelMask;
    SLuint32 endianness;
    SLuint32 representation;
} SLAndroidDataFormat_PCM_EX;

// Accepted and ignored by the shim, but Oboe asks for the interface.
struct SLAndroidConfigurationItf_;
typedef const struct SLAndroidConfigurationItf_ * const * SLAndroidConfigurationItf;

struct SLAndroidConfigurationItf_ {
    SLresult (ABI_ATTR *SetConfiguration)(
        SLAndroidConfigurationItf self,
        const SLchar *configKey,
        const void *pConfigValue,
        SLuint32 valueSize
    );
    SLresult (ABI_ATTR *GetConfiguration)(
        SLAndroidConfigurationItf self,
        const SLchar *configKey,
        SLuint32 *pValueSize,
        void *pConfigValue
    );
    SLresult (ABI_ATTR *AcquireJavaProxy)(
        SLAndroidConfigurationItf self,
        SLuint32 proxyType,
        jobject *pProxyObj
    );
    SLresult (ABI_ATTR *ReleaseJavaProxy)(
        SLAndroidConfigurationItf self,
        SLuint32 proxyType
    );
};

// The interface Oboe feeds audio through.
struct SLAndroidSimpleBufferQueueItf_;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const * SLAndroidSimpleBufferQueueItf;

typedef void (ABI_ATTR *slAndroidSimpleBufferQueueCallback)(
    SLAndroidSimpleBufferQueueItf caller,
    void *pContext
);

typedef struct SLAndroidSimpleBufferQueueState_ {
    SLuint32 count;
    SLuint32 index;
} SLAndroidSimpleBufferQueueState;

struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (ABI_ATTR *Enqueue)(
        SLAndroidSimpleBufferQueueItf self,
        const void *pBuffer,
        SLuint32 size
    );
    SLresult (ABI_ATTR *Clear)(
        SLAndroidSimpleBufferQueueItf self
    );
    SLresult (ABI_ATTR *GetState)(
        SLAndroidSimpleBufferQueueItf self,
        SLAndroidSimpleBufferQueueState *pState
    );
    SLresult (ABI_ATTR *RegisterCallback)(
        SLAndroidSimpleBufferQueueItf self,
        slAndroidSimpleBufferQueueCallback callback,
        void *pContext
    );
};
