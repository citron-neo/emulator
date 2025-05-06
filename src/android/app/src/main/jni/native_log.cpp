// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/android/android_common.h>
#include <common/logging/log.h>
#include <jni.h>

extern "C" {

void Java_org_citron_citron_1emu_utils_Log_debug(JNIEnv* env, jobject obj, jstring jmessage) {
    LOG_DEBUG(Frontend, "{}", Common::Android::GetJString(env, jmessage));
}

void Java_org_citron_citron_1emu_utils_Log_warning(JNIEnv* env, jobject obj, jstring jmessage) {
    LOG_WARNING(Frontend, "{}", Common::Android::GetJString(env, jmessage));
}

void Java_org_citron_citron_1emu_utils_Log_info(JNIEnv* env, jobject obj, jstring jmessage) {
    LOG_INFO(Frontend, "{}", Common::Android::GetJString(env, jmessage));
}

void Java_org_citron_citron_1emu_utils_Log_error(JNIEnv* env, jobject obj, jstring jmessage) {
    LOG_ERROR(Frontend, "{}", Common::Android::GetJString(env, jmessage));
}

void Java_org_citron_citron_1emu_utils_Log_critical(JNIEnv* env, jobject obj, jstring jmessage) {
    LOG_CRITICAL(Frontend, "{}", Common::Android::GetJString(env, jmessage));
}

} // extern "C"
