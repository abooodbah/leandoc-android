plugins {
    id("com.android.application")
}

// An app with one empty activity and no libraries, measured next to the real
// app on the same emulator. Start time minus this app's start time is the
// number tracked, because the emulator's own start cost is large and varies
// between runners. It is never released.
android {
    namespace = "io.github.abooodbah.leandoc.baseline"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.github.abooodbah.leandoc.baseline"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
