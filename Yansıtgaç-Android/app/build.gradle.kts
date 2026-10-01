plugins {
    id("com.android.application")
}

val imzaSifresi: String = System.getenv("YANSITGAC_IMZA_SIFRESI") ?: "yansitgac"

fun imzaAnahtari(): File {
    val anahtar = rootProject.file("imza/yansitgac.jks")
    if (!anahtar.exists()) {
        anahtar.parentFile.mkdirs()
        val keytool = File(System.getProperty("java.home"), "bin/keytool").path
        val kod = ProcessBuilder(
            keytool, "-genkeypair", "-keystore", anahtar.path, "-alias", "yansitgac",
            "-storepass", imzaSifresi, "-keypass", imzaSifresi, "-keyalg", "RSA",
            "-keysize", "2048", "-validity", "20000", "-dname", "CN=Yansitgac"
        ).inheritIO().start().waitFor()
        check(kod == 0) { "İmza anahtarı üretilemedi (keytool çıkış kodu $kod)" }
        logger.warn("Yeni imza anahtarı üretildi: $anahtar — yedekleyin.")
    }
    return anahtar
}

android {
    namespace = "com.yansitgac.android"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.yansitgac.android"
        minSdk = 24
        targetSdk = 36
        // PC programı telefondaki versionCode bundan eskiyse APK'yı yeniden kurar
        // (CMakeLists.txt bu satırı okur). Protokol/davranış değişince artır.
        versionCode = 5
        versionName = "0.5.0"
    }

    signingConfigs {
        // İmza anahtarı depoda DEĞİL (.gitignore). Resmi sürümler yayıncının sakladığı anahtarla
        // imzalanır; anahtar yoksa (başkası derliyorsa) aşağıda otomatik üretilir. Farklı imzalı bir
        // sürüm telefonda kuruluysa PC programı onu kaldırıp yeniden kurar.
        create("yansitgac") {
            storeFile = imzaAnahtari()
            storePassword = imzaSifresi
            keyAlias = "yansitgac"
            keyPassword = imzaSifresi
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("yansitgac")
        }
        debug {
            signingConfig = signingConfigs.getByName("yansitgac")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
