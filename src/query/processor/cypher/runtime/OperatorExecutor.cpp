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

#include "OperatorExecutor.h"
#include "InstanceHandler.h"
#include "../util/Const.h"
#include "../../../../util/logger/Logger.h"
#include "Helpers.h"
#include <thread>
#include <queue>

Logger execution_logger;
std::unordered_map<std::string,
    std::function<void(OperatorExecutor&, SharedBuffer&, std::string, GraphConfig)>> OperatorExecutor::methodMap;
OperatorExecutor::OperatorExecutor(GraphConfig gc, std::string queryPlan, std::string masterIP):
    queryPlan(queryPlan), gc(gc), masterIP(masterIP) {
    this->query = json::parse(queryPlan);
};

void OperatorExecutor::initializeMethodMap() {
    methodMap["AllNodeScan"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.AllNodeScan(buffer, jsonPlan, gc);
    };

    methodMap["ProduceResult"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.ProduceResult(buffer, jsonPlan, gc);
    };

    methodMap["Filter"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.Filter(buffer, jsonPlan, gc);
    };

    methodMap["ExpandAll"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.ExpandAll(buffer, jsonPlan, gc);
    };
    methodMap["VarLengthExpandAll"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.VarLengthExpandAll(buffer, jsonPlan, gc);
    };

    methodMap["UndirectedRelationshipTypeScan"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.UndirectedRelationshipTypeScan(buffer, jsonPlan, gc);
    };

    methodMap["UndirectedAllRelationshipScan"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.UndirectedAllRelationshipScan(buffer, jsonPlan, gc);
    };

    methodMap["DirectedRelationshipTypeScan"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                                     std::string jsonPlan, GraphConfig gc) {
        executor.DirectedRelationshipTypeScan(buffer, jsonPlan, gc);
    };

    methodMap["DirectedAllRelationshipScan"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                                    std::string jsonPlan, GraphConfig gc) {
        executor.DirectedAllRelationshipScan(buffer, jsonPlan, gc);
    };

    methodMap["NodeByIdSeek"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                                    std::string jsonPlan, GraphConfig gc) {
        executor.NodeByIdSeek(buffer, jsonPlan, gc);
    };

    methodMap["Projection"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
            std::string jsonPlan, GraphConfig gc) {
        executor.Projection(buffer, jsonPlan, gc);
    };

    methodMap["AggregationFunction"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                   std::string jsonPlan, GraphConfig gc) {
        executor.AggregationFunction(buffer, jsonPlan, gc);
    };

    methodMap["Create"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                    std::string jsonPlan, GraphConfig gc) {
        executor.Create(buffer, jsonPlan, gc);
    };

    methodMap["CartesianProduct"] = [](OperatorExecutor &executor, SharedBuffer &buffer,
                                     std::string jsonPlan, GraphConfig gc) {
        executor.CartesianProduct(buffer, jsonPlan, gc);
    };

    methodMap["Distinct"] = [](OperatorExecutor &executor, SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
        executor.Distinct(buffer, jsonPlan, gc);
    };

    methodMap["OrderBy"] = [](OperatorExecutor &executor, SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
        executor.OrderBy(buffer, jsonPlan, gc);
    };

    methodMap["NodeScanByLabel"] = [](OperatorExecutor &executor, SharedBuffer &buffer, std::string jsonPlan,
            GraphConfig gc) {
        executor.NodeScanByLabel(buffer, jsonPlan, gc);
    };
}

void OperatorExecutor::AllNodeScan(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);
    for (auto it : nodeManager.nodeIndex) {
        json nodeData;
        auto nodeId = it.first;
        NodeBlock *node = nodeManager.get(nodeId);
        std::string value(node->getMetaPropertyHead()->value);
        if (value == to_string(gc.partitionID)) {
            nodeData["partitionID"] = value;
            std::map<std::string, char*> properties = node->getAllProperties();
            for (auto property : properties) {
                nodeData[property.first] = property.second;
            }
            for (auto& [key, value] : properties) {
                delete[] value;  // Free each allocated char* array
            }
            properties.clear();

            json data;
            string variable = query["variables"];
            data[variable] = nodeData;
            buffer.add(data.dump());
        }
    }
    buffer.add("-1");
}

void OperatorExecutor::NodeScanByLabel(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);
    for (auto it : nodeManager.nodeIndex) {
        json nodeData;
        auto nodeId = it.first;
        NodeBlock *node = nodeManager.get(nodeId);
        string label = node->getLabel();
        std::string value(node->getMetaPropertyHead()->value);
        if (value == to_string(gc.partitionID) && label == query["Label"]) {
            nodeData["partitionID"] = value;
            std::map<std::string, char*> properties = node->getAllProperties();
            for (auto property : properties) {
                nodeData[property.first] = property.second;
            }
            for (auto& [key, value] : properties) {
                delete[] value;  // Free each allocated char* array
            }
            properties.clear();

            json data;
            string variable = query["variable"];
            data[variable] = nodeData;
            buffer.add(data.dump());
        }
    }
    buffer.add("-1");
}

void OperatorExecutor::ProduceResult(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.debug("ProduceResult: Parsing query plan");
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    execution_logger.debug("ProduceResult: NextOperator = " + nextOpt);
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];
    execution_logger.debug("ProduceResult: Launching next operator thread: " + next["Operator"].get<std::string>());
    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);

    while (true) {
        string raw = sharedBuffer.get();
        if (raw == "-1") {
            execution_logger.debug("ProduceResult: Received end signal from sharedBuffer");
            buffer.add(raw);
            result.join();
            break;
        }
        std::vector<std::string> values = query["variable"].get<std::vector<std::string>>();
        json data;
        json rawObj = json::parse(raw);
        for (auto value : values) {
            data[value] = rawObj[value];
        }
        execution_logger.debug("ProduceResult: Adding result to buffer: " + data.dump());
        buffer.add(data.dump());
    }
}

