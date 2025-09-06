// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <future>
#include <jni.h>

#include "video_core/rasterizer_interface.h"

namespace Common::Android {

JNIEnv* GetEnvForThread();

/**
 * Starts a new thread to run JNI. Intended to be used when you must run JNI from a fiber.
 * @tparam T Typename of the return value for the work param
 * @param work Lambda that runs JNI code. This function will take care of attaching this thread to
 * the JVM
 * @return The result from the work lambda param
 */
template <typename T = void>
T RunJNIOnFiber(const std::function<T(JNIEnv*)>& work) {
    std::future<T> j_result = std::async(std::launch::async, [&] {
        auto env = GetEnvForThread();
        return work(env);
    });
    return j_result.get();
}

jclass GetNativeLibraryClass();

jclass GetDiskCacheProgressClass();
jclass GetDiskCacheLoadCallbackStageClass();
jclass GetGameDirClass();
jmethodID GetGameDirConstructor();
jmethodID GetDiskCacheLoadProgress();

jmethodID GetExitEmulationActivity();
jmethodID GetOnEmulationStarted();
jmethodID GetOnEmulationStopped();
jmethodID GetOnProgramChanged();

jclass GetGameClass();
jmethodID GetGameConstructor();
jfieldID GetGameTitleField();
jfieldID GetGamePathField();
jfieldID GetGameProgramIdField();
jfieldID GetGameDeveloperField();
jfieldID GetGameVersionField();
jfieldID GetGameIsHomebrewField();

jclass GetStringClass();
jclass GetPairClass();
jmethodID GetPairConstructor();
jfieldID GetPairFirstField();
jfieldID GetPairSecondField();

jclass GetOverlayControlDataClass();
jmethodID GetOverlayControlDataConstructor();
jfieldID GetOverlayControlDataIdField();
jfieldID GetOverlayControlDataEnabledField();
jfieldID GetOverlayControlDataLandscapePositionField();
jfieldID GetOverlayControlDataPortraitPositionField();
jfieldID GetOverlayControlDataFoldablePositionField();

jclass GetPatchClass();
jmethodID GetPatchConstructor();
jfieldID GetPatchEnabledField();
jfieldID GetPatchNameField();
jfieldID GetPatchVersionField();
jfieldID GetPatchTypeField();
jfieldID GetPatchProgramIdField();
jfieldID GetPatchTitleIdField();

jclass GetDoubleClass();
jmethodID GetDoubleConstructor();
jfieldID GetDoubleValueField();

jclass GetIntegerClass();
jmethodID GetIntegerConstructor();
jfieldID GetIntegerValueField();

jclass GetBooleanClass();
jmethodID GetBooleanConstructor();
jfieldID GetBooleanValueField();

jclass GetPlayerInputClass();
jmethodID GetPlayerInputConstructor();
jfieldID GetPlayerInputConnectedField();
jfieldID GetPlayerInputButtonsField();
jfieldID GetPlayerInputAnalogsField();
jfieldID GetPlayerInputMotionsField();
jfieldID GetPlayerInputVibrationEnabledField();
jfieldID GetPlayerInputVibrationStrengthField();
jfieldID GetPlayerInputBodyColorLeftField();
jfieldID GetPlayerInputBodyColorRightField();
jfieldID GetPlayerInputButtonColorLeftField();
jfieldID GetPlayerInputButtonColorRightField();
jfieldID GetPlayerInputProfileNameField();
jfieldID GetPlayerInputUseSystemVibratorField();

jclass GetCitronInputDeviceInterface();
jmethodID GetCitronDeviceGetName();
jmethodID GetCitronDeviceGetGUID();
jmethodID GetCitronDeviceGetPort();
jmethodID GetCitronDeviceGetSupportsVibration();
jmethodID GetCitronDeviceVibrate();
jmethodID GetCitronDeviceGetAxes();
jmethodID GetCitronDeviceHasKeys();

} // namespace Common::Android
