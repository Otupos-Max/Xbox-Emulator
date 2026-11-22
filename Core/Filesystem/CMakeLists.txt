add_library(xboxemu_filesystem
    XdvdfsImage.cpp
)

target_include_directories(xboxemu_filesystem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)