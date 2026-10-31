// build.gradle.kts — Root build script.
//
// Declares plugin versions used across all modules.
// No Android-specific logic lives in the shared C++ core.

plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.android)      apply false
    alias(libs.plugins.kotlin.compose)      apply false
}
