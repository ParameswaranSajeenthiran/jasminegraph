/**
Copyright 2019 JasmineGraph Team
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
#ifndef JASMINEGRAPH_UTILS_H
#define JASMINEGRAPH_UTILS_H

#include <arpa/inet.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../metadb/SQLiteDBInterface.h"
#include "../performancedb/PerformanceSQLiteDBInterface.h"
#include "Conts.h"

using std::map;
using std::unordered_map;

class Utils {
 private:
    static unordered_map<std::string, std::string> propertiesMap;

 public:
    struct worker {
        std::string workerID;
        std::string hostname;
        std::string username;
        std::string port;
        std::string dataPort;
    };

    static std::string getJasmineGraphProperty(std::string key);

    static std::vector<worker> getWorkerList(SQLiteDBInterface sqlite);

    static std::vector<std::string> getHostListFromProperties();

    static std::vector<std::string> getFileContent(std::string);

    static std::vector<std::string> split(const std::string &, char delimiter);

    static std::string trim_copy(const std::string &, const std::string &);

    static bool parseBoolean(const std::string str);

    static bool fileExists(std::string fileName);

    static int compressFile(const std::string filePath, std::string mode = "pigz");

    static bool is_number(const std::string &compareString);

    static void createDirectory(const std::string dirName);

    static std::vector<std::string> getListOfFilesInDirectory(const std::string dirName);

    static int deleteDirectory(const std::string dirName);

    static std::string getFileName(std::string filePath);

    static int getFileSize(std::string filePath);

    static std::string getJasmineGraphHome();

    static std::string getHomeDir();

    static int copyFile(const std::string sourceFilePath, const std::string destinationFilePath);

    static int unzipFile(std::string filePath, std::string mode = "pigz");

    static bool hostExists(std::string name, std::string ip, std::string workerPort, SQLiteDBInterface sqlite);

    static int compressDirectory(const std::string filePath);

    static int unzipDirectory(std::string filePath);

    static int copyToDirectory(std::string currentPath, std::string copyPath);

    static std::string getHostID(std::string hostName, SQLiteDBInterface sqlite);

    static void assignPartitionsToWorkers(int numberOfWorkers, SQLiteDBInterface sqlite);

    static void updateSLAInformation(PerformanceSQLiteDBInterface perfSqlite, std::string graphId, int partitionCount,
                                     long newSlaValue, std::string command, std::string category);

    static void editFlagZero(std::string flagPath);

    static void editFlagOne(std::string flagPath);

    static std::string checkFlag(std::string flagPath);

    static int connect_wrapper(int sock, const sockaddr *addr, socklen_t slen);
    static std::string getCurrentTimestamp();
};

#endif  // JASMINEGRAPH_UTILS_H