void OperatorExecutor::Filter(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];
    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);

    auto condition = query["condition"];
    FilterHelper FilterHelper(condition.dump());
    while (true) {
        string raw = sharedBuffer.get();
        if (raw == "-1") {
            buffer.add(raw);
            result.join();
            break;
        }
        if (FilterHelper.evaluate(raw)) {
            buffer.add(raw);
        }
    }
}

void OperatorExecutor::UndirectedRelationshipTypeScan(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);

    const std::string& dbPrefix = nodeManager.getDbPrefix();
    long localRelationCount = nodeManager.dbSize(dbPrefix + "_relations.db") / RelationBlock::BLOCK_SIZE;
    long centralRelationCount = nodeManager.dbSize(dbPrefix +
                                                   "_central_relations.db") / RelationBlock::CENTRAL_BLOCK_SIZE;
    string direction = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = false;
    if (direction == "TRUE") {
        isDirected = true;
    }
    int count = 1;
    for (long i = 1; i < localRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getLocalRelation(i*RelationBlock::BLOCK_SIZE);
        if (relation->getLocalRelationshipType() != query["relType"]) {
            continue;
        }
        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json rightDirectionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        rightDirectionData[start] = startNodeData;
        rightDirectionData[dest] = destNodeData;
        rightDirectionData[rel] = relationData;
        buffer.add(rightDirectionData.dump());

        if (!isDirected) {
            json leftDirectionData;
            leftDirectionData[start] = destNodeData;
            leftDirectionData[dest] = startNodeData;
            leftDirectionData[rel] = relationData;
            buffer.add(leftDirectionData.dump());
        }
        count++;
    }

    int central = 1;
    for (long i = 1; i < centralRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getCentralRelation(i * RelationBlock::CENTRAL_BLOCK_SIZE);
        if (relation->getCentralRelationshipType() != query["relType"]) {
            continue;
        }

        std::string pid(relation->getMetaPropertyHead()->value);
        if (pid != to_string(gc.partitionID)) {
            continue;
        }

        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);

        if (startPid != to_string(gc.partitionID)) {
            continue;
        }
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;

        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json rightDirectionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        rightDirectionData[start] = startNodeData;
        rightDirectionData[dest] = destNodeData;
        rightDirectionData[rel] = relationData;
        buffer.add(rightDirectionData.dump());

        if (!isDirected) {
            json leftDirectionData;
            leftDirectionData[start] = destNodeData;
            leftDirectionData[dest] = startNodeData;
            leftDirectionData[rel] = relationData;
            buffer.add(leftDirectionData.dump());
        }
        central++;
    }
    buffer.add("-1");
}

void OperatorExecutor::UndirectedAllRelationshipScan(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);

    const std::string& dbPrefix = nodeManager.getDbPrefix();
    long localRelationCount = nodeManager.dbSize(dbPrefix + "_relations.db") / RelationBlock::BLOCK_SIZE;
    long centralRelationCount = nodeManager.dbSize(dbPrefix +
                                                    "_central_relations.db") / RelationBlock::CENTRAL_BLOCK_SIZE;
    string direction = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = false;
    if (direction == "TRUE") {
        isDirected = true;
    }
    int count = 1;
    for (long i = 1; i < localRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getLocalRelation(i*RelationBlock::BLOCK_SIZE);
        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();


        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json rightDirectionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        rightDirectionData[start] = startNodeData;
        rightDirectionData[dest] = destNodeData;
        rightDirectionData[rel] = relationData;
        buffer.add(rightDirectionData.dump());

        if (!isDirected) {
            json leftDirectionData;
            leftDirectionData[start] = destNodeData;
            leftDirectionData[dest] = startNodeData;
            leftDirectionData[rel] = relationData;
            buffer.add(leftDirectionData.dump());
        }
        count++;
    }

    int central = 1;
    for (long i = 1; i < centralRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getCentralRelation(i*RelationBlock::CENTRAL_BLOCK_SIZE);
        std::string pid(relation->getMetaPropertyHead()->value);
        if (pid != to_string(gc.partitionID)) {
            continue;
        }

        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json rightDirectionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        rightDirectionData[start] = startNodeData;
        rightDirectionData[dest] = destNodeData;
        rightDirectionData[rel] = relationData;
        buffer.add(rightDirectionData.dump());

        if (!isDirected) {
            json leftDirectionData;
            leftDirectionData[start] = destNodeData;
            leftDirectionData[dest] = startNodeData;
            leftDirectionData[rel] = relationData;
            buffer.add(leftDirectionData.dump());
        }
        central++;
    }
    buffer.add("-1");
}

void OperatorExecutor::DirectedRelationshipTypeScan(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.info("DirectedRelationshipTypeScan: Start processing local relations");
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);
    string direction = query["direction"];
    const std::string& dbPrefix = nodeManager.getDbPrefix();
    long localRelationCount = nodeManager.dbSize(dbPrefix + "_relations.db") / RelationBlock::BLOCK_SIZE;
    long centralRelationCount = nodeManager.dbSize(dbPrefix +
                                                   "_central_relations.db") / RelationBlock::CENTRAL_BLOCK_SIZE;
    string graphDirection = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = false;
    if (graphDirection == "TRUE") {
        isDirected = true;
    }
    bool isDirectionRight = query["direction"] == "right";
    int count = 1;
    for (long i = 1; i < localRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getLocalRelation(i*RelationBlock::BLOCK_SIZE);
        if (relation->getLocalRelationshipType() != query["relType"]) {
            continue;
        }
        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json directionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        if (isDirectionRight) {
            directionData[start] = startNodeData;
            directionData[dest] = destNodeData;
        } else {
            directionData[start] = destNodeData;
            directionData[dest] = startNodeData;
        }
        directionData[rel] = relationData;
        buffer.add(directionData.dump());
        execution_logger.info("DirectedRelationshipTypeScan: Added local relation " + std::to_string(i));
        count++;
    }

    execution_logger.info("DirectedRelationshipTypeScan: Start processing central relations");
    int central = 1;
    for (long i = 1; i < centralRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getCentralRelation(i*RelationBlock::CENTRAL_BLOCK_SIZE);
        if (relation->getCentralRelationshipType() != query["relType"]) {
            continue;
        }

        std::string pid(relation->getMetaPropertyHead()->value);
        if (pid != to_string(gc.partitionID)) {
            continue;
        }

        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json directionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        if (isDirectionRight) {
            directionData[start] = startNodeData;
            directionData[dest] = destNodeData;
        } else {
            directionData[start] = destNodeData;
            directionData[dest] = startNodeData;
        }
        directionData[rel] = relationData;
        buffer.add(directionData.dump());
        execution_logger.info("DirectedRelationshipTypeScan: Added central relation " + std::to_string(i));

        central++;
    }
    buffer.add("-1");
    execution_logger.info("DirectedRelationshipTypeScan: Finished processing");
}

