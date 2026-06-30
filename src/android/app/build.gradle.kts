// SPDX-FileCopyrightText: 2023 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

plugins {
    id("com.android.application")
    id("kotlin-parcelize")
    kotlin("plugin.serialization") version "2.4.0"
    id("androidx.navigation.safeargs.kotlin")
    id("org.jlleitschuh.gradle.ktlint") version "14.2.0"
}

/**
 * Use the number of seconds/10 since Jan 1 2016 as the versionCode.
 * This lets us upload a new build at most every 10 seconds for the
 * next 680 years.
 */
val autoVersion = (((System.currentTimeMillis() / 1000) - 1451606400) / 10).toInt()

@Suppress("UnstableApiUsage")
android {
    namespace = "org.citron.citron_emu"

    compileSdk = 37
    ndkVersion = "26.1.10909125"

    buildFeatures {
        viewBinding = true
        buildConfig = true
        resValues = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        // This is necessary for libadrenotools custom driver loading
        jniLibs.useLegacyPackaging = true
    }

    androidResources {
        generateLocaleConfig = true
    }

    defaultConfig {
        // TODO If this is ever modified, change application_id in strings.xml
        applicationId = "org.citron.citron_emu"
        minSdk = 30
        //noinspection OldTargetApi
        targetSdk = 36
        versionName = getGitVersion()

        versionCode = if (System.getenv("AUTO_VERSIONED") == "true") {
            autoVersion
        } else {
            1
        }

        ndk {
            //noinspection ChromeOsAbiSupport
            abiFilters += listOf("arm64-v8a")
        }

        buildConfigField("String", "GIT_HASH", "\"${getGitHash()}\"")
        buildConfigField("String", "BRANCH", "\"${getBranch()}\"")
    }

    val keystoreFile = System.getenv("ANDROID_KEYSTORE_FILE")
    signingConfigs {
        if (keystoreFile != null) {
            create("release") {
                storeFile = file(keystoreFile)
                storePassword = System.getenv("ANDROID_KEYSTORE_PASS")
                keyAlias = System.getenv("ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("ANDROID_KEYSTORE_PASS")
            }
        }
        create("default") {
            storeFile = file("$projectDir/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    // Define build types, which are orthogonal to product flavors.
    buildTypes {

        // Signed by release key, allowing for upload to Play Store.
        release {
            signingConfig = if (keystoreFile != null) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("default")
            }

            resValue("string", "app_name_suffixed", "citron-neo: The switch fell off")
            isDebuggable = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }

        // builds a release build that doesn't need signing
        // Attaches 'debug' suffix to version and package name, allowing installation alongside the release build.
        register("relWithDebInfo") {
            initWith(getByName("release"))
            matchingFallbacks += listOf("release")
            isDebuggable = true
            isMinifyEnabled = false

            applicationIdSuffix = ".relWithDebInfo"
            versionNameSuffix = "-relWithDebInfo"
        }

        // Signed by debug key disallowing distribution on Play Store.
        // Attaches 'debug' suffix to version and package name, allowing installation alongside the release build.
        debug {
            signingConfig = signingConfigs.getByName("default")
            resValue("string", "app_name_suffixed", "citron-neo: The switch fell off Debug")
            isDebuggable = true
            isJniDebuggable = true
            versionNameSuffix = "-debug"
            applicationIdSuffix = ".debug"
        }
    }

    flavorDimensions.add("version")
    productFlavors {
        create("mainline") {
            isDefault = true
            dimension = "version"
            buildConfigField("Boolean", "PREMIUM", "true")
        }

        create("ea") {
            dimension = "version"
            buildConfigField("Boolean", "PREMIUM", "true")
            applicationIdSuffix = ".ea"
        }
    }

    externalNativeBuild {
        cmake {
            version = "3.22.1"
            path = file("../../../CMakeLists.txt")
        }
    }

    defaultConfig {
        externalNativeBuild {
            cmake {
                arguments(
                    "-DENABLE_QT=0", // Don't use QT
                    "-DENABLE_SDL2=0", // Don't use SDL
                    "-DENABLE_WEB_SERVICE=0", // Don't use telemetry
                    "-DENABLE_OPENSSL=1",
                    "-DBUNDLE_SPEEX=ON",
                    "-DANDROID_ARM_NEON=true", // cryptopp requires Neon to work
                    "-DCITRON_USE_BUNDLED_VCPKG=ON",
                    "-DCITRON_USE_BUNDLED_FFMPEG=ON",
                    "-DCITRON_ENABLE_LTO=ON",
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                )

                abiFilters("arm64-v8a", "x86_64")
            }
        }
    }
}

tasks.register<Delete>("ktlintReset") {
    description = "Clean ktlint generated intermediate files"
    delete(layout.buildDirectory.dir("intermediates/ktLint"))
}


ktlint {
    version.set("1.0.1")
}


dependencies {
    implementation("androidx.core:core-ktx:1.19.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.recyclerview:recyclerview:1.4.0")
    implementation("androidx.constraintlayout:constraintlayout:2.2.1")
    implementation("androidx.fragment:fragment-ktx:1.8.9")
    implementation("androidx.documentfile:documentfile:1.1.0")
    implementation("com.google.android.material:material:1.14.0")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.11.0")
    implementation("io.coil-kt:coil:2.7.0")
    implementation("androidx.core:core-splashscreen:1.2.0")
    implementation("androidx.window:window:1.5.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.2.0")
    implementation("androidx.navigation:navigation-fragment-ktx:2.9.8")
    implementation("androidx.navigation:navigation-ui-ktx:2.9.8")
    implementation("info.debatty:java-string-similarity:2.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")
}

fun runGitCommand(command: List<String>): String {
    return try {
        ProcessBuilder(command)
            .directory(project.rootDir)
            .redirectOutput(ProcessBuilder.Redirect.PIPE)
            .redirectError(ProcessBuilder.Redirect.PIPE)
            .start().inputStream.bufferedReader().use { it.readText() }
            .trim()
    } catch (_: Exception) {
        logger.error("Cannot find git")
        ""
    }
}

fun getGitVersion(): String {
    val gitVersion = runGitCommand(
        listOf(
            "git",
            "describe",
            "--always",
            "--long"
        )
    ).replace(Regex("(-0)?-[^-]+$"), "")
    val versionName = if (System.getenv("GITHUB_ACTIONS") != null) {
        System.getenv("GIT_TAG_NAME") ?: gitVersion
    } else {
        gitVersion
    }
    return versionName.ifEmpty { "0.0" }
}

fun getGitHash(): String =
    runGitCommand(listOf("git", "rev-parse", "--short", "HEAD")).ifEmpty { "dummy-hash" }

fun getBranch(): String =
    runGitCommand(listOf("git", "rev-parse", "--abbrev-ref", "HEAD")).ifEmpty { "dummy-hash" }
