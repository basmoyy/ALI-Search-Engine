// settings.gradle.kts — ALI Android project settings.
//
// This file defines the project name and module structure.
// The C++ core is compiled via the app module's CMakeLists.txt which
// delegates to the repo-root CMakeLists — keeping the core portable.

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "ALI"
include(":app")