void OperatorExecutor::DirectedAllRelationshipScan(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);
    string direction = query["direction"];
    const std::string& dbPrefix = nodeManager.getDbPrefix();
    long localRelationCount = nodeManager.dbSize(dbPrefix + "_relations.db") / RelationBlock::BLOCK_SIZE;
    long centralRelationCount = nodeManager.dbSize(dbPrefix +
                                                   "_central_relations.db") / RelationBlock::CENTRAL_BLOCK_SIZE;
    string graphDirection = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = false;
    if (graphDirection == "TRUE") {
        isDirected = true;
    }
    bool isDirectionRight = query["direction"] == "right";
    int count = 1;
    for (long i = 1; i < localRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getLocalRelation(i * RelationBlock::BLOCK_SIZE);
        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json directionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        if (isDirectionRight) {
            directionData[start] = startNodeData;
            directionData[dest] = destNodeData;
        } else if (!isDirected) {
            directionData[start] = destNodeData;
            directionData[dest] = startNodeData;
        }
        directionData[rel] = relationData;
        buffer.add(directionData.dump());
        count++;
    }

    int central = 1;
    for (long i = 1; i < centralRelationCount; i++) {
        json startNodeData;
        json destNodeData;
        json relationData;
        RelationBlock* relation = RelationBlock::getCentralRelation(i * RelationBlock::CENTRAL_BLOCK_SIZE);
        std::string pid(relation->getMetaPropertyHead()->value);
        if (pid != to_string(gc.partitionID)) {
            continue;
        }

        NodeBlock* startNode = relation->getSource();
        NodeBlock* destNode = relation->getDestination();

        std::string startPid(startNode->getMetaPropertyHead()->value);
        startNodeData["partitionID"] = startPid;
        std::map<std::string, char*> startProperties = startNode->getAllProperties();
        for (auto property : startProperties) {
            startNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : startProperties) {
            delete[] value;  // Free each allocated char* array
        }
        startProperties.clear();

        std::string destPid(destNode->getMetaPropertyHead()->value);
        destNodeData["partitionID"] = destPid;
        std::map<std::string, char*> destProperties = destNode->getAllProperties();
        for (auto property : destProperties) {
            destNodeData[property.first] = property.second;
        }
        for (auto& [key, value] : destProperties) {
            delete[] value;  // Free each allocated char* array
        }
        destProperties.clear();

        std::map<std::string, char*> relProperties = relation->getAllProperties();
        for (auto property : relProperties) {
            relationData[property.first] = property.second;
        }
        for (auto& [key, value] : relProperties) {
            delete[] value;  // Free each allocated char* array
        }
        relProperties.clear();

        json directionData;
        string start = query["sourceVariable"];
        string dest = query["destVariable"];
        string rel = query["relVariable"];

        if (isDirectionRight) {
            directionData[start] = startNodeData;
            directionData[dest] = destNodeData;
        } else if (!isDirected) {
            directionData[start] = destNodeData;
            directionData[dest] = startNodeData;
        }
        directionData[rel] = relationData;
        buffer.add(directionData.dump());

        central++;
    }
    buffer.add("-1");
}

void OperatorExecutor::NodeByIdSeek(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    NodeManager nodeManager(gc);
    NodeBlock* node = nodeManager.get(query["id"]);
    if (node) {
        json nodeData;
        std::string value(node->getMetaPropertyHead()->value);
        if (value == to_string(gc.partitionID)) {
            std::map<std::string, char*> properties = node->getAllProperties();
            nodeData["partitionID"] = value;
            for (auto property : properties) {
                nodeData[property.first] = property.second;
            }
            json data;
            string variable = query["variable"];
            data[variable] = nodeData;
            buffer.add(data.dump());
        }
    }
    buffer.add("-1");
}

