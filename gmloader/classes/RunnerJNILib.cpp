#include <stdio.h>
#include <vector>

#include "so_util.h"
#include "jni.h"
#include "jni_internals.h"
#include "RunnerJNILib.h"
#include "libyoyo.h"
#include "jni/classes/bytebuffer.h"
#include "plugin_loader.h"

#define MANGLED_CLASSPATH "Java_com_yoyogames_runner_RunnerJNILib_"
#define CLASS RunnerJNILib

/*
    Native Function Pointers
*/

decltype(RunnerJNILib::Startup)                  RunnerJNILib::Startup = NULL;
decltype(RunnerJNILib::CreateVersionDSMap)       RunnerJNILib::CreateVersionDSMap = NULL;
decltype(RunnerJNILib::Process)                  RunnerJNILib::Process = NULL;
decltype(RunnerJNILib::TouchEvent)               RunnerJNILib::TouchEvent = NULL;
decltype(RunnerJNILib::RenderSplash)             RunnerJNILib::RenderSplash = NULL;
decltype(RunnerJNILib::Resume)                   RunnerJNILib::Resume = NULL;
decltype(RunnerJNILib::Pause)                    RunnerJNILib::Pause = NULL;
decltype(RunnerJNILib::KeyEvent)                 RunnerJNILib::KeyEvent = NULL;
decltype(RunnerJNILib::SetKeyValue)              RunnerJNILib::SetKeyValue = NULL;
decltype(RunnerJNILib::GetAppID)                 RunnerJNILib::GetAppID = NULL;
decltype(RunnerJNILib::GetSaveFileName)          RunnerJNILib::GetSaveFileName = NULL;
decltype(RunnerJNILib::ExpandCompressedFile)     RunnerJNILib::ExpandCompressedFile = NULL;
decltype(RunnerJNILib::iCadeEventDispatch)       RunnerJNILib::iCadeEventDispatch = NULL;
decltype(RunnerJNILib::registerGamepadConnected) RunnerJNILib::registerGamepadConnected = NULL;
decltype(RunnerJNILib::initGLFuncs)              RunnerJNILib::initGLFuncs = NULL;
decltype(RunnerJNILib::canFlip)                  RunnerJNILib::canFlip = NULL;
decltype(RunnerJNILib::GCMPushResult)            RunnerJNILib::GCMPushResult = NULL;

#define NATIVE_METHOD(so, sym) {&CLASS::clazz, MANGLED_CLASSPATH #sym, so, (void**)&CLASS::sym}
const NativeMethod RunnerJNILibNativeMethods[] = {
    NATIVE_METHOD("libyoyo.so", Startup),
    NATIVE_METHOD("libyoyo.so", CreateVersionDSMap),
    NATIVE_METHOD("libyoyo.so", Process),
    NATIVE_METHOD("libyoyo.so", TouchEvent),
    NATIVE_METHOD("libyoyo.so", RenderSplash),
    NATIVE_METHOD("libyoyo.so", Resume),
    NATIVE_METHOD("libyoyo.so", Pause),
    NATIVE_METHOD("libyoyo.so", KeyEvent),
    NATIVE_METHOD("libyoyo.so", SetKeyValue),
    NATIVE_METHOD("libyoyo.so", GetAppID),
    NATIVE_METHOD("libyoyo.so", GetSaveFileName),
    NATIVE_METHOD("libyoyo.so", ExpandCompressedFile),
    NATIVE_METHOD("libyoyo.so", iCadeEventDispatch),
    NATIVE_METHOD("libyoyo.so", registerGamepadConnected),
    NATIVE_METHOD("libyoyo.so", initGLFuncs),
    NATIVE_METHOD("libyoyo.so", canFlip),
    NATIVE_METHOD("libyoyo.so", GCMPushResult),
    {NULL}
};

/*
    Static Class Members
*/

double RunnerJNILib::mGameSpeedControl = 60.0;

const FieldId RunnerJNILibFields[] = {
    REGISTER_STATIC_FIELD(RunnerJNILib, mGameSpeedControl),
    {NULL},
};

/*
    Managed Class Methods
*/

jfloatArray RunnerJNILib::GamepadAxesValues(JNIEnv *env, jclass clz, void *ins, jint deviceIndex, jstring test)
{
    return 0;
}

extern int RunnerJNILib_MoveTaskToBackCalled;
void RunnerJNILib::MoveTaskToBack(JNIEnv *env, jclass clz)
{
    RunnerJNILib_MoveTaskToBackCalled = 1;
    warning("MoveTaskToBack called.\n");
}

