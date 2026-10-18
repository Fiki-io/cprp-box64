#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <android/log.h>

#define LOG_TAG "CPRP_CORE64"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int box64_main(int argc, const char **argv, char **env);

static __thread jmp_buf s_exit_jmp;
static __thread int s_in_runner = 0;

#define CONSOLE_BUFFER_SIZE 65536
static char s_console_buffer[CONSOLE_BUFFER_SIZE];
static size_t s_console_len = 0;
static pthread_mutex_t s_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static int s_pipe_read_fd = -1;
static pthread_t s_logger_thread;
static int s_logger_initialized = 0;

static void append_to_buffer(const char* data, size_t len) {
    if (!data || len == 0) return;
    pthread_mutex_lock(&s_buffer_mutex);
    if (s_console_len + len >= CONSOLE_BUFFER_SIZE - 1) {
        size_t overflow = (s_console_len + len) - (CONSOLE_BUFFER_SIZE - 2);
        if (overflow < s_console_len) {
            memmove(s_console_buffer, s_console_buffer + overflow, s_console_len - overflow);
            s_console_len -= overflow;
        } else {
            s_console_len = 0;
        }
    }
    memcpy(s_console_buffer + s_console_len, data, len);
    s_console_len += len;
    s_console_buffer[s_console_len] = '\0';
    pthread_mutex_unlock(&s_buffer_mutex);
}

static void* logger_thread_func(void* arg) {
    int fd = (int)(intptr_t)arg;
    char buf[1024];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
        append_to_buffer(buf, (size_t)n);
    }
    return NULL;
}

static void init_stdout_stderr_pipe(void) {
    if (s_logger_initialized) return;
    int pfd[2];
    if (pipe(pfd) == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        s_pipe_read_fd = pfd[0];
        pthread_create(&s_logger_thread, NULL, logger_thread_func, (void*)(intptr_t)s_pipe_read_fd);
        pthread_detach(s_logger_thread);
        s_logger_initialized = 1;
    }
}

__attribute__((visibility("default"), noreturn)) void exit(int status) {
    LOGI("exit(%d) intercepted in CPRP native bridge", status);
    if (s_in_runner) {
        longjmp(s_exit_jmp, status == 0 ? 10000 : status);
    }
    _Exit(status);
}

__attribute__((visibility("default"), noreturn)) void _exit(int status) {
    exit(status);
}

__attribute__((visibility("default"), noreturn)) void _Exit(int status) {
    LOGI("_Exit(%d) intercepted in CPRP native bridge", status);
    if (s_in_runner) {
        longjmp(s_exit_jmp, status == 0 ? 10000 : status);
    }
    exit(status);
}

__attribute__((visibility("default"), noreturn)) void abort(void) {
    LOGE("abort() intercepted in CPRP native bridge");
    fprintf(stderr, "[BOX64] abort() terpanggil. Menghentikan emulasi secara aman.\n");
    if (s_in_runner) {
        longjmp(s_exit_jmp, 134);
    }
    _Exit(134);
}

__attribute__((visibility("default"))) int execvp(const char* file, char* const argv[]) {
    LOGE("execvp intercepted in CPRP bridge: attempted to execute '%s' directly. Blocked to prevent Android OS kill.", file ? file : "null");
    fprintf(stderr, "[BOX64] Error: '%s' bukan berkas Linux ELF. Pemanggilan execvp diblokir untuk mencegah crash Android OS.\n", file ? file : "null");
    errno = ENOEXEC;
    return -1;
}

__attribute__((visibility("default"))) int execve(const char* pathname, char* const argv[], char* const envp[]) {
    LOGE("execve intercepted in CPRP bridge: attempted to execute '%s'. Blocked to prevent Android OS kill.", pathname ? pathname : "null");
    fprintf(stderr, "[BOX64] Error: '%s' execve diblokir di lingkungan Android.\n", pathname ? pathname : "null");
    errno = ENOEXEC;
    return -1;
}

static void cprp_bridge_signal_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    void* fault_addr = info ? info->si_addr : NULL;
    LOGE("Fatal signal %d trapped in CPRP bridge at %p", sig, fault_addr);
    fprintf(stderr, "\n[BOX64] Sinyal fatal %d tertangkap pada alamat %p. Menghentikan emulasi secara aman.\n", sig, fault_addr);
    if (s_in_runner) {
        longjmp(s_exit_jmp, 128 + sig);
    }
    _Exit(128 + sig);
}

static int run_core_internal(JNIEnv* env, jobjectArray argsArray) {
    init_stdout_stderr_pipe();
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

    struct sigaction sa_new, sa_old_segv, sa_old_bus, sa_old_ill, sa_old_abrt;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_sigaction = cprp_bridge_signal_handler;
    sa_new.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa_new, &sa_old_segv);
    sigaction(SIGBUS, &sa_new, &sa_old_bus);
    sigaction(SIGILL, &sa_new, &sa_old_ill);
    sigaction(SIGABRT, &sa_new, &sa_old_abrt);

    s_in_runner = 1;
    int jmp_res = setjmp(s_exit_jmp);
    int result = 0;
    if (jmp_res == 0) {
        result = box64_main(argc, argv, NULL);
    } else {
        result = (jmp_res == 10000) ? 0 : jmp_res;
        LOGI("box64_main unwound via intercepted exit/signal with code: %d", result);
    }
    s_in_runner = 0;

    sigaction(SIGSEGV, &sa_old_segv, NULL);
    sigaction(SIGBUS, &sa_old_bus, NULL);
    sigaction(SIGILL, &sa_old_ill, NULL);
    sigaction(SIGABRT, &sa_old_abrt, NULL);

    fflush(stdout);
    fflush(stderr);
    usleep(25000);

    for (int i = 0; i < argc; i++) {
        jstring str = (jstring)(*env)->GetObjectArrayElement(env, argsArray, i);
        (*env)->ReleaseStringUTFChars(env, str, argv[i]);
    }
    free(argv);

    LOGI("CPRP Core64 execution finished with code: %d", result);
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

JNIEXPORT jstring JNICALL Java_com_cprp_launcher_NativeBridge_pollLogOutput(
    JNIEnv* env,
    jobject thiz) {
    pthread_mutex_lock(&s_buffer_mutex);
    jstring res = (*env)->NewStringUTF(env, s_console_buffer);
    s_console_buffer[0] = '\0';
    s_console_len = 0;
    pthread_mutex_unlock(&s_buffer_mutex);
    return res;
}