void OperatorExecutor::ExpandAll(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.debug("ExpandAll: Parsing query plan");
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];
    execution_logger.debug("ExpandAll: Launching next operator thread: " + next["Operator"].get<std::string>());
    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);

    string sourceVariable = query["sourceVariable"];
    string destVariable = query["destVariable"];
    string relVariable = query["relVariable"];

    string relType = "";
    if (query.contains("relType")) {
        relType = query["relType"];
        execution_logger.debug("ExpandAll: relType found: " + relType);
    }
    string graphDirection = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = false;
    if (graphDirection == "TRUE") {
        isDirected = true;
    }
    execution_logger.debug("ExpandAll: graphDirection = " + graphDirection + ", isDirected = " + std::to_string(isDirected));
    bool isDirectionRight = false;
    if (query.contains("direction")) {
        isDirectionRight = query["direction"] == "right";
        execution_logger.debug("ExpandAll: direction found: " + query["direction"].get<std::string>());
    }

    string queryString;

    NodeManager nodeManager(gc);

    while (true) {
        string raw = sharedBuffer.get();
        if (raw == "-1") {
            execution_logger.debug("ExpandAll: Received end signal from sharedBuffer");
            buffer.add(raw);
            result.join();
            break;
        }
        execution_logger.debug("ExpandAll: Processing input row: " + raw);
        json rawObj = json::parse(raw);
        string nodeId = rawObj[sourceVariable]["id"];
        execution_logger.debug("ExpandAll: nodeId = " + nodeId + ", partitionID = " + rawObj[sourceVariable]["partitionID"].get<std::string>());
        if (rawObj[sourceVariable]["partitionID"] == to_string(gc.partitionID)) {
            NodeBlock* node = nodeManager.get(nodeId);
            if (node) {
                execution_logger.debug("ExpandAll: Processing local relations for nodeId: " + nodeId);
                RelationBlock *relation = RelationBlock::getLocalRelation(node->edgeRef);
                if (relation) {
                    RelationBlock *nextRelation = relation;
                    bool isSource;
                    while (nextRelation) {
                      if (to_string(nextRelation->source.nodeId) == nodeId) {
                          isSource = true;
                          execution_logger.debug("ExpandAll: Relation source matches nodeId, isSource = true");
                      } else {
                          isSource = false;
                          execution_logger.debug("ExpandAll: Relation source does not match nodeId, isSource = false");
                      }

                      json relationData;
                      json destNodeData;
                      std::map<std::string, char*> relProperties = nextRelation->getAllProperties();
                      for (auto property : relProperties) {
                          relationData[property.first] = property.second;
                      }
                      execution_logger.debug("ExpandAll: Extracted relation properties: " + json(relProperties).dump());

                      if (relType != "" && relationData["type"] != relType) {
                          execution_logger.debug("ExpandAll: Skipping relation, type mismatch: " + relationData["type"].get<std::string>());
                          if (isSource) {
                              nextRelation = nextRelation->nextLocalSource();
                          } else {
                              nextRelation = nextRelation->nextLocalDestination();
                          }
                          continue;
                      }
                      if (isDirectionRight) {
                          if (!isSource)
                          {
                              execution_logger.debug("ExpandAll: Skipping relation, not source in directed graph");
                              nextRelation = nextRelation->nextLocalSource();
                              continue;

                          }

                      }
                        if (!isDirectionRight)
                        {
                            if (isSource)
                            {
                                execution_logger.debug("ExpandAll: Skipping relation, not destination left directed graph");

                                nextRelation = nextRelation->nextLocalDestination();
                                continue;
                            }
                        }


                      for (auto& [key, value] : relProperties) {
                          delete[] value;  // Free each allocated char* array
                      }
                      relProperties.clear();
                      NodeBlock *destNode;
                      if (isSource) {
                          destNode = nextRelation->getDestination();
                          execution_logger.debug("ExpandAll: isSource true, using getDestination()");
                      } else {
                          destNode = nextRelation->getSource();
                          execution_logger.debug("ExpandAll: isSource false, using getSource()");
                      }
                      std::string value(destNode->getMetaPropertyHead()->value);
                      destNodeData["partitionID"] = value;
                      std::map<std::string, char*> destProperties = destNode->getAllProperties();
                      for (auto property : destProperties) {
                          destNodeData[property.first] = property.second;
                      }
                      execution_logger.debug("ExpandAll: Extracted destNode properties: " + json(destProperties).dump());
                      for (auto& [key, value] : destProperties) {
                          delete[] value;  // Free each allocated char* array
                      }
                      destProperties.clear();
                      rawObj[relVariable] = relationData;
                      rawObj[destVariable] = destNodeData;
                      execution_logger.debug("ExpandAll: Updated rawObj with relation and destNode data: " + rawObj.dump());

                        execution_logger.debug("ExpandAll: Adding local relation result to buffer: " + rawObj.dump());

                        buffer.add(rawObj.dump());
                        if (isSource) {
                            nextRelation = nextRelation->nextLocalSource();
                        } else {
                            nextRelation = nextRelation->nextLocalDestination();
                        }
                    }
                } else {
                    execution_logger.debug("ExpandAll: No local relations found for nodeId: " + nodeId);
                }

                execution_logger.debug("ExpandAll: Processing central relations for nodeId: " + nodeId);
                relation = RelationBlock::getCentralRelation(node->centralEdgeRef);
                if (relation) {
                    RelationBlock *nextRelation = relation;
                    bool isSource;

                    while (nextRelation) {
                        if (to_string(nextRelation->source.nodeId) == nodeId) {
                            isSource = true;
                        } else {
                            isSource = false;
                        }

                        json relationData;
                        json destNodeData;
                        std::map<std::string, char*> relProperties = nextRelation->getAllProperties();
                        for (auto property : relProperties) {
                            relationData[property.first] = property.second;
                        }

                        if (relType != "" && relationData["type"] != relType) {
                            execution_logger.debug("ExpandAll: Skipping central relation, type mismatch: " + relationData["type"].get<std::string>());
                            if (isSource) {
                                nextRelation = nextRelation->nextCentralSource();
                            } else {
                                nextRelation = nextRelation->nextCentralDestination();
                            }
                            continue;
                        }

                        if (isDirectionRight) {
                            if (!isSource)
                            {
                                execution_logger.debug("ExpandAll: Skipping relation, not source in directed graph");
                                nextRelation = nextRelation->nextCentralSource();
                                continue;

                            }

                        }
                        if (!isDirectionRight)
                        {
                            if (isSource)
                            {
                                execution_logger.debug("ExpandAll: Skipping relation, not destination left directed graph");

                                nextRelation = nextRelation->nextCentralDestination();
                                continue;
                            }
                        }

                       for (auto& [key, value] : relProperties) {
                           execution_logger.debug("ExpandAll: Freeing relation property key: " + key);
                           delete[] value;  // Free each allocated char* array
                       }
                       execution_logger.debug("ExpandAll: Clearing relProperties map");
                       relProperties.clear();
                       NodeBlock *destNode;
                       if (isSource) {
                           execution_logger.debug("ExpandAll: isSource true, using getDestination()");
                           destNode = nextRelation->getDestination();
                       } else {
                           execution_logger.debug("ExpandAll: isSource false, using getSource()");
                           destNode = nextRelation->getSource();
                       }
                       std::string value(destNode->getMetaPropertyHead()->value);
                       execution_logger.debug("ExpandAll: destNode partitionID = " + value);
                       destNodeData["partitionID"] = value;
                       std::map<std::string, char*> destProperties = destNode->getAllProperties();
                       for (auto property : destProperties) {
                           execution_logger.debug("ExpandAll: Adding destNode property key: " + property.first);
                           destNodeData[property.first] = property.second;
                       }
                       for (auto& [key, value] : destProperties) {
                           execution_logger.debug("ExpandAll: Freeing destNode property key: " + key);
                           delete[] value;  // Free each allocated char* array
                       }
                       execution_logger.debug("ExpandAll: Clearing destProperties map");
                       destProperties.clear();
                       execution_logger.debug("ExpandAll: Assigning relationData and destNodeData to rawObj");
                       rawObj[relVariable] = relationData;
                       rawObj[destVariable] = destNodeData;
                        execution_logger.debug("ExpandAll: Adding central relation result to buffer: " + rawObj.dump());
                        buffer.add(rawObj.dump());
                        if (isSource) {
                            nextRelation = nextRelation->nextCentralSource();
                        } else {
                            nextRelation = nextRelation->nextCentralDestination();
                        }
                    }
                } else {
                    execution_logger.debug("ExpandAll: No central relations found for nodeId: " + nodeId);
                }
            } else {
                execution_logger.debug("ExpandAll: NodeBlock not found for nodeId: " + nodeId);
            }
        } else {
            execution_logger.debug("ExpandAll: Node is not in this partition, sending subquery to partition " + rawObj[sourceVariable]["partitionID"].get<std::string>());
            if (query.contains("relType")) {
                queryString = ExpandAllHelper::generateSubQuery(query["sourceVariable"],
                                                                query["destVariable"],
                                                                query["relVariable"],
                                                                isDirected,
                                                                isDirectionRight,
                                                                rawObj[sourceVariable]["id"], query["relType"]);
            } else {
                queryString = ExpandAllHelper::generateSubQuery(query["sourceVariable"],
                                                                query["destVariable"],
                                                                query["relVariable"],
                                                                isDirected,
                                                                isDirectionRight, rawObj[sourceVariable]["id"]);
            }
            string queryPlan = ExpandAllHelper::generateSubQueryPlan(queryString);
            execution_logger.debug("ExpandAll: Generated subquery plan: " + queryPlan);
            SharedBuffer temp(INTER_OPERATOR_BUFFER_SIZE);
            std::thread t(Utils::sendDataFromWorkerToWorker,
                          masterIP,
                          gc.graphID,
                          rawObj[sourceVariable]["partitionID"],
                          std::ref(queryPlan),
                          std::ref(temp));
            while (true) {
                string tmpRaw = temp.get();
                if (tmpRaw == "-1") {
                    t.join();
                    execution_logger.debug("ExpandAll: Received end signal from remote partition");
                    break;
                }
                json tmpData = json::parse(tmpRaw);
                rawObj[relVariable] = tmpData[relVariable];
                rawObj[destVariable] = tmpData[destVariable];
                execution_logger.debug("ExpandAll: Adding remote relation result to buffer: " + rawObj.dump());
                buffer.add(rawObj.dump());
            }
        }
    }
}

