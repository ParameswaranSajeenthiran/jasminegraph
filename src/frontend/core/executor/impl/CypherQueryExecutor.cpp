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
#include <fstream>

#include "CypherQueryExecutor.h"
#include "antlr4-runtime.h"
#include "../../../../../src/query/processor/cypher/astbuilder/ASTBuilder.h"
#include "../../../../../src/query/processor/cypher/astbuilder/ASTNode.h"
#include "../../../../../src/query/processor/cypher/semanticanalyzer/SemanticAnalyzer.h"
#include "../../../../../src/query/processor/cypher/queryplanner/QueryPlanner.h"
#include "../../../../../src/query/processor/cypher/runtime/AggregationFactory.h"
#include "../../../../../src/query/processor/cypher/runtime/Aggregation.h"
#include "../../../../../src/server/JasmineGraphServer.h"

#include "/home/ubuntu/software/antlr/CypherLexer.h"
#include "/home/ubuntu/software/antlr/CypherParser.h"

Logger cypher_logger;

inline const json* getNestedValuePtr(const json& obj, const std::string& dottedKey) {
    const json* current = &obj;
    size_t start = 0;
    while (start < dottedKey.size()) {
        size_t dot = dottedKey.find('.', start);
        std::string key = dottedKey.substr(start, dot - start);
        if (!current->is_object()) {
            cypher_logger.error("Current JSON is not an object at key: '" + key + "'");
            return nullptr;
        }
        auto it = current->find(key);
        if (it == current->end()) {
            cypher_logger.error("Key '" + key + "' not found");
            return nullptr;
        }
        current = &(*it);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return current;
}

CypherQueryExecutor::CypherQueryExecutor() {}

CypherQueryExecutor::CypherQueryExecutor(SQLiteDBInterface *db, PerformanceSQLiteDBInterface *perfDb,
    JobRequest jobRequest) {
    this->sqlite = db;
    this->perfDB = perfDb;
    this->request = jobRequest;
}

void CypherQueryExecutor::execute() {
    cypher_logger.info("Executing Cypher Query");

    int uniqueId = getUid();
    std::string masterIP = request.getMasterIP();
    std::string graphId = request.getParameter(Conts::PARAM_KEYS::GRAPH_ID);
    std::string canCalibrateString = request.getParameter(Conts::PARAM_KEYS::CAN_CALIBRATE);
    std::string autoCalibrateString = request.getParameter(Conts::PARAM_KEYS::AUTO_CALIBRATION);
    std::string queueTime = request.getParameter(Conts::PARAM_KEYS::QUEUE_TIME);
    std::string graphSLAString = request.getParameter(Conts::PARAM_KEYS::GRAPH_SLA);
    std::string queryString = request.getParameter(Conts::PARAM_KEYS::CYPHER_QUERY::QUERY_STRING);
    int numberOfPartitions = std::stoi(request.getParameter(Conts::PARAM_KEYS::NO_OF_PARTITIONS));
    int connFd = std::stoi(request.getParameter(Conts::PARAM_KEYS::CONN_FILE_DESCRIPTOR));
    bool *loop_exit = reinterpret_cast<bool*>(static_cast<std::uintptr_t>(std::stoull(
        request.getParameter(Conts::PARAM_KEYS::LOOP_EXIT_POINTER))));


    bool canCalibrate = Utils::parseBoolean(canCalibrateString);
    bool autoCalibrate = Utils::parseBoolean(autoCalibrateString);

    antlr4::ANTLRInputStream input(queryString);
    // Create a lexer from the input
    CypherLexer lexer(&input);
    cypher_logger.info("Created lexer from input");

    // Create a token stream from the lexer
    antlr4::CommonTokenStream tokens(&lexer);
    cypher_logger.info("Created tokens from lexer");

    // Create a parser from the token stream
    CypherParser parser(&tokens);
    cypher_logger.info("Created parser from tokens");

    ASTBuilder astBuilder;
    auto* ast = any_cast<ASTNode*>(astBuilder.visitOC_Cypher(parser.oC_Cypher()));
    cypher_logger.debug(ast->print());

    SemanticAnalyzer semanticAnalyzer;
    string queryPlan;
    Operator *executionPlan;
    if (semanticAnalyzer.analyze(ast)) {
        cypher_logger.info("AST is successfully analyzed");
        QueryPlanner queryPlanner;
        executionPlan = queryPlanner.createExecutionPlan(ast);
        queryPlan = executionPlan->execute();
    } else {
        cypher_logger.error("Query isn't semantically correct: " + queryString);
    }




    std::vector<std::future<void>> intermRes;
    std::vector<std::future<int>> statResponse;

    auto begin = chrono::high_resolution_clock::now();

    const auto &workerList = JasmineGraphServer::getWorkers(numberOfPartitions);

    if (executionPlan->isApply)
    {
        std::vector<std::unique_ptr<SharedBuffer>> bufferPool;
        bufferPool.reserve(numberOfPartitions);
        for (size_t i = 0; i < numberOfPartitions; ++i)
        {
            bufferPool.emplace_back(std::make_unique<SharedBuffer>(MASTER_BUFFER_SIZE));
        }

            std::thread nestedQuery(&Apply::executeDistributed, static_cast<Apply*>(executionPlan), masterIP, std::stoi(graphId), numberOfPartitions, std::ref(bufferPool));


            // Stream results to client
            int closeFlag = 0;
            int count = 0;
            int result_wr;
            while (true) {
                if (closeFlag == numberOfPartitions) {
                    if (nestedQuery.joinable()) {
                        nestedQuery.join();
                    }
                    break;
                }
                for (size_t i = 0; i < bufferPool.size(); ++i) {
                    std::string data;
                    if (bufferPool[i]->tryGet(data)) {
                        if (data == "-1") {
                            closeFlag++;
                        } else {
                            count++;
                            result_wr = write(connFd, data.c_str(), data.length());
                            result_wr = write(connFd, Conts::CARRIAGE_RETURN_NEW_LINE.c_str(),
                                              Conts::CARRIAGE_RETURN_NEW_LINE.size());
                            if (result_wr < 0) {
                                cypher_logger.error("Error writing to socket");
                                *loop_exit = true;
                                return;
                            }
                        }
                    }
                }
            }
        }else
        {
            std::vector<std::unique_ptr<SharedBuffer>> bufferPool;
            bufferPool.reserve(numberOfPartitions);  // Pre-allocate space for pointers
            for (size_t i = 0; i < numberOfPartitions; ++i) {
                bufferPool.emplace_back(std::make_unique<SharedBuffer>(MASTER_BUFFER_SIZE));
            }

            std::vector<std::thread> workerThreads;
            int count = 0;
            for (auto worker : workerList) {
                workerThreads.emplace_back(
                    doCypherQuery,
                    worker.hostname, worker.port,
                    masterIP, std::stoi(graphId), count,
                    queryPlan, std::ref(*bufferPool[count]));
                count++;
            }

            PerformanceUtil::init();

            std::string query =
                "SELECT attempt from graph_sla INNER JOIN sla_category where graph_sla.id_sla_category=sla_category.id and "
                "graph_sla.graph_id='" +
                graphId + "' and graph_sla.partition_count='" + std::to_string(numberOfPartitions) +
                "' and sla_category.category='" + Conts::SLA_CATEGORY::LATENCY + "' and sla_category.command='" + CYPHER +
                "';";

            std::vector<vector<pair<string, string>>> queryResults = perfDB->runSelect(query);

            if (queryResults.size() > 0) {
                std::string attemptString = queryResults[0][0].second;
                int calibratedAttempts = atoi(attemptString.c_str());

                if (calibratedAttempts >= Conts::MAX_SLA_CALIBRATE_ATTEMPTS) {
                    canCalibrate = false;
                }
            } else {
                cypher_logger.info("###CYPHER-QUERY-EXECUTOR### Inserting initial record for SLA ");
                Utils::updateSLAInformation(perfDB, graphId, numberOfPartitions, 0, CYPHER, Conts::SLA_CATEGORY::LATENCY);
                statResponse.push_back(std::async(std::launch::async, AbstractExecutor::collectPerformaceData, perfDB,
                                                  graphId.c_str(), CYPHER, Conts::SLA_CATEGORY::LATENCY, numberOfPartitions,
                                                  masterIP, autoCalibrate));
                isStatCollect = true;
            }

            int result_wr;
            int closeFlag = 0;
            if (Operator::isAggregate) {
                auto startTime = std::chrono::high_resolution_clock::now();
                if (Operator::aggregateType == AggregationFactory::AVERAGE) {
                    Aggregation* aggregation = AggregationFactory::getAggregationMethod(AggregationFactory::AVERAGE);
                    while (true) {
                        if (closeFlag >= numberOfPartitions) {
                            break;
                        }
                        for (size_t i = 0; i < bufferPool.size(); ++i) {
                            std::string data;
                            if (bufferPool[i]->tryGet(data)) {
                                if (data == "-1") {
                                    closeFlag++;
                                } else {
                                    aggregation->insert(data);
                                }
                            }
                        }
                    }
                    aggregation->getResult(connFd);
                }else if (Operator::aggregateType == AggregationFactory::COUNT) {
                    Aggregation* aggregation = AggregationFactory::getAggregationMethod(AggregationFactory::COUNT );
                    while (true) {
                        if (closeFlag == numberOfPartitions) {
                            break;
                        }
                        for (size_t i = 0; i < bufferPool.size(); ++i) {
                            std::string data;
                            if (bufferPool[i]->tryGet(data)) {
                                if (data == "-1") {
                                    closeFlag++;
                                } else {
                                    aggregation->insert(data);
                                }
                            }
                        }
                    }
                    aggregation->getResult(connFd);
                }

                else if (Operator::aggregateType == AggregationFactory::ASC ||
                           Operator::aggregateType == AggregationFactory::DESC) {
                    struct BufferEntry {
                        std::string value;
                        size_t bufferIndex;
                        json data;
                        bool isAsc;
                        BufferEntry(const std::string& v, size_t idx, const json& parsed, bool asc)
                                : value(v), bufferIndex(idx), data(parsed), isAsc(asc) {}
                        bool operator<(const BufferEntry& other) const {
                            const json* val1 = getNestedValuePtr(data, Operator::aggregateKey);
                            if (!val1) {
                                cypher_logger.error("Missing key in val1 for comparison: " + Operator::aggregateKey);
                                return false;  // or decide what fallback you want
                            }
                            const json* val2 = getNestedValuePtr(other.data, Operator::aggregateKey);
                            if (!val2) {
                                cypher_logger.error("Missing key in val2 for comparison: " + Operator::aggregateKey);
                                return false;
                            }
                            bool result;
                            if (val1->is_number_integer() && val2->is_number_integer()) {
                                result = val1->get<int>() > val2->get<int>();
                            } else if (val1->is_string() && val2->is_string()) {
                                result = val1->get<std::string>() > val2->get<std::string>();
                            } else {
                                result = val1->dump() > val2->dump();  // fallback comparison
                            }
                            return isAsc ? result : !result;  // Flip for DESC
                        }
                    };
                    bool isAsc = (Operator::aggregateType == AggregationFactory::ASC);
                    std::priority_queue<BufferEntry> mergeQueue;  // Min-heap
                    while (true) {
                        if (closeFlag == numberOfPartitions) {
                            break;
                        }
                        for (size_t i = 0; i < bufferPool.size(); ++i) {
                            std::string value;
                            if (bufferPool[i]->tryGet(value)) {
                                if (value != "-1") {
                                    try {
                                        json parsed = json::parse(value);
                                        const json *aggVal = getNestedValuePtr(parsed, Operator::aggregateKey);
                                        if (!aggVal) {
                                            cypher_logger.error("Missing key '" + Operator::aggregateKey
                                                + "' in JSON: " + value);
                                            continue;
                                        }
                                        BufferEntry entry{value, i, parsed, isAsc};
                                        mergeQueue.push(entry);
                                    } catch (const json::exception &e) {
                                        cypher_logger.error("JSON parse error: " + std::string(e.what()));
                                        continue;
                                    }
                                } else {
                                    closeFlag++;
                                }
                            }
                        }
                    }

                    cypher_logger.info("START MASTER SORTING");
                    cypher_logger.info(std::to_string(mergeQueue.size()));
                    while (!mergeQueue.empty()) {
                        BufferEntry smallest = mergeQueue.top();
                        cypher_logger.info(smallest.value);
                        size_t queueSize = mergeQueue.size();
                        cypher_logger.debug(std::to_string(queueSize));
                        mergeQueue.pop();
                        result_wr = write(connFd, smallest.value.c_str(), smallest.value.length());
                        if (result_wr < 0) {
                            cypher_logger.error("Error writing to socket");
                            return;
                        }
                        result_wr = write(connFd, Conts::CARRIAGE_RETURN_NEW_LINE.c_str(),
                                          Conts::CARRIAGE_RETURN_NEW_LINE.size());
                        if (result_wr < 0) {
                            cypher_logger.error("Error writing to socket");
                            *loop_exit = true;
                            return;
                        }
                        if (closeFlag < numberOfPartitions) {
                            std::string nextValue = bufferPool[smallest.bufferIndex]->get();
                            if (nextValue == "-1") {
                                closeFlag++;
                                cypher_logger.info("closeflag" + std::to_string(closeFlag));
                            } else {
                                try {
                                    json parsed = json::parse(nextValue);
                                    if (!parsed.contains(Operator::aggregateKey)) {
                                        cypher_logger.error("Missing key '" + Operator::aggregateKey +
                                                              "' in JSON: " + nextValue);
                                        continue;
                                    }
                                    BufferEntry entry{nextValue, smallest.bufferIndex, parsed, isAsc};
                                    mergeQueue.push(entry);
                                } catch (const json::exception& e) {
                                    cypher_logger.error("JSON parse error: " + std::string(e.what()));
                                }
                            }
                        }
                    }
                           } else {
                               std::string log = "Query is recongnized as Aggreagation, but method doesnot have implemented yet";
                               result_wr = write(connFd, log.c_str(), log.length());
                               result_wr = write(connFd, Conts::CARRIAGE_RETURN_NEW_LINE.c_str(),
                                                 Conts::CARRIAGE_RETURN_NEW_LINE.size());
                               if (result_wr < 0) {
                                   cypher_logger.error("Error writing to socket");
                                   *loop_exit = true;
                                   return;
                               }
                           }
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                int totalTime = duration.count();
                cypher_logger.info("Total time taken for aggregation: " + std::to_string(totalTime) + " ms");
                Operator::isAggregate = false;
            } else if (Operator::isGroupBy) {
               std::unordered_map<std::string, std::vector<Aggregation*>> groupedHelpers;

               cypher_logger.debug("GroupBy: Starting to process rows from sharedBuffer");
               int rowCount = 0;

               while (true)
               {
                   cypher_logger.debug("GroupBy: Top of main while loop. closeFlag = " + std::to_string(closeFlag) + ", numberOfPartitions = " + std::to_string(numberOfPartitions));
                   if (closeFlag == numberOfPartitions) {
                       cypher_logger.debug("GroupBy: All partitions closed. Writing grouped results to client.");

                       for (const auto &[groupKey, helpers] : groupedHelpers) {
                           cypher_logger.debug("GroupBy: Writing groupKey: " + groupKey + " with " + std::to_string(helpers.size()) + " helpers.");
                           json out;
                           std::istringstream keyStream(groupKey);
                           std::string token;
                           int i = 0;

                           json group = json::parse(helpers[0]->data);
                           cypher_logger.debug("GroupBy: Initial group data: " + group.dump());

                           // Add aggregated results
                           for (size_t j = 0; j < helpers.size(); ++j) {
                               std::string variableName = helpers[j]->getVariableName();
                               cypher_logger.debug("GroupBy: Adding aggregation result for variable: " + variableName + ", type: " + helpers[j]->getType());
                               group[variableName] = json::parse(helpers[j]->data)[helpers[j]->getType()];
                               group.erase(helpers[j]->getType());
                               group.erase("groupByKey");
                           }
                           cypher_logger.debug("GroupBy: Final group data to write: " + group.dump());
                           result_wr = write(connFd, group.dump().c_str(), group.dump().length());
                           cypher_logger.debug("GroupBy: Wrote group data to client, result_wr = " + std::to_string(result_wr));
                           result_wr = write(connFd, Conts::CARRIAGE_RETURN_NEW_LINE.c_str(),
                                             Conts::CARRIAGE_RETURN_NEW_LINE.size());
                           cypher_logger.debug("GroupBy: Wrote CRLF to client, result_wr = " + std::to_string(result_wr));
                       }
                       cypher_logger.debug("GroupBy: Finished writing all groups. Breaking main loop.");
                       break;
                   }
                   for (size_t i = 0; i < bufferPool.size(); ++i)
                   {
                       cypher_logger.debug("GroupBy: Checking bufferPool[" + std::to_string(i) + "]");
                       std::string data;
                       if (bufferPool[i]->tryGet(data))
                       {
                           cypher_logger.debug("GroupBy: bufferPool[" + std::to_string(i) + "] tryGet success. Data: " + data);
                           if (data == "-1") {
                               closeFlag++;
                               cypher_logger.debug("GroupBy: Received close flag from bufferPool[" + std::to_string(i) + "]. closeFlag now: " + std::to_string(closeFlag));
                           }
                        else {

                           try {
                               json rowJson = json::parse(data);
                               cypher_logger.debug("GroupBy: Processing row: " + data);

                               // === Build group key ===
                               std::string groupKey = rowJson["groupByKey"];
                               cypher_logger.debug("GroupBy: Computed groupKey: " + groupKey);
                               std::vector<std::tuple<std::string, std::string>> aggregateColumns;
                               cypher_logger.debug("GroupBy: Extracting aggregateColumns");
                               for (const auto &agg : rowJson["variable"]) {
                                   std::string func = agg["functionName"];
                                   std::string var = agg["variable"];
                                   // std::string prop = agg["property"];
                                   aggregateColumns.emplace_back(func, var);
                                   cypher_logger.debug("GroupBy: aggregateColumn - function: " + func + ", variable: " + var );
                               }
                               // === Create aggregation helpers per group if not exists ===
                               if (groupedHelpers.find(groupKey) == groupedHelpers.end()) {
                                   cypher_logger.debug("GroupBy: Creating new helpers for groupKey: " + groupKey);
                                   std::vector<Aggregation*> helpers;
                                   for (const auto &[func, var] : aggregateColumns) {
                                       cypher_logger.debug("GroupBy: Creating AggregationHelper for function: " + func + ", variable: " + var);
                                       Aggregation* helper = AggregationFactory::getAggregationMethod(func);
                                       helper->setVariableName(var);
                                       helper->setTpye(func);
                                       helpers.push_back(helper);
                                   }
                                   groupedHelpers[groupKey] = helpers;
                                   cypher_logger.debug("GroupBy: Created new helpers for groupKey: " + groupKey);
                               }

                               // === Insert data into helpers ===
                               for (Aggregation* helper : groupedHelpers[groupKey]) {
                                   cypher_logger.debug("GroupBy: Inserting data into AggregationHelper for groupKey: " + groupKey + ", variable: " + helper->getVariableName() + ", type: " + helper->getType());
                                   json row = rowJson[helper->getVariableName()];
                                   // json row = json::parse(var);
                                      cypher_logger.debug("GroupBy: Row data for variable " + helper->getVariableName() + ": " + row.dump());

                    if (row.is_object()) {
                       rowJson[helper->getType()] = row[helper->getType()];
                        cypher_logger.debug("GroupBy: Row is object, extracted value for " + helper->getType() + ": " + row[helper->getType()].dump());
                   } else if (row.is_string()) {
                       try {
                           json rowObj = json::parse(row.get<std::string>());
                            cypher_logger.debug("GroupBy: Row is string, parsed to JSON: " + rowObj.dump());
                           rowJson[helper->getType()] = rowObj[helper->getType()];
                            cypher_logger.debug("GroupBy: Extracted value for " + helper->getType() + ": " + rowObj[helper->getType()].dump());
                       } catch (const std::exception& e) {
                           cypher_logger.warn("Failed to parse row string to JSON: " + std::string(e.what()));
                       }
                   }
                                   rowJson["variable"] = helper->getType();
                                   // rowJson["variable"] = helper->getType();
                                      cypher_logger.debug("GroupBy: Row JSON after processing: " + rowJson.dump());
                                   helper->insert(rowJson.dump());
                                   cypher_logger.debug("GroupBy: Inserted data into AggregationHelper for groupKey: " + groupKey);
                               }
                               rowCount++;
                               cypher_logger.debug("GroupBy: rowCount incremented to " + std::to_string(rowCount));
                           } catch (const std::exception& e) {
                               cypher_logger.warn("GroupBy: Skipping malformed row. Exception: " + std::string(e.what()));
                           } catch (...) {
                               cypher_logger.warn("GroupBy: Skipping malformed row. Unknown exception.");
                           }
                       }
                   }
                   }
               }

                Operator::isGroupBy = false;
           } else {
                int count = 0;
                while (true) {
                    if (closeFlag == numberOfPartitions) {
                        break;
                    }
                    for (size_t i = 0; i < bufferPool.size(); ++i) {
                        std::string data;
                        if (bufferPool[i]->tryGet(data)) {
                            if (data == "-1") {
                                closeFlag++;
                            } else {
                                count++;
                                result_wr = write(connFd, data.c_str(), data.length());
                                result_wr = write(connFd, Conts::CARRIAGE_RETURN_NEW_LINE.c_str(),
                                                  Conts::CARRIAGE_RETURN_NEW_LINE.size());
                                if (result_wr < 0) {
                                    cypher_logger.error("Error writing to socket");
                                    *loop_exit = true;
                                    return;
                                }
                            }
                        }
                    }
                }
                cypher_logger.info("Total records returned: " + std::to_string(count));
            }

            for (auto& thread : workerThreads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            cypher_logger.info("###CYPHER-QUERY-EXECUTOR### Executing Query : Completed");
        }
    workerResponded = true;
    JobResponse jobResponse;
    jobResponse.setJobId(request.getJobId());
    responseVector.push_back(jobResponse);

    responseVectorMutex.lock();
    responseMap[request.getJobId()] = jobResponse;
    responseVectorMutex.unlock();

    auto end = chrono::high_resolution_clock::now();
    auto dur = end - begin;
    auto msDuration = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();

    std::string durationString = std::to_string(msDuration);

    if (canCalibrate || autoCalibrate) {
        Utils::updateSLAInformation(perfDB, graphId, numberOfPartitions, msDuration, CYPHER,
                                    Conts::SLA_CATEGORY::LATENCY);
        isStatCollect = false;
    }

    processStatusMutex.lock();
    for (auto processCompleteIterator = processData.begin(); processCompleteIterator != processData.end();
         ++processCompleteIterator) {
        ProcessInfo processInformation = *processCompleteIterator;
        if (processInformation.id == uniqueId) {
            processData.erase(processInformation);
            break;
        }
    }
    processStatusMutex.unlock();
}

void CypherQueryExecutor::doCypherQuery(std::string host, int port, std::string masterIP, int graphID,
                                               int PartitionId, std::string message, SharedBuffer &sharedBuffer) {
    Utils::sendQueryPlanToWorker(host, port, masterIP, graphID, PartitionId, message, sharedBuffer);
}


int CypherQueryExecutor::getUid() {
    static std::atomic<std::uint32_t> uid{0};
    return ++uid;
}

// void CypherQueryExecutor::apply( Operator* op ,
//         std::vector<std::unique_ptr<SharedBuffer>> &bufferPool,>)
// {
//
//
//     std::vector<std::thread> workerThreads;
//     int count = 0;
//     for (auto worker : workerList) {
//         workerThreads.emplace_back(
//             doCypherQuery,
//             worker.hostname, worker.port,
//             masterIP, std::stoi(graphId), count,
//             queryPlan, std::ref(*bufferPool[count]));
//         count++;
//     }
//
// }
