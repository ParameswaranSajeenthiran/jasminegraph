/**
Copyright 2025 JasmineGraph Team
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 */
#include "FaissIndex.h"

#include <faiss/IndexFlat.h>
#include <faiss/IndexIDMap.h>
#include <faiss/index_io.h>
#include <faiss/IndexIVFPQ.h>
#include <filesystem>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>     // open
#include <unistd.h>    // fsync, close
#include <sys/stat.h>  // stat
#include "../frontend/core/executor/impl/StreamingTriangleCountExecutor.h"
#include "../util/logger/Logger.h"

// Static members
std::unique_ptr<FaissIndex> FaissIndex::instance = nullptr;
std::once_flag FaissIndex::initFlag;
Logger faiss_index_logger;
std::unordered_map<std::string, std::unique_ptr<FaissIndex>> FaissIndex::instances;

std::mutex FaissIndex::instancesMutex;
FaissIndex* FaissIndex::getInstance(int embeddingDim,
                                    const std::string& filepath) {
    std::lock_guard<std::mutex> lock(instancesMutex);

    auto it = instances.find(filepath);
    if (it != instances.end()) {
        return it->second.get();
    }

    FaissIndex* index = new FaissIndex (embeddingDim, filepath);
    instances.emplace(filepath, index);
    return index;
}
FaissIndex::FaissIndex(int embeddingDim, const std::string& filepath)
    : dim(embeddingDim), filePath(filepath) {
  load(filepath);
}

// FaissIndex::~FaissIndex() {
//   try {
//     faiss_index_logger.info("saving FAISS index from destructor");
//     save(filePath);
//   } catch (const std::exception& e) {
//     faiss_index_logger.error("[FaissIndex] Failed to auto-save index: " +
//                              std::string(e.what()));
//   }
//   delete index;
// }

faiss::idx_t FaissIndex::add(const std::vector<float>& embedding,
                             std::string nodeId) {
  if (embedding.size() != dim) {
    throw std::runtime_error("Embedding dimension mismatch!");
  }
    try {
        std::lock_guard<std::mutex> lock(mtx);

        faiss::idx_t new_id = index->ntotal;
        faiss_index_logger.debug("[FaissIndex] Adding new embedding with nodeId: " +
                                 nodeId + ", assigned id: " + std::to_string(new_id));

        index->add(1, embedding.data());

        faiss_index_logger.debug(
            "[FaissIndex] Embedding added to index. Updating nodeEmbeddingMap.");
        nodeIdToEmbeddingIdMap[nodeId] = new_id;
        embeddingIdToNodeIdMap[new_id] = nodeId;

        // if (index->ntotal % 1000 == 0) {
        //     // lock.unlock();
        //     faiss_index_logger.info("saving faiss index periodically");
        //     save(filePath);
        //     faiss_index_logger.info("saved faiss index periodically");
        //
        // }

        return new_id;
    } catch (const std::exception& e) {
       faiss_index_logger.error(std::string("Failed to reconstruct embedding for ID ") + nodeId + ": " +
           e.what());
        throw std::runtime_error("Failed to reconstruct embedding for ID " + nodeId);
    }
}

