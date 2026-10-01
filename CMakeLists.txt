cmake_minimum_required(VERSION 3.16)
project(Yansitgac VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK2 REQUIRED IMPORTED_TARGET gtk+-2.0)

# Statik minimal FFmpeg (scripts/ffmpeg-derle.sh ile üretilir).
set(FFMPEG_DIZINI ${CMAKE_SOURCE_DIR}/third_party/ffmpeg CACHE PATH "Statik FFmpeg kurulum dizini")
if(NOT EXISTS ${FFMPEG_DIZINI}/lib/libavcodec.a)
    message(FATAL_ERROR "FFmpeg yok. Önce: ./scripts/ffmpeg-derle.sh")
endif()

# Telefon uygulamasının versionCode'u: PC, telefondaki sürüm bundan eskiyse APK'yı yeniden kurar.
set(ANDROID_DIZINI "${CMAKE_SOURCE_DIR}/Yansıtgaç-Android")
file(STRINGS "${ANDROID_DIZINI}/app/build.gradle.kts" SURUM_SATIRI REGEX "versionCode *= *[0-9]+")
string(REGEX MATCH "[0-9]+" APK_SURUM "${SURUM_SATIRI}")
if(NOT APK_SURUM)
    message(FATAL_ERROR "Yansıtgaç-Android/app/build.gradle.kts içinde versionCode bulunamadı")
endif()

add_executable(yansitgac
    src/main.cpp
    src/oturum.cpp
    src/adb.cpp
    src/cozucu.cpp
    src/ses.cpp
)
target_include_directories(yansitgac PRIVATE ${FFMPEG_DIZINI}/include)
target_link_libraries(yansitgac PRIVATE
    PkgConfig::GTK2
    ${FFMPEG_DIZINI}/lib/libavcodec.a
    ${FFMPEG_DIZINI}/lib/libswscale.a
    ${FFMPEG_DIZINI}/lib/libavutil.a
    pthread m ${CMAKE_DL_LIBS}
)
target_compile_definitions(yansitgac PRIVATE
    YANSITGAC_APK_SURUM=${APK_SURUM}
    # Geliştirme sırasında Gradle çıktısı doğrudan kullanılsın.
    YANSITGAC_GELISTIRME_APK="${ANDROID_DIZINI}/app/build/outputs/apk/release/app-release.apk"
)
target_compile_options(yansitgac PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
# Statik libstdc++/libgcc: AppImage'ın farklı dağıtımlarda çalışmasını kolaylaştırır.
target_link_options(yansitgac PRIVATE -static-libstdc++ -static-libgcc -Wl,--as-needed)
