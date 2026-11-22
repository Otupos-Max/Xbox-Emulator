#include "Core/Filesystem/XdvdfsImage.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace xboxemu::filesystem {
namespace {

constexpr uint64_t kSectorSize = 2048;
constexpr uint64_t kDescriptorSector = 32;
constexpr uint32_t kDirectoryAttribute = 0x10;
constexpr uint32_t kMaxDirectorySize = 32 * 1024 * 1024;
constexpr size_t kMaxEntries = 100000;
constexpr unsigned kMaxDirectoryDepth = 64;
constexpr char kVolumeMagic[] = "MICROSOFT*XBOX*MEDIA";

void setError(std::string* error, const char* message) {
    if (error) *error = message;
}

} // namespace

bool XdvdfsImage::open(const std::string& path, std::string* error) {
    image_.close();
    image_.clear();
    files_.clear();
    imageSize_ = 0;
    gameOffset_ = 0;
    entriesVisited_ = 0;

    image_.open(path, std::ios::binary);
    if (!image_) {
        setError(error, "Nao foi possivel abrir a imagem");
        return false;
    }
    image_.seekg(0, std::ios::end);
    const std::streamoff end = image_.tellg();
    if (end < 0) {
        setError(error, "Tamanho da imagem invalido");
        return false;
    }
    imageSize_ = static_cast<uint64_t>(end);

    constexpr std::array<uint64_t, 5> likelyOffsets = {
        0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000
    };
    std::array<uint8_t, 28> descriptor{};
    bool found = false;
    for (uint64_t base : likelyOffsets) {
        const uint64_t descriptorOffset = base + kDescriptorSector * kSectorSize;
        if (readAt(descriptorOffset, descriptor.data(), descriptor.size()) &&
            std::memcmp(descriptor.data(), kVolumeMagic, sizeof(kVolumeMagic) - 1) == 0) {
            gameOffset_ = base;
            found = true;
            break;
        }
    }
    if (!found) {
        setError(error, "Descritor XDVDFS nao encontrado");
        return false;
    }

    const uint32_t rootSector = readLe32(descriptor.data() + 20);
    const uint32_t rootSize = readLe32(descriptor.data() + 24);
    const uint64_t rootOffset = gameOffset_ + static_cast<uint64_t>(rootSector) * kSectorSize;
    if (rootSize < 14 || rootSize > kMaxDirectorySize ||
        rootOffset > imageSize_ || rootSize > imageSize_ - rootOffset) {
        setError(error, "Diretorio raiz XDVDFS fora dos limites");
        return false;
    }

    if (!parseDirectory(rootSector, rootSize, "", 0, error)) {
        files_.clear();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool XdvdfsImage::findFile(const std::string& path, XdvdfsFile& result) const {
    const auto found = files_.find(normalizePath(path));
    if (found == files_.end()) return false;
    result = found->second;
    return true;
}

bool XdvdfsImage::readFile(const std::string& path, std::vector<uint8_t>& data,
                           std::string* error) const {
    XdvdfsFile file;
    if (!findFile(path, file) || file.directory) {
        setError(error, "Arquivo XDVDFS nao encontrado");
        return false;
    }
    data.resize(file.size);
    if (file.size > 0 && !readAt(file.offset, data.data(), data.size())) {
        data.clear();
        setError(error, "Dados do arquivo XDVDFS truncados");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool XdvdfsImage::readFilePrefix(const std::string& path, size_t maxBytes,
                                 std::vector<uint8_t>& data, std::string* error) const {
    XdvdfsFile file;
    if (!findFile(path, file) || file.directory) {
        setError(error, "Arquivo XDVDFS nao encontrado");
        return false;
    }
    const size_t bytesToRead = std::min<size_t>(file.size, maxBytes);
    data.resize(bytesToRead);
    if (bytesToRead > 0 && !readAt(file.offset, data.data(), bytesToRead)) {
        data.clear();
        setError(error, "Cabecalho XDVDFS truncado");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool XdvdfsImage::readAt(uint64_t offset, void* data, size_t size) const {
    if (offset > imageSize_ || size > imageSize_ - offset ||
        offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    image_.clear();
    image_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!image_) return false;
    image_.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return image_.gcount() == static_cast<std::streamsize>(size);
}

bool XdvdfsImage::parseDirectory(uint32_t sector, uint32_t size, const std::string& parent,
                                 unsigned depth, std::string* error) {
    if (depth > kMaxDirectoryDepth || size < 14 || size > kMaxDirectorySize) {
        setError(error, "Estrutura de diretorio XDVDFS invalida");
        return false;
    }
    const uint64_t offset = gameOffset_ + static_cast<uint64_t>(sector) * kSectorSize;
    if (offset > imageSize_ || size > imageSize_ - offset) {
        setError(error, "Dados do diretorio XDVDFS fora da imagem");
        return false;
    }
    std::vector<uint8_t> directory(size);
    if (!readAt(offset, directory.data(), directory.size())) {
        setError(error, "Nao foi possivel ler o diretorio XDVDFS");
        return false;
    }
    std::unordered_map<uint16_t, bool> visited;
    return parseNode(directory, 0, parent, depth, visited, error);
}

bool XdvdfsImage::parseNode(const std::vector<uint8_t>& directory, uint16_t ordinal,
                            const std::string& parent, unsigned depth,
                            std::unordered_map<uint16_t, bool>& visited, std::string* error) {
    if (!visited.emplace(ordinal, true).second) {
        setError(error, "Ciclo na arvore de diretorio XDVDFS");
        return false;
    }
    if (++entriesVisited_ > kMaxEntries || depth > kMaxDirectoryDepth) {
        setError(error, "Diretorio XDVDFS excede os limites");
        return false;
    }

    const size_t entryOffset = static_cast<size_t>(ordinal) * 4;
    if (entryOffset > directory.size() || directory.size() - entryOffset < 14) {
        setError(error, "Entrada XDVDFS fora do diretorio");
        return false;
    }
    const uint8_t* entry = directory.data() + entryOffset;
    const uint16_t left = readLe16(entry);
    const uint16_t right = readLe16(entry + 2);
    const uint32_t sector = readLe32(entry + 4);
    const uint32_t size = readLe32(entry + 8);
    const uint8_t attributes = entry[12];
    const uint8_t nameLength = entry[13];
    if (nameLength == 0 || nameLength > directory.size() - entryOffset - 14) {
        setError(error, "Nome de arquivo XDVDFS invalido");
        return false;
    }
    const std::string name(reinterpret_cast<const char*>(entry + 14), nameLength);
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name == "." || name == "..") {
        setError(error, "Nome de arquivo XDVDFS inseguro");
        return false;
    }

    if (left != 0 && !parseNode(directory, left, parent, depth, visited, error)) return false;

    const std::string fullPath = parent.empty() ? name : parent + "/" + name;
    const bool isDirectory = (attributes & kDirectoryAttribute) != 0;
    const uint64_t dataOffset = gameOffset_ + static_cast<uint64_t>(sector) * kSectorSize;
    if (!isDirectory && (dataOffset > imageSize_ || size > imageSize_ - dataOffset)) {
        setError(error, "Arquivo XDVDFS aponta para fora da imagem");
        return false;
    }
    files_[normalizePath(fullPath)] = XdvdfsFile{fullPath, dataOffset, size, isDirectory};

    if (isDirectory && size > 0 && !parseDirectory(sector, size, fullPath, depth + 1, error)) {
        return false;
    }
    if (right != 0 && !parseNode(directory, right, parent, depth, visited, error)) return false;
    return true;
}

std::string XdvdfsImage::normalizePath(const std::string& path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (unsigned char ch : path) {
        if (ch == '/') normalized.push_back('/');
        else if (ch == '\\') normalized.push_back('/');
        else normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    return normalized;
}

uint32_t XdvdfsImage::readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t XdvdfsImage::readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

} // namespace xboxemu::filesystem