int RunnerJNILib::OsGetInfo(JNIEnv *env, jclass clz)
{
    static String osinfo_arr[] = {
        /* "RELEASE", */       String("v1.0"),
        /* "MODEL", */         String("Homebrew"),
        /* "DEVICE", */        String("Homebrew"),
        /* "MANUFACTURER", */  String("JohnnyonFlame"),
        /* "CPU_ABI", */       String("armeabi-v7a"),
        /* "CPU_ABI2", */      String("arm64-v8a"),
        /* "BOOTLOADER", */    String("U-Boot"),
        /* "BOARD", */         String("You tell me"),
        /* "VERSION", */       String("v1.0"),
        /* "REGION", */        String("Global"),
        /* "VERSION_NAME", */  String("v1.0"),
    };

    int osinfo = RunnerJNILib::CreateVersionDSMap(env, NULL, 0x13,
        &osinfo_arr[0], &osinfo_arr[1], &osinfo_arr[2], &osinfo_arr[3], &osinfo_arr[4], &osinfo_arr[5],
        &osinfo_arr[6], &osinfo_arr[7], &osinfo_arr[8], &osinfo_arr[9], &osinfo_arr[10], (jboolean)1);
    warning(" -- Retuning OsInfo %d. --\n", osinfo);
    return osinfo;
}

// Video JNI methods dispatch to a plugin-registered backend if one is
// present; otherwise they're no-ops (or return 0 for query methods).
void RunnerJNILib::VideoOpen(JNIEnv *env, jclass clz, jstring path)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->open) vb->open(env->GetStringUTFChars(path, NULL));
}

void RunnerJNILib::VideoClose(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->close) vb->close();
}

jboolean RunnerJNILib::VideoDraw(JNIEnv *env, jclass clz, jobject bytebuffer)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (!vb || !vb->draw) return 0;
    ByteBuffer *buf = (ByteBuffer *)bytebuffer;
    return vb->draw(buf->address());
}

void RunnerJNILib::VideoSetVolume(JNIEnv *env, jclass clz, jdouble volume)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->set_volume) vb->set_volume(volume);
}

void RunnerJNILib::VideoSeekTo(JNIEnv *env, jclass clz, jdouble time)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->seek_to) vb->seek_to(time);
}

void RunnerJNILib::VideoEnableLoop(JNIEnv *env, jclass clz, jdouble loop)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->enable_loop) vb->enable_loop(loop);
}

void RunnerJNILib::VideoPause(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->pause) vb->pause();
}

void RunnerJNILib::VideoResume(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    if (vb && vb->resume) vb->resume();
}

jdouble RunnerJNILib::VideoStatus(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->status) ? vb->status() : 0.0;
}

jdouble RunnerJNILib::VideoGetStatus(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_status) ? vb->get_status() : 0.0;
}

jdouble RunnerJNILib::VideoGetFormat(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_format) ? vb->get_format() : 0.0;
}

jdouble RunnerJNILib::VideoW(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_width) ? vb->get_width() : 0.0;
}

jdouble RunnerJNILib::VideoH(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_height) ? vb->get_height() : 0.0;
}

jdouble RunnerJNILib::VideoGetDuration(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_duration) ? vb->get_duration() : 0.0;
}

jdouble RunnerJNILib::VideoGetPosition(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_position) ? vb->get_position() : 0.0;
}

jdouble RunnerJNILib::VideoGetVolume(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->get_volume) ? vb->get_volume() : 0.0;
}

jdouble RunnerJNILib::VideoIsLooping(JNIEnv *env, jclass clz)
{
    const gml_video_backend_t *vb = get_video_backend();
    return (vb && vb->is_looping) ? vb->is_looping() : 0.0;
}

const ManagedMethod RunnerJNILibManagedMethods[] = {
    REGISTER_STATIC_METHOD(RunnerJNILib, OsGetInfo, "()I"),
    REGISTER_STATIC_METHOD(RunnerJNILib, GamepadAxesValues, "(I)[F"),
    REGISTER_STATIC_METHOD(RunnerJNILib, MoveTaskToBack, "()V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoOpen, "(Ljava/lang/String;)V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoClose, "()V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoDraw, "(Ljava/nio/ByteBuffer;)Z"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoSetVolume, "(D)V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoEnableLoop, "(D)V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoSeekTo, "(D)V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoPause, "()V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoResume, "()V"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoStatus, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoGetFormat, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoW, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoH, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoGetDuration, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoGetPosition, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoGetStatus, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoIsLooping, "()D"),
    REGISTER_STATIC_METHOD(RunnerJNILib, VideoGetVolume, "()D"),
    NULL
};

Class RunnerJNILib::clazz = {
    .classpath = "com/yoyogames/runner/RunnerJNILib",
    .classname = "RunnerJNILib",
    .managed_methods = RunnerJNILibManagedMethods,
    .native_methods = RunnerJNILibNativeMethods,
    .fields = RunnerJNILibFields,
    .instance_size = sizeof(String)
};

static const int registered = ClassRegistry::register_class(RunnerJNILib::clazz);