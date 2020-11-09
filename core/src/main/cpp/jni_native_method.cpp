#include <vector>
#include <unistd.h>
#include <mntent.h>

#include "jni_native_method.h"
#include "logging.h"
#include "misc.h"
#include "module.h"
#include "api.h"
#include "main.h"

namespace JNI {

    namespace Zygote {
        const char* classname = "com/android/internal/os/Zygote";
        JNINativeMethod *nativeForkAndSpecialize = nullptr;
        JNINativeMethod *nativeSpecializeAppProcess = nullptr;
        JNINativeMethod *nativeForkSystemServer = nullptr;
    }

    namespace SystemProperties {
        const char* classname = "android/os/SystemProperties";
        JNINativeMethod *set = nullptr;
    }
}

static int shouldSkipUid(int uid) {
    int appId = uid % 100000;

    // limit only regular app, or strange situation will happen, such as zygote process not start (dead for no reason and leave no clues?)
    // https://android.googlesource.com/platform/frameworks/base/+/android-9.0.0_r8/core/java/android/os/UserHandle.java#151
    if (appId >= 10000 && appId <= 19999) return 0;
    return 1;
}

// -----------------------------------------------------------------

static void nativeForkAndSpecialize_pre(
        JNIEnv *env, jclass clazz, jint &uid, jint &gid, jintArray &gids, jint &runtime_flags,
        jobjectArray &rlimits, jint &mount_external, jstring &se_info, jstring &se_name,
        jintArray &fdsToClose, jintArray &fdsToIgnore, jboolean &is_child_zygote,
        jstring &instructionSet, jstring &appDataDir, jboolean &isTopApp, jobjectArray &pkgDataInfoList,
        jobjectArray &whitelistedDataInfoList, jboolean &bindMountAppDataDirs, jboolean &bindMountAppStorageDirs) {

    for (auto module : *get_modules()) {
        if (!module->hasForkAndSpecializePre())
            continue;

        if (module->hasShouldSkipUid() && module->shouldSkipUid(uid))
            continue;

        if (!module->hasShouldSkipUid() && shouldSkipUid(uid))
            continue;

        module->forkAndSpecializePre(
                env, clazz, &uid, &gid, &gids, &runtime_flags, &rlimits, &mount_external,
                &se_info, &se_name, &fdsToClose, &fdsToIgnore, &is_child_zygote,
                &instructionSet, &appDataDir, &isTopApp, &pkgDataInfoList, &whitelistedDataInfoList,
                &bindMountAppDataDirs, &bindMountAppStorageDirs);
    }
}

static void nativeForkAndSpecialize_post(JNIEnv *env, jclass clazz, jint uid, jint res) {

    if (res == 0) restore_replaced_func(env);

    for (auto module : *get_modules()) {
        if (!module->hasForkAndSpecializePost())
            continue;

        if (module->hasShouldSkipUid() && module->shouldSkipUid(uid))
            continue;

        if (!module->hasShouldSkipUid() && shouldSkipUid(uid))
            continue;

        /*
         * Magic problem:
         * There is very low change that zygote process stop working and some processes forked from zygote
         * become zombie process.
         * When the problem happens:
         * The following log (%s: forkAndSpecializePost) is not printed
         * strace zygote: futex(0x6265a70698, FUTEX_WAIT_BITSET_PRIVATE, 2, NULL, 0xffffffff
         * zygote maps: 6265a70000-6265a71000 rw-p 00020000 103:04 1160  /system/lib64/liblog.so
         * 6265a70698-6265a70000+20000 is nothing in liblog
         *
         * Don't known why, so we just don't print log in zygote and see what will happen
         */
        if (res == 0) LOGD("%s: forkAndSpecializePost", module->name);

        module->forkAndSpecializePost(env, clazz, res);
    }
}

// -----------------------------------------------------------------

