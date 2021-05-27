#include <android/log.h>
#include <mutex>
#include <sqlite3.h>

#include "DatabasePlatform.h"
#include "DatabasePlatformAndroid.h"

#define LOG_TAG "watermelondb.jsi"
#define SQLITE_LOG_TAG "watermelondb.sqlite"

namespace watermelondb {
namespace platform {

void consoleLog(std::string message) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s\n", message.c_str());
}

void consoleError(std::string message) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s\n", message.c_str());
}

jint verbose_level;

bool isVerboseLogEnabled() {
    // TODO: Available since API level 30
    // return __android_log_is_loggable(verbose_level, SQLITE_LOG_TAG, ANDROID_LOG_INFO);
    return false;
}

// Based on https://github.com/aosp-mirror/platform_frameworks_base/blob/6bebb8418ceecf44d2af40033870f3aabacfe36e/core/jni/android_database_SQLiteGlobal.cpp#L38
static void sqliteLogCallback(void *data, int err, const char *message) {
    bool isVerbose = !!data;
    int errType = err & 255;
    if (errType == 0 || errType == SQLITE_CONSTRAINT || errType == SQLITE_SCHEMA || errType == SQLITE_NOTICE ||
        err == SQLITE_WARNING_AUTOINDEX) {
        if (isVerbose) {
            __android_log_print(ANDROID_LOG_VERBOSE, SQLITE_LOG_TAG, "(%d) %s\n", err, message);
        }
    } else if (errType == SQLITE_WARNING) {
        __android_log_print(ANDROID_LOG_WARN, SQLITE_LOG_TAG, "(%d) %s\n", err, message);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, SQLITE_LOG_TAG, "(%d) %s\n", err, message);
    }
}

std::once_flag sqliteInitialization;

void initializeSqlite() {
    std::call_once(sqliteInitialization, []() {
        // Redirect sqlite messages to Android log
        if (sqlite3_config(SQLITE_CONFIG_LOG, &sqliteLogCallback, isVerboseLogEnabled()) != SQLITE_OK) {
            consoleError("Failed to configure SQLite to redirect messages to Android log");
        }

        // Enable file URI syntax https://www.sqlite.org/uri.html (e.g. ?mode=memory&cache=shared)
        if (sqlite3_config(SQLITE_CONFIG_URI, 1) != SQLITE_OK) {
            consoleError("Failed to configure SQLite to support file URI syntax - shared cache will not work");
        }

        if (sqlite3_config(SQLITE_CONFIG_SINGLETHREAD) != SQLITE_OK) {
            consoleError("Failed to configure SQLite to SQLITE_CONFIG_SINGLETHREAD");
        }

        // sqlite should do its best to stay <8MB of heap allocations
        // 8MB is what android uses by default:
        // https://github.com/aosp-mirror/platform_frameworks_base/blob/6bebb8418ceecf44d2af40033870f3aabacfe36e/core/jni/android_database_SQLiteGlobal.cpp#L68
        sqlite3_soft_heap_limit(8 * 1024 * 1024);

        if (sqlite3_initialize() != SQLITE_OK) {
            consoleError("Failed to initialize sqlite - this probably means sqlite was already initialized");
        }
    });
}

static JavaVM *jvm;

void configureJNI(JNIEnv *env) {
    assert(env);
    if (env->GetJavaVM(&jvm) != JNI_OK) {
        consoleError("Could not initialize WatermelonDB JSI - cannot get JavaVM");
        std::abort();
    }
    assert(jvm);

    // // find magic constant needed for verbose logs
    // jclass logClass = env->FindClass("android/util/Log");
    // if (logClass == NULL) {
    //     throw std::runtime_error("Unable to find android/util/Log");
    // }
    // jfieldID logVerboseFieldId = env->GetStaticFieldID(logClass, "VERBOSE", "I");
    // if (logVerboseFieldId == NULL) {
    //     throw std::runtime_error("Unable to find android/util/Log's VERBOSE");
    // }
    // verbose_level = env->GetStaticIntField(logClass, logVerboseFieldId);
}

std::string resolveDatabasePath(std::string path) {
    JNIEnv *env;
    assert(jvm);
    if (jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
        throw std::runtime_error("Unable to resolve db path - JVM thread attach failed");
    }
    assert(env);

    jclass clazz = env->FindClass("com/nozbe/watermelondb/jsi/JSIInstaller");
    if (clazz == NULL) {
        throw std::runtime_error("Unable to resolve db path - missing JSIInstaller class");
    }
    jmethodID mid = env->GetStaticMethodID(clazz, "_resolveDatabasePath", "(Ljava/lang/String;)Ljava/lang/String;");
    if (mid == NULL) {
        throw std::runtime_error("Unable to resolve db path - missing Java _resolveDatabasePath method");
    }

    jobject jniPath = env->NewStringUTF(path.c_str());
    if (jniPath == NULL) {
        throw std::runtime_error("Unable to resolve db path - could not construct a Java string");
    }
    jstring jniResolvedPath = (jstring)env->CallStaticObjectMethod(clazz, mid, jniPath);
    if (env->ExceptionCheck()) {
        throw std::runtime_error("Unable to resolve db path - exception occured while resolving path");
    }
    const char *cResolvedPath = env->GetStringUTFChars(jniResolvedPath, 0);
    if (cResolvedPath == NULL) {
        throw std::runtime_error("Unable to resolve db path - failed to get path string");
    }
    std::string resolvedPath(cResolvedPath);
    env->ReleaseStringUTFChars(jniResolvedPath, cResolvedPath);
    return resolvedPath;
}

void deleteDatabaseFile(std::string path, bool warnIfDoesNotExist) {
    // TODO: Unimplemented
}

void onMemoryAlert(std::function<void(void)> callback) {
    // TODO: Unimplemented
    // NOTE: https://developer.android.com/reference/android/app/Application#onTrimMemory(int)
}

char *providedJson = NULL;
size_t providedJsonLen = 0;

void provideJson(jstring json) {
    JNIEnv *env;
    assert(jvm);
    if (jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
        throw std::runtime_error("Unable to resolve db path - JVM thread attach failed");
    }
    assert(env);

    // TODO: MM
    // if (providedJson) {
    //     providedJson = NULL;
    // }

    const char *jsonStr = env->GetStringUTFChars(json, 0);
    if (jsonStr == NULL) {
        throw std::runtime_error("Unable to get json string");
    }

    jsize len = env->GetStringUTFLength(json);

    providedJson = (char*) jsonStr;
    providedJsonLen = len;
}

std::string_view consumeJson(int tag) {
    std::string_view view(providedJson, providedJsonLen);
    return view;
}

} // namespace platform
} // namespace watermelondb
