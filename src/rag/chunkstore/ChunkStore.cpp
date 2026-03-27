#include "ChunkStore.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../../util/logger/Logger.h"

namespace fs = std::filesystem;
Logger chunk_store_logger;

std::string ChunkStore::getChunkFileName(int graphId, int partitionId, int chunkId) const {
    return baseDir + "/g" + std::to_string(graphId) +
           "_p" + std::to_string(partitionId) +
           "_chunk" + std::to_string(chunkId);
}

ChunkStore::ChunkStore(const std::string& dir) : baseDir(dir) {
    if (!fs::exists(baseDir)) {
        fs::create_directories(baseDir);
    }
}

void ChunkStore::saveChunk(int graphId, int partitionId, int chunkId, const std::string& content) {
    std::lock_guard<std::mutex> lock(fileMutex);

    std::string filePath = getChunkFileName(graphId, partitionId, chunkId);
    std::string tmpPath  = filePath + ".tmp";

    // ---- 1. Write to temp file ----
    std::ofstream out(tmpPath, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open chunk file for writing: " + tmpPath);
    }

    out.write(content.data(), content.size());
    out.flush();
    out.close();

    // ---- 2. fsync (important for crash safety) ----
    int fd = open(tmpPath.c_str(), O_RDWR);
    if (fd == -1) {
        throw std::runtime_error("Failed to open temp file for fsync");
    }
    fsync(fd);
    close(fd);

    // ---- 3. Atomic rename ----
    fs::rename(tmpPath, filePath);
}

std::string ChunkStore::readChunk(int graphId, int partitionId, int chunkId) {
    std::lock_guard<std::mutex> lock(fileMutex);

    std::string filePath = getChunkFileName(graphId, partitionId, chunkId);

    if (!fs::exists(filePath)) {
        return "";
    }

    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
      chunk_store_logger.error("Failed to open chunk file for reading: " + filePath);
        return "";
    }

    std::streamsize size = in.tellg();
    in.seekg(0);

    std::string content(size, '\0');
    if (size > 0) {
        in.read(&content[0], size);
    }

    return content;
}

bool ChunkStore::chunkExists(int graphId, int partitionId, int chunkId) {
    return fs::exists(getChunkFileName(graphId, partitionId, chunkId));
}

void ChunkStore::deleteChunk(int graphId, int partitionId, int chunkId) {
    std::lock_guard<std::mutex> lock(fileMutex);
    fs::remove(getChunkFileName(graphId, partitionId, chunkId));
}