void OperatorExecutor::VarLengthExpandAll(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.debug("VarLengthExpandAll: Parsing query plan");
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];
    execution_logger.debug("VarLengthExpandAll: Launching next operator thread: " + next["Operator"].get<std::string>());
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);

    string sourceVariable = query["sourceVariable"];
    string destVariable   = query["destVariable"];
    string relVariable    = query["relVariable"];

    int minHops = query.contains("minHops") ?std::stoi(query["minHops"].get<std::string>()): 1;
    int maxHops = query.contains("maxHops") ? std::stoi(query["maxHops"].get<std::string>()): 1;
    execution_logger.debug("VarLengthExpandAll: minHops = " + std::to_string(minHops) +
                           ", maxHops = " + std::to_string(maxHops));

    string relType = "";
    if (query.contains("relType")) {
        relType = query["relType"];
        execution_logger.debug("VarLengthExpandAll: relType found: " + relType);
    }

    string graphDirection = Utils::getGraphDirection(to_string(gc.graphID), masterIP);
    bool isDirected = (graphDirection == "TRUE");
    execution_logger.debug("VarLengthExpandAll: graphDirection = " + graphDirection + ", isDirected = " + std::to_string(isDirected));

    bool isDirectionRight = false;
    if (query.contains("direction")) {
        isDirectionRight = query["direction"] == "right";
        execution_logger.debug("VarLengthExpandAll: direction found: " + query["direction"].get<std::string>());
    }

    NodeManager nodeManager(gc);

    while (true) {
        string raw = sharedBuffer.get();
        execution_logger.debug("VarLengthExpandAll: Received from sharedBuffer: " + raw);
        if (raw == "-1") {
            execution_logger.debug("VarLengthExpandAll: Received end signal from sharedBuffer");
            buffer.add(raw);
            result.join();
            execution_logger.debug("VarLengthExpandAll: Thread joined, breaking loop");
            break;
        }

        execution_logger.debug("VarLengthExpandAll: Processing input row: " + raw);
        json rawObj = json::parse(raw);
        string startNodeId = rawObj[sourceVariable]["id"];
        string startPartition = rawObj[sourceVariable]["partitionID"].get<std::string>();
        execution_logger.debug("VarLengthExpandAll: startNodeId = " + startNodeId + ", startPartition = " + startPartition);

        if (startPartition == to_string(gc.partitionID)) {
            execution_logger.debug("VarLengthExpandAll: Node is in this partition");
            NodeBlock* startNode = nodeManager.get(startNodeId);
            if (!startNode) {
                execution_logger.debug("VarLengthExpandAll: Start NodeBlock not found for nodeId: " + startNodeId);
                continue;
            }

            struct PathStep {
                NodeBlock* node;
                json pathObj;
                int depth;
            };

            std::queue<PathStep> frontier;
            json initialPath = rawObj;
            initialPath["pathNodes"] = json::array({ rawObj[sourceVariable] });
            initialPath["pathRels"]  = json::array();

            execution_logger.debug("VarLengthExpandAll: Pushing initial path to frontier");
            frontier.push({ startNode, initialPath, 0 });

            while (!frontier.empty()) {
                auto step = frontier.front();
                frontier.pop();

                NodeBlock* currentNode = step.node;
                json currentPath = step.pathObj;
                int depth = step.depth;

                execution_logger.debug("VarLengthExpandAll: Frontier pop: nodeId = " + to_string(currentNode->nodeId) + ", depth = " + std::to_string(depth));

                if (depth >= minHops && depth <= maxHops) {
                    execution_logger.debug("VarLengthExpandAll: Adding path result of length " + std::to_string(depth) + ": " + currentPath.dump());
                    json currentPathCopy = currentPath;
                    currentPathCopy["depth"] = depth;
                    currentPathCopy[destVariable]= currentPath["pathNodes"].back();


                    currentPathCopy[relVariable]=currentPathCopy;

                    // log currentPath

                    // currentPath[destVariable]= currentPath["pathNodes"].back();
                    // currentPath["b"] = currentPath["pathNodes"].back();
                    // execution_logger.debug(currentPath);
                    // execution_logger.debug(destVariable);

                     buffer.add(currentPathCopy.dump());
                    currentPathCopy.clear();
                }
                if (depth == maxHops) {
                    execution_logger.debug("VarLengthExpandAll: Reached maxHops, skipping further expansion");
                    continue;
                }

                // Expand local and central relations from currentNode
                auto expandRelations = [&](RelationBlock* relation, bool isCentral) {
                    execution_logger.debug("VarLengthExpandAll: Expanding " + std::string(isCentral ? "central" : "local") + " relations for nodeId: " + to_string(currentNode->nodeId));
                    RelationBlock* nextRelation = relation;
                    while (nextRelation) {
                        bool isSource = (to_string(nextRelation->source.nodeId) == to_string(currentNode->nodeId));
                        execution_logger.debug("VarLengthExpandAll: Relation nodeId = " + to_string(nextRelation->source.nodeId) + ", isSource = " + std::to_string(isSource));

                        json relationData;
                        std::map<std::string, char*> relProperties = nextRelation->getAllProperties();
                        for (auto property : relProperties) {
                            relationData[property.first] = property.second;
                            execution_logger.debug("VarLengthExpandAll: Relation property: " + property.first + " = " + property.second);
                        }

                        if (!relType.empty() && relationData["type"] != relType) {
                            execution_logger.debug("VarLengthExpandAll: Skipping relation, type mismatch: " + relationData["type"].get<std::string>());
                            nextRelation = isSource ?
                                (isCentral ? nextRelation->nextCentralSource() : nextRelation->nextLocalSource()) :
                                (isCentral ? nextRelation->nextCentralDestination() : nextRelation->nextLocalDestination());
                            for (auto& [k,v] : relProperties) {
                                execution_logger.debug("VarLengthExpandAll: Freeing relation property: " + k);
                                delete[] v;
                            }
                            continue;
                        }

                        if (isDirected) {
                            if (isDirectionRight && !isSource) {
                                execution_logger.debug("VarLengthExpandAll: Skipping relation, not source in directed graph");
                                nextRelation = nextRelation->nextLocalSource();
                                for (auto& [k,v] : relProperties) {
                                    execution_logger.debug("VarLengthExpandAll: Freeing relation property: " + k);
                                    delete[] v;
                                }
                                continue;
                            }
                            if (!isDirectionRight && isSource) {
                                execution_logger.debug("VarLengthExpandAll: Skipping relation, not destination left directed graph");
                                nextRelation = nextRelation->nextLocalDestination();
                                for (auto& [k,v] : relProperties) {
                                    execution_logger.debug("VarLengthExpandAll: Freeing relation property: " + k);
                                    delete[] v;
                                }
                                continue;
                            }
                        }

                        NodeBlock* destNode = isSource ? nextRelation->getDestination() : nextRelation->getSource();
                        execution_logger.debug("VarLengthExpandAll: destNodeId = " + to_string(destNode->nodeId));
                        json destNodeData;
                        std::map<std::string, char*> destProperties = destNode->getAllProperties();
                        destNodeData["partitionID"] = std::string(destNode->getMetaPropertyHead()->value);
                        for (auto property : destProperties) {
                            destNodeData[property.first] = property.second;
                            execution_logger.debug("VarLengthExpandAll: destNode property: " + property.first + " = " + property.second);
                        }

                        for (auto& [k,v] : relProperties) {
                            execution_logger.debug("VarLengthExpandAll: Freeing relation property: " + k);
                            delete[] v;
                        }
                        for (auto& [k,v] : destProperties) {
                            execution_logger.debug("VarLengthExpandAll: Freeing destNode property: " + k);
                            delete[] v;
                        }

                        json newPath = currentPath;
                        newPath["pathNodes"].push_back(destNodeData);
                        newPath["pathRels"].push_back(relationData);

                        execution_logger.debug("VarLengthExpandAll: Pushing new path to frontier, depth = " + std::to_string(depth + 1));
                        frontier.push({ destNode, newPath, depth + 1 });

                        nextRelation = isSource ?
                            (isCentral ? nextRelation->nextCentralSource() : nextRelation->nextLocalSource()) :
                            (isCentral ? nextRelation->nextCentralDestination() : nextRelation->nextLocalDestination());
                    }
                };

                // Local relations
                RelationBlock* localRel = RelationBlock::getLocalRelation(currentNode->edgeRef);
                if (localRel) {
                    execution_logger.debug("VarLengthExpandAll: Expanding local relations for nodeId: " + to_string(currentNode->nodeId));
                    expandRelations(localRel, false);
                } else {
                    execution_logger.debug("VarLengthExpandAll: No local relations for nodeId: " + to_string(currentNode->nodeId));
                }

                // Central relations
                RelationBlock* centralRel = RelationBlock::getCentralRelation(currentNode->centralEdgeRef);
                if (centralRel) {
                    execution_logger.debug("VarLengthExpandAll: Expanding central relations for nodeId: " + to_string(currentNode->nodeId));
                    expandRelations(centralRel, true);
                } else {
                    execution_logger.debug("VarLengthExpandAll: No central relations for nodeId: " + to_string(currentNode->nodeId));
                }
            }
        } else {
            // Remote partition handling: forward subquery similar to ExpandAll but with hop limits
            execution_logger.debug("VarLengthExpandAll: Node is not in this partition, forwarding variable-length query to partition " + startPartition);
            string queryString = ExpandAllHelper::generateVarLengthSubQuery(
                query["sourceVariable"], query["destVariable"], query["relVariable"],
                minHops, maxHops, isDirected, isDirectionRight, rawObj[sourceVariable]["id"],
                relType.empty() ? "" : query["relType"]);

            execution_logger.debug("VarLengthExpandAll: Generated subquery string: " + queryString);
            string queryPlan = ExpandAllHelper::generateSubQueryPlan(queryString);
            execution_logger.debug("VarLengthExpandAll: Generated subquery plan: " + queryPlan);
            SharedBuffer temp(INTER_OPERATOR_BUFFER_SIZE);
            std::thread t(Utils::sendDataFromWorkerToWorker, masterIP, gc.graphID,
                          rawObj[sourceVariable]["partitionID"], std::ref(queryPlan), std::ref(temp));
            execution_logger.debug("VarLengthExpandAll: Started remote worker thread");

            while (true) {
                string tmpRaw = temp.get();
                execution_logger.debug("VarLengthExpandAll: Received from remote partition: " + tmpRaw);
                if (tmpRaw == "-1") {
                    t.join();
                    execution_logger.debug("VarLengthExpandAll: Remote thread joined, breaking loop");
                    break;
                }
                buffer.add(tmpRaw);
            }
        }
    }
    execution_logger.debug("VarLengthExpandAll: Finished processing all input rows");
}


