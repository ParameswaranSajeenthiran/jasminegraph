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

#include "StatisticCollector.h"

static clock_t lastCPU, lastSysCPU, lastUserCPU;
static int numProcessors;

int StatisticCollector::init() {
    FILE* file;
    struct tms timeSample;
    char line[128];

    lastCPU = times(&timeSample);
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

    file = fopen("/proc/cpuinfo", "r");
    numProcessors = 0;
    while (fgets(line, 128, file) != NULL) {
        if (strncmp(line, "processor", 9) == 0) numProcessors++;
    }
    fclose(file);
    return 0;
}

int StatisticCollector::getMemoryUsageByProcess() {
    FILE* file = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    std::cout << "Memory Usage: " + std::to_string(result) << std::endl;
    return result;
}

int StatisticCollector::parseLine(char* line) {
    int i = strlen(line);
    const char* p = line;
    while (*p < '0' || *p > '9') p++;
    line[i - 3] = '\0';
    i = atoi(p);
    return i;
}

double StatisticCollector::getCpuUsage() {
    struct tms timeSample;
    clock_t now;
    double percent;

    now = times(&timeSample);
    if (now <= lastCPU || timeSample.tms_stime < lastSysCPU || timeSample.tms_utime < lastUserCPU) {
        // Overflow detection. Just skip this value.
        percent = -1.0;
    } else {
        percent = (timeSample.tms_stime - lastSysCPU) + (timeSample.tms_utime - lastUserCPU);
        percent /= (now - lastCPU);
        percent /= numProcessors;
        percent *= 100;
    }
    lastCPU = now;
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

    return percent;
}

std::string StatisticCollector::collectVMStatistics(std::string isVMStatManager,
                                                    std::string isTotalAllocationRequired) {
    std::string vmLevelStatistics;

    if (isVMStatManager == "true") {
        long totalMemoryUsed = getTotalMemoryUsage();
        double totalCPUUsage = getTotalCpuUsage();

        std::stringstream stream;
        stream << std::fixed << std::setprecision(2) << totalCPUUsage;
        std::string cpuUsageString = stream.str();

        vmLevelStatistics = std::to_string(totalMemoryUsed) + "," + cpuUsageString + ",";
    }

    if (isTotalAllocationRequired == "true") {
        long totalMemory = getTotalMemoryAllocated();
        int totalCoresAvailable = getTotalNumberofCores();

        vmLevelStatistics = vmLevelStatistics + std::to_string(totalMemory) + "," + std::to_string(totalCoresAvailable);
    }

    return vmLevelStatistics;
}

long StatisticCollector::getTotalMemoryAllocated() {
    std::string token;
    std::ifstream file("/proc/meminfo");
    while (file >> token) {
        if (token == "MemTotal:") {
            unsigned long mem;
            if (file >> mem) {
                return mem;
            } else {
                return 0;
            }
        }
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0;
}

int StatisticCollector::getTotalNumberofCores() {
    unsigned concurentThreadsSupported = std::thread::hardware_concurrency();
    return concurentThreadsSupported;
}

long StatisticCollector::getTotalMemoryUsage() {
    std::string token;
    std::ifstream file("/proc/meminfo");
    unsigned long memTotal;
    unsigned long memFree;
    unsigned long buffers;
    unsigned long cached;
    unsigned long sReclaimable;
    unsigned long memUsage;
    while (file >> token) {
        if (token == "MemTotal:") {
            file >> memTotal;
        } else if (token == "MemFree:") {
            file >> memFree;
        } else if (token == "Buffers:") {
            file >> buffers;
        } else if (token == "Cached:") {
            file >> cached;
        } else if (token == "SReclaimable:") {
            file >> sReclaimable;
        }
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    memUsage = memTotal - (memFree + buffers + cached + sReclaimable);
    return memUsage;
}

double StatisticCollector::getTotalCpuUsage() {
    std::string mpstatCommand = "mpstat";
    char buffer[BUFFER_SIZE];
    std::string result = "";
    std::vector<std::string>::iterator paramNameIterator;
    int count = 0;
    double totalCPUUsage = 0;

    FILE* input = popen(mpstatCommand.c_str(), "r");

    if (input) {
        // read the input
        while (!feof(input)) {
            if (fgets(buffer, BUFFER_SIZE, input) != NULL) {
                result.append(buffer);
            }
        }
        if (!result.empty()) {
            std::vector<std::string> splittedStats = Utils::split(result, '\n');
            int length = splittedStats.size();
            std::string parameterNames = splittedStats[length - 2];
            std::string parameterValues = splittedStats[length - 1];
            std::vector<std::string> splittedParamNames = Utils::split(parameterNames, ' ');
            splittedParamNames.erase(std::remove(splittedParamNames.begin(), splittedParamNames.end(), ""),
                                     splittedParamNames.end());
            std::vector<std::string> splittedParamValues = Utils::split(parameterValues, ' ');
            splittedParamValues.erase(std::remove(splittedParamValues.begin(), splittedParamValues.end(), ""),
                                      splittedParamValues.end());

            for (paramNameIterator = splittedParamNames.begin(); paramNameIterator != splittedParamNames.end();
                 ++paramNameIterator) {
                std::string paramName = *paramNameIterator;

                if (paramName.find("%") != std::string::npos && paramName.find("idle") == std::string::npos) {
                    std::string paramValue = splittedParamValues[count];
                    double paramCPUUsage = std::stof(paramValue.c_str());
                    paramCPUUsage = round(paramCPUUsage * 100) / 100;
                    totalCPUUsage = totalCPUUsage + paramCPUUsage;
                }

                count++;
            }
        }
        pclose(input);
    }

    return totalCPUUsage;
}

double StatisticCollector::getLoadAverage() {
    double averages[3];

    getloadavg(averages, 3);

    return averages[0];
}

void StatisticCollector::logLoadAverage(std::string name) {
    PerformanceUtil::logLoadAverage();

    int elapsedTime = 0;
    time_t start;
    time_t end;
    PerformanceUtil performanceUtil;
    performanceUtil.init();

    start = time(0);

    while (true) {
        if (isStatCollect) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue;
        }

        time_t elapsed = time(0) - start;
        if (elapsed >= Conts::LOAD_AVG_COLLECTING_GAP) {
            elapsedTime += Conts::LOAD_AVG_COLLECTING_GAP * 1000;
            PerformanceUtil::logLoadAverage();
            start = start + Conts::LOAD_AVG_COLLECTING_GAP;
        } else {
            sleep(Conts::LOAD_AVG_COLLECTING_GAP - elapsed);
        }
    }
}
