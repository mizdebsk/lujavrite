/*-
 * Copyright (c) 2023-2025 Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <assert.h>
#include <dlfcn.h>

#include <jni.h>

#include <lauxlib.h>
#include <lua.h>

static JavaVM *JJ;
static __thread JNIEnv *J;
static __thread lua_State *LL;

static const char *
dlerror_safe(void)
{
    const char *error = dlerror();
    if (error) {
        return error;
    }
    return "unknown error";
}

/**
 * Determine whether an embedded Java Virtual Machine has already been
 * initialized.
 *
 * Returns:
 * - true if JVM has been initialized, false otherwise
 */
static int
initialized(lua_State *L)
{
    lua_pushboolean(L, J != NULL);
    return 1;
}

/**
 * Initialize Java Virtual Machine.
 *
 * dlopen() libjvm.so and call JNI_CreateJavaVM() with specified arguments.
 *
 * Parameters:
 * - path to libjvm.so, e.g., /usr/lib/jvm/java-17-openjdk/lib/server/libjvm.so
 * - zero or more arguments passed to JVM - e.g., -Djava.class.path=...
 *
 * Returns:
 * - nothing
 */
static int
init(lua_State *L)
{
    if (JJ != NULL) {
        luaL_error(L, "JVM has already been initialized");
    }
    const char *libjvm_path = luaL_checkstring(L, 1);
    void *libjvm = dlopen(libjvm_path, RTLD_LAZY);
    if (!libjvm) {
        luaL_error(L, "dlopen(libjvm.so) error: %s", dlerror_safe());
    }
    jint(JNICALL * JNI_CreateJavaVM)(JavaVM * *pvm, void **penv, void *args) =
        dlsym(libjvm, "JNI_CreateJavaVM");
    if (!JNI_CreateJavaVM) {
        luaL_error(L, "dlsym(JNI_CreateJavaVM) error: %s", dlerror_safe());
    }

    int n = lua_gettop(L) - 1;
    JavaVMOption jvmopt[n];
    for (int i = 0; i < n; i++) {
        jvmopt[i].optionString = (char *)luaL_checkstring(L, i + 2);
    }

    JavaVMInitArgs vmArgs;
    vmArgs.version = JNI_VERSION_1_8;
    vmArgs.nOptions = n;
    vmArgs.options = jvmopt;
    vmArgs.ignoreUnrecognized = JNI_FALSE;

    jint flag = JNI_CreateJavaVM(&JJ, (void **)&J, &vmArgs);
    if (flag != JNI_OK) {
        luaL_error(L, "failed to create JVM");
    }

    /* Hack: Now that JVM has been successfully created, let's ensure
       that lujavrite.so library is not unloaded as we would lose the
       reference to created JVM and wouldn't be able to interact with it
       in the future (after lujavrite.so is loaded again). */
    Dl_info dli;
    if (!dladdr(&JJ, &dli)) {
        luaL_error(L, "dladdr() failed: %s", dlerror_safe());
    }
    if (!dlopen(dli.dli_fname, RTLD_NOW)) {
        luaL_error(L, "dlopen(%s) error: %s", dli.dli_fname, dlerror_safe());
    }

    return 0;
}

/**
 * Call a static Java function that accepts zero or more String argument
 * and returns String.
 *
 * Parameters:
 * - class name, e.g. "com/mycompany/MyClass"
 * - method name, e.g., "myMethod"
 * - method signature, e.g., "(Ljava/lang/String;)Ljava/lang/String;"
 * - zero or more string arguments
 *
 * Returns:
 * - string return value of Java function
 */
