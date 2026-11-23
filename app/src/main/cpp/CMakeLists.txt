cmake_minimum_required(VERSION 3.22)

project(XenonAndroid)

set(XBOXEMU_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../)
cmake_path(NORMAL_PATH XBOXEMU_ROOT)

add_subdirectory(${XBOXEMU_ROOT}/Core Core)

set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC ${NATIVE_APP_GLUE_DIR})

add_library(xenon_native SHARED
    xenon_native.cpp
    ProfileSelectionScreen.cpp
    SplashScreen.cpp
)

# The NDK glue exports ANativeActivity_onCreate from a static library. Without
# forcing it to be kept, the linker drops it and Android crashes at launch.
target_link_options(xenon_native PRIVATE "-u" "ANativeActivity_onCreate")

target_include_directories(xenon_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XBOXEMU_ROOT}
)

target_link_libraries(xenon_native
    PRIVATE
        xboxemu_cpu
        xboxemu_filesystem
        xboxemu_memory
        native_app_glue
        android
        EGL
        GLESv2
        log
)