std::vector<std::pair<faiss::idx_t, float>> FaissIndex::search(
    const std::vector<float>& query, int k) {
  if (query.size() != dim) {
    throw std::runtime_error("Query dimension mismatch!");
  }

  std::vector<faiss::idx_t> indices(k);
  std::vector<float> distances(k);
  std::lock_guard<std::mutex> lock(mtx);
  index->search(1, query.data(), k, distances.data(), indices.data());
  std::vector<std::pair<faiss::idx_t, float>> results;
  for (int i = 0; i < k; i++) {
    results.emplace_back(indices[i], distances[i]);
  }
  return results;
}
void FaissIndex::save(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(fileMtx);
    namespace fs = std::filesystem;

    fs::path basePath(filepath);
    fs::path dir = basePath.parent_path();
    fs::path filename = basePath.filename();

    // handle case: no directory (e.g., "index.faiss")
    if (dir.empty()) {
        dir = ".";
    }
    // ---- 6. Remove old backups (keep only latest) ----
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();

        if (name.find(filename.string() + ".batch_") != std::string::npos &&
            name.find(".bak") != std::string::npos) {
            fs::remove(entry.path());
            }
    }
    faiss_index_logger.info("Saving FAISS index to file: " + filepath);
    faiss_index_logger.info("Index size: " + std::to_string(index->ntotal));
    faiss_index_logger.info("Map size: " + std::to_string(nodeIdToEmbeddingIdMap.size()));

    // ---- 1. Create unique batch temp file ----
    std::string tmpIndex = filepath + ".batch_" + std::to_string(std::time(nullptr));
    std::string tmpMap   = tmpIndex + ".map";

    // ---- 2. Write FAISS index to temp ----
    faiss::write_index(index, tmpIndex.c_str());

    // ---- 3. fsync index file ----
    int fd = open(tmpIndex.c_str(), O_RDWR);
    if (fd == -1) {
        throw std::runtime_error("Failed to open temp index file for fsync");
    }
    fsync(fd);
    close(fd);

    // ---- 4. Write map file ----
    std::ofstream mapFile(tmpMap, std::ios::binary);
    if (!mapFile.is_open()) {
        throw std::runtime_error("Failed to open temp map file for saving.");
    }

    size_t size = nodeIdToEmbeddingIdMap.size();
    mapFile.write(reinterpret_cast<const char*>(&size), sizeof(size));

    for (const auto& entry : nodeIdToEmbeddingIdMap) {
        size_t keyLen = entry.first.size();
        mapFile.write(reinterpret_cast<const char*>(&keyLen), sizeof(keyLen));
        mapFile.write(entry.first.data(), keyLen);
        mapFile.write(reinterpret_cast<const char*>(&entry.second),
                      sizeof(entry.second));
    }
    mapFile.flush();

    int mapFd = open(tmpMap.c_str(), O_RDWR);
    fsync(mapFd);
    close(mapFd);

    mapFile.close();

    // ---- 5. Atomic replace (VERY IMPORTANT) ----
    rename(tmpIndex.c_str(), filepath.c_str());
    rename(tmpMap.c_str(), (filepath + ".map").c_str());

    // ---- 6. IMPORTANT: Keep batch files ----
    // Since rename moved them, we need to ALSO keep a copy

    std::string batchIndexBackup = tmpIndex + ".bak";
    std::string batchMapBackup   = tmpMap + ".bak";

    // Copy current main file back as batch archive
    std::ifstream srcIdx(filepath, std::ios::binary);
    std::ofstream dstIdx(batchIndexBackup, std::ios::binary);
    dstIdx << srcIdx.rdbuf();

    std::ifstream srcMap(filepath + ".map", std::ios::binary);
    std::ofstream dstMap(batchMapBackup, std::ios::binary);
    dstMap << srcMap.rdbuf();

    faiss_index_logger.info("Save completed safely with batch backup.");
}

void FaissIndex::save() {
    try {
        save(filePath);

} catch (const std::exception& e) {
    faiss_index_logger.error(std::string("Failed to save index: ") + e.what());
}
}
void FaissIndex::load(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mtx);

    faiss_index_logger.info("Loading FAISS index from: " + filepath);

    bool loadedSuccessfully = false;

    // ---- 1. Try MAIN file ----
    try {
        std::ifstream f(filepath, std::ios::binary);
        if (f.good()) {
            faiss::Index* loaded = faiss::read_index(filepath.c_str());
            index = dynamic_cast<faiss::IndexFlatL2*>(loaded);

            if (!index) throw std::runtime_error("Invalid index type");

            faiss_index_logger.info("Loaded MAIN index successfully");
            loadedSuccessfully = true;
        }
    } catch (const std::exception& e) {
        faiss_index_logger.warn("Main index corrupted. Trying backup...");
        faiss_index_logger.error(e.what());
    }

    // ---- 2. Try BACKUP (.bak) ----
    if (!loadedSuccessfully) {
        std::string backupFile = getLatestBackupFile(filepath, true);

        if (!backupFile.empty()) {
            try {
                faiss::Index* loaded = faiss::read_index(backupFile.c_str());
                index = dynamic_cast<faiss::IndexFlatL2*>(loaded);

                if (!index) throw std::runtime_error("Invalid backup index");

                faiss_index_logger.info("Recovered from BACKUP: " + backupFile);
                loadedSuccessfully = true;
            } catch (const std::exception& e) {
                faiss_index_logger.warn("Backup also failed.");
                faiss_index_logger.error(e.what());
            }
        }
    }

    // ---- 3. Fallback → new index ----
    if (!loadedSuccessfully) {
        faiss_index_logger.error("All recovery failed. Creating new index.");
        index = new faiss::IndexFlatL2(dim);
    }

    // ---- 4. Load mapping (same logic with fallback) ----
    auto loadMap = [&](const std::string& mapPath) -> bool {
        std::ifstream mapFile(mapPath, std::ios::binary);
        if (!mapFile.is_open()) return false;

        size_t size = 0;
        if (!mapFile.read(reinterpret_cast<char*>(&size), sizeof(size))) {
            return false;
        }

        for (size_t i = 0; i < size; i++) {
            size_t keyLen = 0;
            if (!mapFile.read(reinterpret_cast<char*>(&keyLen), sizeof(keyLen)))
                return false;

            if (keyLen == 0 || keyLen > 1024) return false;

            std::string key(keyLen, '\0');
            if (!mapFile.read(&key[0], keyLen)) return false;

            key.resize(strnlen(key.c_str(), keyLen));

            faiss::idx_t value;
            if (!mapFile.read(reinterpret_cast<char*>(&value), sizeof(value)))
                return false;

            nodeIdToEmbeddingIdMap[key] = value;
            embeddingIdToNodeIdMap[value] = key;
        }

        return true;
    };

    // Try main map
    if (!loadMap(filepath + ".map")) {
        faiss_index_logger.warn("Main map failed. Trying backup map...");

        std::string backupFile = getLatestBackupFile(filepath, false);
        if (!backupFile.empty()) {
            loadMap(backupFile + ".map");
        }
    }

    faiss_index_logger.info("Load completed.");
}

