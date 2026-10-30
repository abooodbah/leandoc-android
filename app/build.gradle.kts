import java.util.Properties
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// The signing key stays on the maintainer's machine. keystore.properties and
// every keystore file are ignored by git; see "Signing for Google Play" in the
// README. CI writes the file from repository secrets: the upload key for the
// Play bundle, the release key for the GitHub release. Without the file, release
// builds come out unsigned, which is what F-Droid expects. Pass
// -PdebugSignRelease to sign them with the debug key instead, so they install
// for testing (CI does this for its emulator measurements).
val uploadKey = rootProject.file("keystore.properties").takeIf { it.isFile }?.let { file ->
    Properties().apply { file.inputStream().use { load(it) } }
}
val debugSignRelease = providers.gradleProperty("debugSignRelease").isPresent

android {
    namespace = "io.github.abooodbah.leandoc"
    compileSdk = 36
    // r28 links native code for 16 KB pages by default, which Google Play
    // requires for apps that target Android 15 or later.
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.abooodbah.leandoc"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "0.1.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk {
            // No phone that runs Android 8 or later uses 32-bit x86; emulators
            // and Chromebooks are x86_64. Leaving it out saves about 1 MB.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    signingConfigs {
        if (uploadKey != null) {
            create("upload") {
                storeFile = rootProject.file(uploadKey.getProperty("storeFile"))
                storePassword = uploadKey.getProperty("storePassword")
                keyAlias = uploadKey.getProperty("keyAlias")
                keyPassword = uploadKey.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            signingConfig = signingConfigs.findByName("upload") ?: signingConfigs.getByName("debug").takeIf { debugSignRelease }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        abortOnError = true
        checkReleaseBuilds = true
        // Reports go to the CI artifact so a failure can be read without a rerun.
        htmlReport = true
        xmlReport = false
    }

    // The dependency list that AGP encrypts for Google Play is kept out of the APK,
    // where IzzyOnDroid flags it as an unreadable blob, and kept in the Play
    // bundle.
    dependenciesInfo {
        includeInApk = false
        includeInBundle = true
    }

    testOptions {
        unitTests.isReturnDefaultValues = false
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    // org.json is part of Android, but only as stubs in JVM tests.
    testImplementation("org.json:json:20240303")

    androidTestImplementation("androidx.test:core:1.7.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test:rules:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
