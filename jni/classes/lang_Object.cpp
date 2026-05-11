#include "jni.h"
#include "jni_internals.h"

static const Class lang_Object_clazz = {
    .classpath = "java/lang/Object",
    .classname = "lang_Object",
    .managed_methods = {NULL},
    .fields = {NULL},
    .instance_size = sizeof(void*),
};

static const int registered = ClassRegistry::register_class(lang_Object_clazz);
