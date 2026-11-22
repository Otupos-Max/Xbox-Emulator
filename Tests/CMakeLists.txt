include(FetchContent)

# ---------------------------------------------------------------------------
# GoogleTest
# ---------------------------------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Android: não usar pthreads do host
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper: cria um executável de teste a partir de uma pasta de fontes
# ---------------------------------------------------------------------------
function(xboxemu_add_test NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${NAME} ${ARG_SOURCES})
    target_link_libraries(${NAME}
        PRIVATE
            GTest::gtest_main
            ${ARG_LIBS}
    )
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR})

    gtest_discover_tests(${NAME}
        PROPERTIES
            TIMEOUT 30
    )
endfunction()

# ---------------------------------------------------------------------------
# Testes por módulo
# ---------------------------------------------------------------------------

# PPC
xboxemu_add_test(test_ppc_decoder_smoke
    SOURCES PPC/test_decoder_smoke.cpp
    LIBS    xboxemu_cpu
)

xboxemu_add_test(test_xdvdfs_image
    SOURCES Filesystem/test_xdvdfs_image.cpp
    LIBS    xboxemu_filesystem
)

# Memory
xboxemu_add_test(test_memory_ram_read_write
    SOURCES Memory/test_ram_read_write.cpp
    LIBS    xboxemu_memory
)

xboxemu_add_test(test_memory_ram_endianness
    SOURCES Memory/test_ram_endianness.cpp
    LIBS    xboxemu_memory
)

xboxemu_add_test(test_memory_mmio_dispatch
    SOURCES Memory/test_mmio_dispatch.cpp
    LIBS    xboxemu_memory
)