static int
call(lua_State *L)
{
    if (JJ == NULL) {
        luaL_error(L, "JVM has not been initialized");
    }
    assert(JJ);
    if (J == NULL) {
        if ((*JJ)->GetEnv(JJ, (void **)&J, JNI_VERSION_1_8) != JNI_OK) {
            if ((*JJ)->AttachCurrentThread(JJ, (void **)&J, NULL) != JNI_OK) {
                luaL_error(L, "failed to attach current thread to JVM");
            }
        }
    }
    const char *class_name = luaL_checkstring(L, 1);
    const char *method_name = luaL_checkstring(L, 2);
    const char *method_signature = luaL_checkstring(L, 3);

    jclass jcls = (*J)->FindClass(J, class_name);
    if (jcls == NULL) {
        (*J)->ExceptionDescribe(J);
        luaL_error(L, "unable to find the Java class to call");
    }
    jmethodID methodId =
        (*J)->GetStaticMethodID(J, jcls, method_name, method_signature);
    if (methodId == NULL) {
        (*J)->ExceptionDescribe(J);
        luaL_error(L, "unable to find the Java method to call");
    }

    int n = lua_gettop(L) - 3;
    jvalue args[n];
    for (int i = 0; i < n; i++) {
        if (lua_isnil(L, i + 4)) {
            args[i].l = NULL;
        } else {
            args[i].l = (*J)->NewStringUTF(J, luaL_checkstring(L, i + 4));
        }
    }

    assert(LL == NULL);
    LL = L;

    jstring ret = (*J)->CallStaticObjectMethodA(J, jcls, methodId, args);

    LL = NULL;

    for (int i = 0; i < n; i++) {
        if (args[i].l != NULL) {
            (*J)->DeleteLocalRef(J, args[i].l);
        }
    }

    if ((*J)->ExceptionCheck(J)) {
        (*J)->ExceptionDescribe(J);
        luaL_error(L, "exception was thrown from called Java code");
    }

    if ((*J)->IsSameObject(J, ret, NULL)) {
        lua_pushnil(L);
    } else {
        const char *ret1 = (*J)->GetStringUTFChars(J, ret, NULL);
        lua_pushstring(L, ret1);
        (*J)->ReleaseStringUTFChars(J, ret, ret1);
    }

    return 1;
}

/**
 * Register lua module.
 * Called by Lua when loading the library.
 */
int
luaopen_lujavrite(lua_State *L)
{
    static const struct luaL_Reg functs[] = {
        {"initialized", initialized},
        {"init", init},
        {"call", call},
        {NULL, NULL},
    };

    lua_newtable(L);
    luaL_setfuncs(L, functs, 0);
    return 1;
}

/**
 * Ensures that the Lua state is present.
 *
 * Returns:
 * - JNI_OK if Lua state is present,
 * - otherwise JNI_ERR and throws java.lang.RuntimeException.
 */
static int
check_lua_state(JNIEnv *J)
{
    if (LL == NULL) {
        jclass class = (*J)->FindClass(J, "java/lang/RuntimeException");
        assert(class);
        (*J)->ThrowNew(
            J, class,
            "lujavrite: unable to call Lua from Java: Lua state is NULL");
        return JNI_ERR;
    }
    return JNI_OK;
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_getglobal(JNIEnv *J, jobject this, jstring name)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return 0;
    }
    const char *name1 = (*J)->GetStringUTFChars(J, name, NULL);
    jint ret = lua_getglobal(LL, name1);
    (*J)->ReleaseStringUTFChars(J, name, name1);
    return ret;
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_getfield(JNIEnv *J, jobject this, jint index,
                                     jstring name)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return 0;
    }
    const char *name1 = (*J)->GetStringUTFChars(J, name, NULL);
    jint ret = lua_getfield(LL, index, name1);
    (*J)->ReleaseStringUTFChars(J, name, name1);
    return ret;
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_pushstring(JNIEnv *J, jobject this, jstring string)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return;
    }
    const char *string1 = (*J)->GetStringUTFChars(J, string, NULL);
    (void)lua_pushstring(LL, string1);
    (*J)->ReleaseStringUTFChars(J, string, string1);
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_pcall(JNIEnv *J, jobject this, jint nargs,
                                  jint nresults, jint msgh)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return 0;
    }
    jint ret = lua_pcall(LL, nargs, nresults, msgh);
    return ret;
}

JNIEXPORT jstring JNICALL
Java_io_kojan_lujavrite_Lua_tostring(JNIEnv *J, jobject this, jint index)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return 0;
    }
    const char *val1 = lua_tostring(LL, index);
    jstring val = (*J)->NewStringUTF(J, val1);
    return val;
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_remove(JNIEnv *J, jobject this, jint index)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return;
    }
    lua_remove(LL, index);
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_pop(JNIEnv *J, jobject this, jint n)
{
    (void)this;
    if (check_lua_state(J) != JNI_OK) {
        return;
    }
    lua_pop(LL, n);
}

/* Local Variables: */
/* c-basic-offset: 4 */
/* indent-tabs-mode: nil */
/* End: */
