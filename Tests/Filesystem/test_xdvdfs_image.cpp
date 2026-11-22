#include <gtest/gtest.h>

#include "Core/Filesystem/XdvdfsImage.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kSectorSize = 2048;

void writeLe32(std::vector<uint8_t>& image, size_t offset, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
        image[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
}

std::filesystem::path createImage(bool corruptRoot = false) {
    std::vector<uint8_t> image(35 * kSectorSize, 0);
    const size_t descriptor = 32 * kSectorSize;
    constexpr char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::copy(magic, magic + sizeof(magic) - 1, image.begin() + descriptor);
    writeLe32(image, descriptor + 20, corruptRoot ? 100 : 33);
    writeLe32(image, descriptor + 24, 64);

    const size_t rootEntry = 33 * kSectorSize;
    writeLe32(image, rootEntry + 4, 34);
    writeLe32(image, rootEntry + 8, 4);
    image[rootEntry + 12] = 0;
    constexpr char fileName[] = "default.xex";
    image[rootEntry + 13] = static_cast<uint8_t>(sizeof(fileName) - 1);
    std::copy(fileName, fileName + sizeof(fileName) - 1, image.begin() + rootEntry + 14);
    image[34 * kSectorSize] = 'X';
    image[34 * kSectorSize + 1] = 'E';
    image[34 * kSectorSize + 2] = 'X';
    image[34 * kSectorSize + 3] = '2';

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
            / ("xenon-xdvdfs-" + std::to_string(unique) + ".iso");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

std::filesystem::path createNestedImage() {
    std::vector<uint8_t> image(36 * kSectorSize, 0);
    const size_t descriptor = 32 * kSectorSize;
    constexpr char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::copy(magic, magic + sizeof(magic) - 1, image.begin() + descriptor);
    writeLe32(image, descriptor + 20, 33);
    writeLe32(image, descriptor + 24, 64);

    const size_t rootEntry = 33 * kSectorSize;
    writeLe32(image, rootEntry + 4, 34);
    writeLe32(image, rootEntry + 8, 64);
    image[rootEntry + 12] = 0x10;
    image[rootEntry + 13] = 4;
    std::copy("game", "game" + 4, image.begin() + rootEntry + 14);

    const size_t childEntry = 34 * kSectorSize;
    writeLe32(image, childEntry + 4, 35);
    writeLe32(image, childEntry + 8, 4);
    image[childEntry + 13] = 11;
    constexpr char fileName[] = "default.xex";
    std::copy(fileName, fileName + sizeof(fileName) - 1, image.begin() + childEntry + 14);
    std::copy("XEX2", "XEX2" + 4, image.begin() + 35 * kSectorSize);

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
            / ("xenon-xdvdfs-nested-" + std::to_string(unique) + ".iso");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

} // namespace

TEST(XdvdfsImage, FindsAndReadsRootFileCaseInsensitively) {
    const auto path = createImage();
    xboxemu::filesystem::XdvdfsImage image;
    std::string error;
    ASSERT_TRUE(image.open(path.string(), &error)) << error;

    xboxemu::filesystem::XdvdfsFile file;
    ASSERT_TRUE(image.findFile("/DEFAULT.XEX", file));
    EXPECT_EQ(file.size, 4u);
    EXPECT_FALSE(file.directory);

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(image.readFile("default.xex", bytes, &error)) << error;
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "XEX2");

    bytes.clear();
    ASSERT_TRUE(image.readFilePrefix("default.xex", 2, bytes, &error)) << error;
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "XE");
    std::filesystem::remove(path);
}

TEST(XdvdfsImage, RejectsInvalidAndOutOfBoundsRootDirectories) {
    const auto path = createImage(true);
    xboxemu::filesystem::XdvdfsImage image;
    std::string error;
    EXPECT_FALSE(image.open(path.string(), &error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(path);
}

TEST(XdvdfsImage, RejectsFilesOutsideImageBounds) {
    const auto path = createImage();
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        const size_t entrySize = 33 * kSectorSize + 8;
        file.seekp(static_cast<std::streamoff>(entrySize));
        const uint32_t invalidSize = 0xFFFFFFFFu;
        std::array<uint8_t, 4> encoded{};
        for (unsigned byte = 0; byte < 4; ++byte) encoded[byte] = static_cast<uint8_t>(invalidSize >> (byte * 8));
        file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }
    xboxemu::filesystem::XdvdfsImage image;
    std::string error;
    EXPECT_FALSE(image.open(path.string(), &error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(path);
}

TEST(XdvdfsImage, TraversesNestedDirectories) {
    const auto path = createNestedImage();
    xboxemu::filesystem::XdvdfsImage image;
    std::string error;
    ASSERT_TRUE(image.open(path.string(), &error)) << error;

    xboxemu::filesystem::XdvdfsFile file;
    ASSERT_TRUE(image.findFile("\\GAME\\DEFAULT.XEX", file));
    EXPECT_EQ(file.path, "game/default.xex");
    std::filesystem::remove(path);
}