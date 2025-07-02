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
#include <stdio.h>
#include <stdlib.h>
#define __USE_GNU 1
#include <dlfcn.h>

#include <jni.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

static JNIEnv *J;
static __thread lua_State *LL;

/**
 * Determine whether embedded Java Virtual Machine has already been initialized.
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
 * - path to libjvm.so, eg. /usr/lib/jvm/java-17-openjdk/lib/server/libjvm.so
 * - zero or more arguments passed to JVM - eg. -Djava.class.path=...
 *
 * Returns:
 * - nothing
 */
static int
init(lua_State *L)
{
  if (J != NULL) {
    fprintf(stderr, "lujavrite: error: JVM has already been initialized\n");
    exit(66);
  }
  const char *libjvm_path = luaL_checkstring(L, 1);
  void *libjvm = dlopen(libjvm_path, RTLD_LAZY);
  if (!libjvm) {
    fprintf(stderr, "lujavrite: dlopen(libjvm.so) error: %s\n", dlerror());
    exit(66);
  }
  jint (JNICALL *JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args)
    = dlsym(libjvm, "JNI_CreateJavaVM");
  if (!JNI_CreateJavaVM) {
    fprintf(stderr, "lujavrite: dlsym(JNI_CreateJavaVM) error: %s\n", dlerror());
    exit(66);
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

  JavaVM *javaVM;
  jint flag = JNI_CreateJavaVM(&javaVM, (void **)&J, &vmArgs);
  if (flag != JNI_OK) {
    fprintf(stderr, "lujavrite: error: failed to create JVM\n");
    exit(66);
  }

  /* Hack: Now that JVM has been successfully created, lets ensure
     that lujavrite.so library is not unloaded as we would loose the
     reference to created JVM and wouldn't be able to interact with it
     in the future (after lujavrite.so is loaded again). */
  Dl_info dli;
  if (!dladdr(&J, &dli)) {
    fprintf(stderr, "lujavrite: dladdr() failed");
    exit(66);
  }
  if (!dlopen(dli.dli_fname, RTLD_NOW)) {
    fprintf(stderr, "lujavrite: dlopen(%s) error: %s\n", dli.dli_fname, dlerror());
    exit(66);
  }

  return 0;
}


/**
 * Call static Java function that accepts zero or more String argument
 * and returns String.
 *
 * Parameters:
 * - class name, eg. "com/mycompany/MyClass"
 * - method name, eg. "myMethod"
 * - method signature, eg. "(Ljava/lang/String;)Ljava/lang/String;"
 * - zero or more string arguments
 *
 * Returns:
 * - string return value of Java function
 */
static int
call(lua_State *L)
{
  if (J == NULL) {
    fprintf(stderr, "lujavrite: error: JVM has not been initialized\n");
    exit(66);
  }
  const char *class_name = luaL_checkstring(L, 1);
  const char *method_name = luaL_checkstring(L, 2);
  const char *method_signature = luaL_checkstring(L, 3);

  jclass jcls = (*J)->FindClass(J, class_name);
  if (jcls == NULL) {
    (*J)->ExceptionDescribe(J);
    exit(66);
  }
  jmethodID methodId = (*J)->GetStaticMethodID(J, jcls, method_name, method_signature);
  if (methodId == NULL) {
    (*J)->ExceptionDescribe(J);
    exit(66);
  }

  int n = lua_gettop(L) - 3;
  jvalue args[n];
  for (int i = 0; i < n; i++) {
    if (lua_isnil(L, i + 4)) {
      args[i].l = NULL;
    }
    else {
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
    exit(66);
  }

  if ((*J)->IsSameObject(J, ret, NULL)) {
    lua_pushnil(L);
  }
  else {
    const char *ret1 = (*J)->GetStringUTFChars(J, ret, NULL);
    lua_pushstring(L, ret1);
    (*J)->ReleaseStringUTFChars(J, ret, ret1);
  }

  return 1;
}

/**
 * Register lua module.
 * Called by Lua when loading library.
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


static void check_lua_state(JNIEnv *J)
{
  (void)J;
  if (LL == NULL) {
    fprintf(stderr, "lujavrite: unable to call Lua from Java: Lua state is NULL\n");
    exit(66);
  }
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_getglobal(JNIEnv *J, jobject this, jstring name)
{
  (void)this;
  check_lua_state(J);
  const char *name1 = (*J)->GetStringUTFChars(J, name, NULL);
  jint ret = lua_getglobal(LL, name1);
  (*J)->ReleaseStringUTFChars(J, name, name1);
  return ret;
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_getfield(JNIEnv *J, jobject this, jint index, jstring name)
{
  (void)this;
  check_lua_state(J);
  const char *name1 = (*J)->GetStringUTFChars(J, name, NULL);
  jint ret = lua_getfield(LL, index, name1);
  (*J)->ReleaseStringUTFChars(J, name, name1);
  return ret;
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_pushstring(JNIEnv *J, jobject this, jstring string)
{
  (void)this;
  check_lua_state(J);
  const char *string1 = (*J)->GetStringUTFChars(J, string, NULL);
  (void)lua_pushstring(LL, string1);
  (*J)->ReleaseStringUTFChars(J, string, string1);
}

JNIEXPORT jint JNICALL
Java_io_kojan_lujavrite_Lua_pcall(JNIEnv *J, jobject this, jint nargs, jint nresults, jint msgh)
{
  (void)this;
  check_lua_state(J);
  jint ret = lua_pcall(LL, nargs, nresults, msgh);
  return ret;
}

JNIEXPORT jstring JNICALL
Java_io_kojan_lujavrite_Lua_tostring(JNIEnv *J, jobject this, jint index)
{
  (void)this;
  check_lua_state(J);
  const char *val1 =  lua_tostring(LL, index);
  jstring val = (*J)->NewStringUTF(J, val1);
  return val;
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_remove(JNIEnv *J, jobject this, jint index)
{
  (void)this;
  check_lua_state(J);
  lua_remove(LL, index);
}

JNIEXPORT void JNICALL
Java_io_kojan_lujavrite_Lua_pop(JNIEnv *J, jobject this, jint n)
{
  (void)this;
  check_lua_state(J);
  lua_pop(LL, n);
}
