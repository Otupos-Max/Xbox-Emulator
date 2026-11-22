#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xboxemu::filesystem {

struct XdvdfsFile {
    std::string path;
    uint64_t offset = 0;
    uint32_t size = 0;
    bool directory = false;
};

class XdvdfsImage {
public:
    bool open(const std::string& path, std::string* error = nullptr);
    bool findFile(const std::string& path, XdvdfsFile& result) const;
    bool readFile(const std::string& path, std::vector<uint8_t>& data,
                  std::string* error = nullptr) const;
    bool readFilePrefix(const std::string& path, size_t maxBytes,
                        std::vector<uint8_t>& data, std::string* error = nullptr) const;

    uint64_t gameOffset() const { return gameOffset_; }

private:
    bool readAt(uint64_t offset, void* data, size_t size) const;
    bool parseDirectory(uint32_t sector, uint32_t size, const std::string& parent,
                        unsigned depth, std::string* error);
    bool parseNode(const std::vector<uint8_t>& directory, uint16_t ordinal,
                   const std::string& parent, unsigned depth,
                   std::unordered_map<uint16_t, bool>& visited, std::string* error);
    static std::string normalizePath(const std::string& path);
    static uint32_t readLe32(const uint8_t* data);
    static uint16_t readLe16(const uint8_t* data);

    mutable std::ifstream image_;
    uint64_t imageSize_ = 0;
    uint64_t gameOffset_ = 0;
    size_t entriesVisited_ = 0;
    std::unordered_map<std::string, XdvdfsFile> files_;
};

} // namespace xboxemu::filesystem