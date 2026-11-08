// jni_bridge.cpp - thin JNI layer: Kotlin strings in, strings out. Package: com.example.ebovtip (change to yours).
#include <jni.h>
extern "C" {
const char* ebov_version(); char* ebov_run(const char*, int); char* ebov_screen(const char*, const char*, const char*); void ebov_free(char*);
}
static jstring wrap(JNIEnv* env, char* s) { jstring j = env->NewStringUTF(s ? s : ""); ebov_free(s); return j; }

extern "C" JNIEXPORT jstring JNICALL Java_com_example_ebovtip_EngineBridge_version(JNIEnv* env, jobject) {
  return env->NewStringUTF(ebov_version());
}
extern "C" JNIEXPORT jstring JNICALL Java_com_example_ebovtip_EngineBridge_run(JNIEnv* env, jobject, jstring params, jint format) {
  const char* p = env->GetStringUTFChars(params, nullptr);
  jstring out = wrap(env, ebov_run(p, format));
  env->ReleaseStringUTFChars(params, p);
  return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_example_ebovtip_EngineBridge_screen(JNIEnv* env, jobject, jstring params, jstring screenCfg, jstring prefix) {
  const char* p = env->GetStringUTFChars(params, nullptr);
  const char* s = env->GetStringUTFChars(screenCfg, nullptr);
  const char* x = env->GetStringUTFChars(prefix, nullptr);
  jstring out = wrap(env, ebov_screen(p, s, x));
  env->ReleaseStringUTFChars(params, p); env->ReleaseStringUTFChars(screenCfg, s); env->ReleaseStringUTFChars(prefix, x);
  return out;
}
