/*-
 * Copyright (c) 2023 Red Hat, Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <jni.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

static JNIEnv *J;

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
    jvmopt[0].optionString = (char *)luaL_checkstring(L, i + 2);
  }

  JavaVMInitArgs vmArgs;
  vmArgs.version = JNI_VERSION_1_8;
  vmArgs.nOptions = n;
  vmArgs.options = jvmopt;
  vmArgs.ignoreUnrecognized = JNI_FALSE;

  JavaVM *javaVM;
  jint flag = JNI_CreateJavaVM(&javaVM, (void **)&J, &vmArgs);
  if (flag == JNI_ERR) {
    fprintf(stderr, "lujavrite: error: failed to create JVM\n");
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

  jstring ret = (*J)->CallStaticObjectMethodA(J, jcls, methodId, args);
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
    {"init", init},
    {"call", call},
    {NULL, NULL},
  };

  lua_newtable(L);
  luaL_setfuncs(L, functs, 0);
  return 1;
}
