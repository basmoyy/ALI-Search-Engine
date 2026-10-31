// app/build.gradle.kts — ALI Android app module.
//
// The C++ core is compiled via externalNativeBuild → CMake.
// The CMakeLists.txt in src/main/cpp/ points at the repo-root core,
// keeping the native code entirely platform-independent.
//
// ABI targets:
//   arm64-v8a   — primary (Android 8+, most current devices)
//   armeabi-v7a — secondary (older 32-bit devices)
//   x86_64      — emulator support
//
// Minimum SDK 26 (Android 8.0) ensures:
//   - Full NDK C++17/20 support
//   - WorkManager background scheduling
//   - Reliable foreground service APIs

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace   = "com.ali.search"
    compileSdk  = 35

    defaultConfig {
        applicationId  = "com.ali.search"
        minSdk         = 26
        targetSdk      = 35
        versionCode    = 1
        versionName    = "0.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // Pass the ABI filter down to CMake.
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        // CMake build for the native C++ core.
        // The CMakeLists.txt delegates to the repo-root core/ directory,
        // so no core code is duplicated or modified for Android.
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti")
                arguments += listOf(
                    // Point at the repo root so CMake can find core/, interfaces/, etc.
                    "-DANDROID_STL=c++_shared",
                    "-DALI_BUILD_TESTS=OFF",
                    "-DALI_BUILD_BENCHMARKS=OFF",
                    "-DALI_BUILD_TOOLS=OFF"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path    = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled   = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlinOptions {
        jvmTarget = "11"
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(platform(libs.compose.bom))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.work.runtime.ktx)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.compose.material3)
    implementation(libs.compose.foundation)
    implementation(libs.kotlinx.coroutines.android)

    debugImplementation(libs.compose.ui.tooling)
}