void OperatorExecutor::AggregationFunction(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.debug("AggregationFunction: Parsing query plan");
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];
    execution_logger.debug("AggregationFunction: Launching next operator thread: " + next["Operator"].get<std::string>());
    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);
    execution_logger.debug("AggregationFunction: Creating AverageAggregationHelper for variable: " + query["variable"].get<std::string>() + ", property: " + query["property"].get<std::string>());
    string functionName = query["FunctionName"].get<std::string>();
    string variable = query["variable"].get<std::string>();
    string property = query.contains("property") ? query["property"].get<std::string>() : "*";
    AggregationHelper* averageAggregationHelper = AggregationHelperFactory::getAggregationMethod(functionName, variable, property );
    while (true) {
        string raw = sharedBuffer.get();
        execution_logger.debug("AggregationFunction: Received from sharedBuffer: " + raw);
        if (raw == "-1") {
            execution_logger.debug("AggregationFunction: Received end signal, adding final result to buffer");
            buffer.add(averageAggregationHelper->getFinalResult());
            buffer.add(raw);
            result.join();
            execution_logger.debug("AggregationFunction: Thread joined, breaking loop");
            break;
        }
        execution_logger.debug("AggregationFunction: Inserting data into AverageAggregationHelper: " + raw);
        averageAggregationHelper->insertData(raw);
    }
}