bool FaissIndex::isEmbeddingExist(std::string nodeId) {
    std::lock_guard<std::mutex> lock(mtx);

    if (!index) {
        throw std::runtime_error("FAISS index not initialized.");
    }

    try {
        return  nodeIdToEmbeddingIdMap.find(nodeId) != nodeIdToEmbeddingIdMap.end();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to reconstruct embedding for ID ") + nodeId + ": " +
            e.what());
    }
}
std::vector<float> FaissIndex::getEmbeddingById(std::string nodeId) {
  std::lock_guard<std::mutex> lock(mtx);

  if (!index) {
    throw std::runtime_error("FAISS index not initialized.");
  }

  // Allocate vector for reconstructed embedding
  std::vector<float> embedding(dim);

  try {
    // FAISS reconstruct expects the **internal index position**, not
    // necessarily the ID If using IndexIDMap, reconstruct the vector for a
    // given ID
    if (nodeIdToEmbeddingIdMap.find(nodeId) == nodeIdToEmbeddingIdMap.end()) {
      return embedding;  // Return empty vector if nodeId not found
    }
    faiss::idx_t id = nodeIdToEmbeddingIdMap.at(nodeId.c_str());
    if (id < 0 || id >= index->ntotal) {
      throw std::out_of_range("ID out of range in FAISS index.");
    }
    index->reconstruct(id, embedding.data());
  } catch (const std::exception& e) {
    throw std::runtime_error(
        std::string("Failed to reconstruct embedding for ID ") + nodeId + ": " +
        e.what());
  }

  return embedding;
}

std::string FaissIndex::getNodeIdFromEmbeddingId(faiss::idx_t embeddingId) {
  auto it = embeddingIdToNodeIdMap.find(embeddingId);
  if (it == embeddingIdToNodeIdMap.end()) {
    throw std::runtime_error("Node ID not found for embedding ID: " +
                             std::to_string(embeddingId));
  }

  return it->second;  // access the nodeId from the right map
}

std::vector<std::vector<float>>
FaissIndex::getEmbeddingsByIds(const std::vector<std::string>& nodeIds) {
    std::lock_guard<std::mutex> lock(mtx);

    if (!index) {
        throw std::runtime_error("FAISS index not initialized.");
    }

    std::vector<std::vector<float>> results;
    results.reserve(nodeIds.size());

    for (const auto& nodeId : nodeIds) {
        auto it = nodeIdToEmbeddingIdMap.find(nodeId);
        if (it == nodeIdToEmbeddingIdMap.end()) {
            // push empty embedding or skip — your choice
            results.emplace_back(dim, 0.0f);
            continue;
        }

        faiss::idx_t id = it->second;
        if (id < 0 || id >= index->ntotal) {
            results.emplace_back(dim, 0.0f);
            continue;
        }

        std::vector<float> emb(dim);
        index->reconstruct(id, emb.data());
        results.emplace_back(std::move(emb));
    }

    return results;
}

std::string FaissIndex::getLatestBackupFile(const std::string& basePath, bool isIndex) {
    namespace fs = std::filesystem;

    fs::path base(basePath);
    fs::path dir = base.parent_path();
    std::string filename = base.filename().string();

    std::vector<std::pair<long, fs::path>> candidates;

    if (!fs::exists(dir)) return "";

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();

        // Must match base + batch
        if (name.find(filename) == std::string::npos ||
            name.find(".batch_") == std::string::npos) {
            continue;
            }

        // 🔥 Filter by type
        if (isIndex) {
            // Only: *.bak (but NOT .map.bak)
            if (!(Utils::endsWith(name, ".bak") && name.find(".map") == std::string::npos)) {
                continue;
            }
        } else {
            // Only: *.map.bak
            if (!Utils::endsWith(name, ".map.bak")) {
                continue;
            }
        }

        // Extract timestamp
        size_t pos = name.find(".batch_");
        if (pos == std::string::npos) continue;

        pos += 7;
        size_t end = name.find('.', pos);

        long ts = -1;
        try {
            ts = std::stol(name.substr(pos, end - pos));
        } catch (...) {
            continue;
        }

        candidates.emplace_back(ts, entry.path());
    }

    if (candidates.empty()) return "";

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });

    return candidates.back().second.string();
}
