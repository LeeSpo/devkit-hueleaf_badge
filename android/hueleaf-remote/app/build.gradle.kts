plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseKeystore = System.getenv("HUELEAF_ANDROID_KEYSTORE")

android {
    namespace = "com.hueleaf.remote"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.hueleaf.remote"
        minSdk = 23
        targetSdk = 35
        versionCode = providers.gradleProperty("hueleafVersionCode").orElse("1").get().toInt()
        versionName = providers.gradleProperty("hueleafVersionName").orElse("1.0.0").get()
        testInstrumentationRunner = "com.hueleaf.remote.UiSmokeInstrumentation"
    }
    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storePassword = System.getenv("HUELEAF_ANDROID_STORE_PASSWORD")
                keyAlias = System.getenv("HUELEAF_ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("HUELEAF_ANDROID_KEY_PASSWORD")
            }
        }
    }
    buildTypes {
        getByName("release") {
            if (releaseKeystore != null) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }
    buildFeatures { viewBinding = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.recyclerview:recyclerview:1.1.0")
    implementation("androidx.viewpager2:viewpager2:1.0.0")
    testImplementation("junit:junit:4.13.2")
}