static void nativeSpecializeAppProcess_pre(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir,
        jboolean &isTopApp, jobjectArray &pkgDataInfoList, jobjectArray &whitelistedDataInfoList,
        jboolean &bindMountAppDataDirs, jboolean &bindMountAppStorageDirs) {

    for (auto module : *get_modules()) {
        if (!module->hasSpecializeAppProcessPre())
            continue;

        module->specializeAppProcessPre(
                env, clazz, &uid, &gid, &gids, &runtimeFlags, &rlimits, &mountExternal, &seInfo,
                &niceName, &startChildZygote, &instructionSet, &appDataDir, &isTopApp,
                &pkgDataInfoList, &whitelistedDataInfoList, &bindMountAppDataDirs, &bindMountAppStorageDirs);
    }
}

static void nativeSpecializeAppProcess_post(JNIEnv *env, jclass clazz) {

    restore_replaced_func(env);

    for (auto module : *get_modules()) {
        if (!module->hasSpecializeAppProcessPost())
            continue;

        LOGD("%s: specializeAppProcessPost", module->name);
        module->specializeAppProcessPost(env, clazz);
    }
}

// -----------------------------------------------------------------

static void nativeForkSystemServer_pre(
        JNIEnv *env, jclass clazz, uid_t &uid, gid_t &gid, jintArray &gids, jint &debug_flags,
        jobjectArray &rlimits, jlong &permittedCapabilities, jlong &effectiveCapabilities) {

    for (auto module : *get_modules()) {
        if (!module->hasForkSystemServerPre())
            continue;

        module->forkSystemServerPre(
                env, clazz, &uid, &gid, &gids, &debug_flags, &rlimits, &permittedCapabilities,
                &effectiveCapabilities);
    }
}

static void nativeForkSystemServer_post(JNIEnv *env, jclass clazz, jint res) {
    for (auto module : *get_modules()) {
        if (!module->hasForkSystemServerPost())
            continue;

        if (res == 0) LOGD("%s: forkSystemServerPost", module->name);
        module->forkSystemServerPost(env, clazz, res);
    }
}

// -----------------------------------------------------------------