void OperatorExecutor::Projection(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    execution_logger.debug("Projection: Parsing query plan");
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];

    execution_logger.debug("Projection: Launching next operator thread: " + next["Operator"].get<std::string>());
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);
    if (!query.contains("project") || !query["project"].is_array()) {
        execution_logger.debug("Projection: No project array found, forwarding all results");
        while (true) {
            string raw = sharedBuffer.get();
            execution_logger.debug("Projection: Received raw = " + raw);
            buffer.add(raw);
            if (raw == "-1") {
                execution_logger.debug("Projection: Received end signal, joining thread");
                result.join();
                break;
            }
        }
    } else {
        execution_logger.debug("Projection: Project array found, processing projection");
        while (true) {
            string raw = sharedBuffer.get();
            execution_logger.debug("Projection: Received raw = " + raw);
            if (raw == "-1") {
                execution_logger.debug("Projection: Received end signal, joining thread");
                buffer.add(raw);
                result.join();
                break;
            }
            auto data = json::parse(raw);
            for (const auto& operand : query["project"]) {
                for (auto& [key, value] : data.items()) {
                    if (operand.contains("variable") && key == operand["variable"]) {
                        string assign = operand["assign"];
                        string property = operand["property"];
                        execution_logger.debug("Projection: Assigning property '" + property + "' of variable '" + key + "' to '" + assign + "'");
                        data[assign] = value[property];
                    } else if (operand.contains("functionName") && key == operand["functionName"]) {
                        string assign = operand["assign"];
                        execution_logger.debug("Projection: Assigning function result '" + key + "' to '" + assign + "'");
                        data["variable"] = assign;
                        data[assign] = value;
                    }
                }
            }
            execution_logger.debug("Projection: Adding projected data to buffer: " + data.dump());
            buffer.add(data.dump());
        }
    }
}

void OperatorExecutor::Create(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    string partitionAlgo = Utils::getPartitionAlgorithm(to_string(gc.graphID), masterIP);
    CreateHelper createHelper(query["elements"], partitionAlgo, gc, masterIP);
    if (query.contains("NextOperator")) {
        std::string nextOpt = query["NextOperator"];
        json next = json::parse(nextOpt);
        auto method = OperatorExecutor::methodMap[next["Operator"]];
        // Launch the method in a new thread
        std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);
        while (true) {
            string raw = sharedBuffer.get();
            if (raw == "-1") {
                buffer.add(raw);
                result.join();
                break;
            }
            createHelper.insertFromData(raw, std::ref(buffer));
        }
    } else {
        createHelper.insertWithoutData(std::ref(buffer));
        buffer.add("-1");
    }
}

