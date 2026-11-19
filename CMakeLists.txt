cmake_minimum_required(VERSION 3.22)

project(XboxEmulator
    VERSION 0.1.0
    DESCRIPTION "Experimental Xbox 360 emulator for Android"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Padrão C++
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Flags de compilação
# ---------------------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Build type default
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo"
        CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Detecção de Android NDK (opcional — permite compilar no host também)
# ---------------------------------------------------------------------------
if (ANDROID)
    message(STATUS "Building for Android (ABI: ${ANDROID_ABI}, API: ${ANDROID_PLATFORM})")
    add_compile_definitions(XBOXEMU_PLATFORM_ANDROID=1)
else()
    message(STATUS "Building for host platform")
    add_compile_definitions(XBOXEMU_PLATFORM_HOST=1)
endif()

# ---------------------------------------------------------------------------
# Módulos
# ---------------------------------------------------------------------------
# Adicione aqui à medida que os módulos forem criados:
# add_subdirectory(Core)

# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------
option(XBOXEMU_BUILD_TESTS "Build unit tests" ON)

if (XBOXEMU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
