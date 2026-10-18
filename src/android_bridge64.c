#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "CPRP_CORE64"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int box64_main(int argc, const char **argv, char **env);

static int run_core_internal(JNIEnv* env, jobjectArray argsArray) {
    if (!argsArray) return -1;
    int argc = (*env)->GetArrayLength(env, argsArray);
    const char** argv = (const char**)malloc((argc + 1) * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        jstring str = (jstring)(*env)->GetObjectArrayElement(env, argsArray, i);
        argv[i] = (*env)->GetStringUTFChars(env, str, 0);
    }
    argv[argc] = NULL;

    LOGI("CPRP Core64 (Box64 ARM64) invoked with %d arguments.", argc);
    for (int i = 0; i < argc; i++) {
        LOGI("arg[%d] = %s", i, argv[i]);
    }

    int result = box64_main(argc, argv, NULL);
    LOGI("CPRP Core64 finished with code: %d", result);
    return result;
}

JNIEXPORT jint JNICALL Java_com_cprp_launcher_NativeBridge_startCore(
    JNIEnv* env,
    jobject thiz,
    jobjectArray argsArray) {
    return run_core_internal(env, argsArray);
}

JNIEXPORT jint JNICALL Java_com_cprp_launcher_NativeBridge_startCore64(
    JNIEnv* env,
    jobject thiz,
    jobjectArray argsArray) {
    return run_core_internal(env, argsArray);
}