jint nativeForkAndSpecialize_marshmallow(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint debug_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jstring instructionSet, jstring appDataDir) {

    jintArray fdsToIgnore = nullptr;
    jboolean is_child_zygote = JNI_FALSE;
    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_marshmallow_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, instructionSet, appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_oreo(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint debug_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jstring instructionSet, jstring appDataDir) {

    jboolean is_child_zygote = JNI_FALSE;
    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_oreo_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, instructionSet, appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_p(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir) {

    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_p_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, is_child_zygote, instructionSet, appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_q_alternative(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir, jboolean isTopApp) {

    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_q_alternative_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, is_child_zygote, instructionSet, appDataDir, isTopApp);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_r(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir, jboolean isTopApp, jobjectArray pkgDataInfoList,
        jobjectArray whitelistedDataInfoList, jboolean bindMountAppDataDirs, jboolean bindMountAppStorageDirs) {

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_r_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, is_child_zygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_r_dp3(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir, jboolean isTopApp, jobjectArray pkgDataInfoList,
        jboolean bindMountAppStorageDirs) {

    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_r_dp3_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, is_child_zygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            bindMountAppStorageDirs);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_r_dp2(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jstring se_name,
        jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir, jboolean isTopApp, jobjectArray pkgDataInfoList) {

    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_r_dp2_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, se_name,
            fdsToClose, fdsToIgnore, is_child_zygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_samsung_p(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtime_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jint category, jint accessInfo,
        jstring se_name, jintArray fdsToClose, jintArray fdsToIgnore, jboolean is_child_zygote,
        jstring instructionSet, jstring appDataDir) {

    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_samsung_p_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, runtime_flags, rlimits, mount_external, se_info, category,
            accessInfo, se_name, fdsToClose, fdsToIgnore, is_child_zygote, instructionSet,
            appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_samsung_o(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint debug_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jint category, jint accessInfo,
        jstring se_name, jintArray fdsToClose, jintArray fdsToIgnore, jstring instructionSet,
        jstring appDataDir) {

    jboolean is_child_zygote = JNI_FALSE;
    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_samsung_o_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external, se_info, category,
            accessInfo, se_name, fdsToClose, fdsToIgnore, instructionSet, appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_samsung_n(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint debug_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jint category, jint accessInfo,
        jstring se_name, jintArray fdsToClose, jstring instructionSet, jstring appDataDir,
        jint a1) {

    jintArray fdsToIgnore = nullptr;
    jboolean is_child_zygote = JNI_FALSE;
    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_samsung_n_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external, se_info, category,
            accessInfo, se_name, fdsToClose, instructionSet, appDataDir, a1);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

jint nativeForkAndSpecialize_samsung_m(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint debug_flags,
        jobjectArray rlimits, jint mount_external, jstring se_info, jint category, jint accessInfo,
        jstring se_name, jintArray fdsToClose, jstring instructionSet, jstring appDataDir) {

    jintArray fdsToIgnore = nullptr;
    jboolean is_child_zygote = JNI_FALSE;
    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeForkAndSpecialize_pre(env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external,
                                se_info, se_name, fdsToClose, fdsToIgnore, is_child_zygote,
                                instructionSet, appDataDir, isTopApp, pkgDataInfoList, whitelistedDataInfoList,
                                bindMountAppDataDirs, bindMountAppStorageDirs);

    jint res = ((nativeForkAndSpecialize_samsung_m_t *) JNI::Zygote::nativeForkAndSpecialize->fnPtr)(
            env, clazz, uid, gid, gids, debug_flags, rlimits, mount_external, se_info, category,
            accessInfo, se_name, fdsToClose, instructionSet, appDataDir);

    nativeForkAndSpecialize_post(env, clazz, uid, res);
    return res;
}

// -----------------------------------------------------------------

void nativeSpecializeAppProcess_q(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir) {

    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_q_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir);

    nativeSpecializeAppProcess_post(env, clazz);
}

void nativeSpecializeAppProcess_q_alternative(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir,
        jboolean isTopApp) {

    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_q_alternative_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp);

    nativeSpecializeAppProcess_post(env, clazz);
}

void nativeSpecializeAppProcess_r(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir,
        jboolean isTopApp, jobjectArray pkgDataInfoList, jobjectArray whitelistedDataInfoList,
        jboolean bindMountAppDataDirs, jboolean bindMountAppStorageDirs) {

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_r_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    nativeSpecializeAppProcess_post(env, clazz);
}

void nativeSpecializeAppProcess_r_dp3(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir,
        jboolean isTopApp, jobjectArray pkgDataInfoList, jboolean bindMountAppStorageDirs) {

    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_r_dp3_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            bindMountAppStorageDirs);

    nativeSpecializeAppProcess_post(env, clazz);
}

void nativeSpecializeAppProcess_r_dp2(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jstring niceName,
        jboolean startChildZygote, jstring instructionSet, jstring appDataDir,
        jboolean isTopApp, jobjectArray pkgDataInfoList) {

    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_r_dp2_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList);

    nativeSpecializeAppProcess_post(env, clazz);
}


void nativeSpecializeAppProcess_samsung_q(
        JNIEnv *env, jclass clazz, jint uid, jint gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jint mountExternal, jstring seInfo, jint space, jint accessInfo,
        jstring niceName, jboolean startChildZygote, jstring instructionSet, jstring appDataDir) {

    jboolean isTopApp = JNI_FALSE;
    jobjectArray pkgDataInfoList = nullptr;
    jobjectArray whitelistedDataInfoList = nullptr;
    jboolean bindMountAppDataDirs = JNI_FALSE;
    jboolean bindMountAppStorageDirs = JNI_FALSE;

    nativeSpecializeAppProcess_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, niceName,
            startChildZygote, instructionSet, appDataDir, isTopApp, pkgDataInfoList,
            whitelistedDataInfoList, bindMountAppDataDirs, bindMountAppStorageDirs);

    ((nativeSpecializeAppProcess_samsung_t *) JNI::Zygote::nativeSpecializeAppProcess->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, mountExternal, seInfo, space,
            accessInfo, niceName, startChildZygote, instructionSet, appDataDir);

    nativeSpecializeAppProcess_post(env, clazz);
}

// -----------------------------------------------------------------

jint nativeForkSystemServer(
        JNIEnv *env, jclass clazz, uid_t uid, gid_t gid, jintArray gids, jint runtimeFlags,
        jobjectArray rlimits, jlong permittedCapabilities, jlong effectiveCapabilities) {

    nativeForkSystemServer_pre(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, permittedCapabilities,
            effectiveCapabilities);

    jint res = ((nativeForkSystemServer_t *) JNI::Zygote::nativeForkSystemServer->fnPtr)(
            env, clazz, uid, gid, gids, runtimeFlags, rlimits, permittedCapabilities,
            effectiveCapabilities);

    nativeForkSystemServer_post(env, clazz, res);
    return res;
}

jint nativeForkSystemServer_samsung_q(
        JNIEnv *env, jclass cls, uid_t uid, gid_t gid, jintArray gids, jint runtimeFlags,
        jint space, jint accessInfo, jobjectArray rlimits, jlong permittedCapabilities,
        jlong effectiveCapabilities) {

    nativeForkSystemServer_pre(
            env, cls, uid, gid, gids, runtimeFlags, rlimits, permittedCapabilities,
            effectiveCapabilities);

    jint res = ((nativeForkSystemServer_samsung_q_t *) JNI::Zygote::nativeForkSystemServer->fnPtr)(
            env, cls, uid, gid, gids, runtimeFlags, space, accessInfo, rlimits, permittedCapabilities,
            effectiveCapabilities);

    nativeForkSystemServer_post(env, cls, res);
    return res;
}

/*
 * On Android 9+, in very rare cases, SystemProperties.set("sys.user." + userId + ".ce_available", "true")
 * will throw an exception (we don't known if this is caused by Riru) and user data will be wiped.
 * So we hook it and clear the exception to prevent this problem from happening.
 *
 * log:
 * UserDataPreparer: Setting property: sys.user.0.ce_available=true
 * PackageManager: Destroying user 0 on volume null because we failed to prepare: java.lang.RuntimeException: failed to set system property
 *
 * http://androidxref.com/9.0.0_r3/xref/frameworks/base/services/core/java/com/android/server/pm/UserDataPreparer.java#107
 * -> http://androidxref.com/9.0.0_r3/xref/frameworks/base/services/core/java/com/android/server/pm/UserDataPreparer.java#112
 * -> http://androidxref.com/9.0.0_r3/xref/system/vold/VoldNativeService.cpp#751
 * -> http://androidxref.com/9.0.0_r3/xref/system/vold/Ext4Crypt.cpp#743
 * -> http://androidxref.com/9.0.0_r3/xref/system/vold/Ext4Crypt.cpp#221
 */
void SystemProperties_set(JNIEnv *env, jobject clazz, jstring keyJ, jstring valJ) {
    const char *key = env->GetStringUTFChars(keyJ, JNI_FALSE);
    char user[16];
    int no_throw = sscanf(key, "sys.user.%[^.].ce_available", user) == 1;
    env->ReleaseStringUTFChars(keyJ, key);

    ((SystemProperties_set_t *) JNI::SystemProperties::set->fnPtr)(env, clazz, keyJ, valJ);

    jthrowable exception = env->ExceptionOccurred();
    if (exception && no_throw) {
        LOGW("prevented data destroy");

        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}