void OperatorExecutor::CartesianProduct(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    SharedBuffer left(INTER_OPERATOR_BUFFER_SIZE);
    SharedBuffer right(INTER_OPERATOR_BUFFER_SIZE);
    std::string leftOpt = query["left"];
    std::string rightOpt = query["right"];
    json leftJson = json::parse(leftOpt);
    json rightJson = json::parse(rightOpt);
    auto leftMethod = OperatorExecutor::methodMap[leftJson["Operator"]];
    auto rightMethod = OperatorExecutor::methodMap[rightJson["Operator"]];
    // Launch the method in a new thread
    std::thread leftThread(leftMethod, std::ref(*this), std::ref(left), query["left"], gc);
    while (true) {
        string leftRaw = left.get();
        if (leftRaw == "-1") {
            buffer.add(leftRaw);
            leftThread.join();
            break;
        }

        string partitionCount = Utils::getJasmineGraphProperty("org.jasminegraph.server.npartitions");
        int numberOfPartitions = std::stoi(partitionCount);
        std::vector<std::thread> workerThreads;

        for (int i = 0; i < numberOfPartitions; i++) {
            if (i == gc.partitionID) {
                continue;
            }
            workerThreads.emplace_back(
                    Utils::sendDataFromWorkerToWorker,
                    masterIP,
                    gc.graphID,
                    to_string(i),
                    query["right"],
                    std::ref(right));
        }

        std::thread rightThread(rightMethod, std::ref(*this), std::ref(right), query["right"], gc);
        int count = 0;
        while (true) {
            string rightRaw = right.get();
            if (rightRaw == "-1") {
                count++;
                if (count == numberOfPartitions) {
                    buffer.add("-1");
                    rightThread.join();
                    for (auto& t : workerThreads) {
                        if (t.joinable()) {
                            t.join();
                        }
                    }
                }
                continue;
            }



            json leftData = json::parse(leftRaw);
            json rightData = json::parse(rightRaw);

            for (auto& [key, value] : rightData.items()) {
                leftData[key] = value;
            }
            buffer.add(leftData.dump());
        }
    }
}

void OperatorExecutor::Distinct(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];

    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);
    if (!query.contains("project") || !query["project"].is_array()) {
        while (true) {
            string raw = sharedBuffer.get();
            buffer.add(raw);
            if (raw == "-1") {
                result.join();
                break;
            }
        }
    } else {
        while (true) {
            string raw = sharedBuffer.get();
            if (raw == "-1") {
                buffer.add(raw);
                result.join();
                break;
            }
            auto data = json::parse(raw);
            for (const auto& operand : query["project"]) {
                for (auto& [key, value] : data.items()) {
                    if (operand.contains("variable") && key == operand["variable"]) {
                        string assign = operand["assign"];
                        string property = operand["property"];
                        data[assign] = value[property];
                    } else if (operand.contains("functionName") && key == operand["functionName"]) {
                        string assign = operand["assign"];
                        data["variable"] = assign;
                        data[assign] = value;
                    }
                }
            }
            buffer.add(data.dump());
        }
    }
}

struct Row {
    json data;
    std::string jsonStr;
    std::string sortKey;
    bool isAsc;

    Row(const std::string& str, const std::string& key, bool asc)
        : jsonStr(str), sortKey(key), isAsc(asc) {
        data = json::parse(str);
    }

    json getNestedValue(const json& obj, const std::string& key) const {
        json current = obj;
        std::stringstream ss(key);
        std::string token;

        while (std::getline(ss, token, '.')) {
            if (!current.contains(token)) {
                return nullptr;
            }
            current = current[token];
        }
        return current;
    }

    bool operator<(const Row& other) const {
        json val1 = getNestedValue(data, sortKey);
        json val2 = getNestedValue(other.data, sortKey);

        bool result;
        if (val1.is_number_integer() && val2.is_number_integer()) {
            result = val1.get<int>() > val2.get<int>();
        } else if (val1.is_string() && val2.is_string()) {
            result = val1.get<std::string>() > val2.get<std::string>();
        } else {
            result = val1.dump() > val2.dump();
        }
        return isAsc ? result : !result;  // Flip for DESC
    }
};

void OperatorExecutor::OrderBy(SharedBuffer &buffer, std::string jsonPlan, GraphConfig gc) {
    json query = json::parse(jsonPlan);
    SharedBuffer sharedBuffer(INTER_OPERATOR_BUFFER_SIZE);
    std::string nextOpt = query["NextOperator"];
    json next = json::parse(nextOpt);
    auto method = OperatorExecutor::methodMap[next["Operator"]];

    // Launch the method in a new thread
    std::thread result(method, std::ref(*this), std::ref(sharedBuffer), query["NextOperator"], gc);

    std::string sortKey = query["variable"];
    std::string order = query["order"];
    const size_t MAX_SIZE = 5000;
    bool isAsc = (order == "ASC");

    std::priority_queue<Row> heap;
    while (true) {
        std::string jsonStr = sharedBuffer.get();
        if (jsonStr == "-1") {
            while (!heap.empty()) {
                buffer.add(heap.top().jsonStr);
                heap.pop();
            }
            buffer.add(jsonStr);  // -1 close flag
            result.join();
            break;
        }

        try {
            Row row(jsonStr, sortKey, isAsc);
            json nestedVal = row.getNestedValue(row.data, sortKey);

            if (nestedVal.is_null()) {
                execution_logger.warn("OrderBy: Sort key '" + sortKey + "' not found in row: " + jsonStr);
                continue;
            }
            heap.push(row);
            if (heap.size() > MAX_SIZE) {
                execution_logger.info("OrderBy: Heap size exceeded MAX_SIZE, popping");
                heap.pop();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << "\n";
        }
    }
}
