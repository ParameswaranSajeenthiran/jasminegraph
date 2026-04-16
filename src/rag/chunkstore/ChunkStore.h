/**
Copyright 2025 JasmineGraph Team
Licensed under the Apache License, Version 2.0
*/

#pragma once
#include <string>
#include <mutex>

class ChunkStore {
    private:
        std::string baseDir;
        std::mutex fileMutex;

        std::string getChunkFileName(int graphId, int partitionId, int chunkId) const;

    public:
        explicit ChunkStore(const std::string& dir);

        void saveChunk(int graphId, int partitionId, int chunkId, const std::string& content);

        std::string readChunk(int graphId, int partitionId, int chunkId);

        bool chunkExists(int graphId, int partitionId, int chunkId);

        void deleteChunk(int graphId, int partitionId, int chunkId);
};