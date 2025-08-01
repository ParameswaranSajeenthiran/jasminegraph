/**
Copyright 2024 JasmineGraph Team
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

#include "Operators.h"
#include <nlohmann/json.hpp>
#include <map>
#include <boost/tuple/detail/tuple_basic.hpp>

#include "QueryPlanner.h"
#include "../../../../frontend/core/executor/impl/CypherQueryExecutor.h"
#include "../../../../server/JasmineGraphServer.h"
#include "../util/Const.h"
#include "../astbuilder/ASTNode.h"
#include "../runtime/AggregationFactory.h"
#include "../util/SharedBuffer.h"
using namespace std;
Logger operatorLogger;
using json = nlohmann::json;

bool Operator::isGroupBy = false;
bool Operator::isAggregate = false;
int Operator::limit = -1;
std::string Operator::aggregateType = "";
std::string Operator::aggregateKey = "";

// NodeScan Implementation
NodeScanByLabel::NodeScanByLabel(string label, string var) : label(label), var(var) {}

string NodeScanByLabel::execute() {
    json nodeByLabel;
    nodeByLabel["Operator"] = "NodeScanByLabel";
    nodeByLabel["variable"] = var;
    nodeByLabel["Label"] = label;
    return nodeByLabel.dump();
}

// MultipleNodeScanByLabel Implementation
MultipleNodeScanByLabel::MultipleNodeScanByLabel(vector<string> label, const string& var) : label(label), var(var) {}

string MultipleNodeScanByLabel::execute() {
    json multipleNodeByLabel;
    multipleNodeByLabel["Operator"] = "MultipleNodeScanByLabel";
    multipleNodeByLabel["variables"] = var;
    multipleNodeByLabel["Label"] = label;
    return multipleNodeByLabel.dump();
}

// NodeByIdSeek Implementation
NodeByIdSeek::NodeByIdSeek(string id, string var) : id(id), var(var) {}

string NodeByIdSeek::execute() {
    json nodeByIdSeek;
    nodeByIdSeek["Operator"] = "NodeByIdSeek";
    nodeByIdSeek["variable"] = var;
    nodeByIdSeek["id"] = id;
    return nodeByIdSeek.dump();
}

// AllNodeScan Implementation
AllNodeScan::AllNodeScan(const string& var) : var(var) {}

string AllNodeScan::execute() {
    json allNodeScan;
    allNodeScan["Operator"] = "AllNodeScan";
    allNodeScan["variables"] = var;
    return allNodeScan.dump();
}

// ProduceResults Implementation
ProduceResults::ProduceResults(Operator* opr, vector<ASTNode*> item) : item(item), op(opr) {}

string ProduceResults::execute() {
    json produceResult;
    produceResult["Operator"] = "ProduceResult";
    produceResult["variable"] = json::array();
    if (op) {
        produceResult["NextOperator"] = op->execute();
    }

    for (auto* result : item) {
        if (result->nodeType == Const::AS) {
            if (result->elements[0]->nodeType == Const :: FUNCTION_BODY)
            {                    auto nonArithmetic = result->elements[0]->elements[1]->elements[0];

                if (nonArithmetic->elements.empty())
                {
                    string variable = nonArithmetic->value;
                    string property ="*";
                    produceResult["variable"].push_back("variable");
                    produceResult["variable"].push_back("numberOfData");
                }
                else
                {
                    string variable = nonArithmetic->elements[0]->value;
                    string property = nonArithmetic->elements[1]->elements[0]->value;
                    produceResult["variable"].push_back("variable");
                    produceResult["variable"].push_back("numberOfData");
                }
                if (Operator::isGroupBy)
                {
                    produceResult["variable"].push_back("groupByKey");
                }
            }
            produceResult["variable"].push_back(result->elements[1]->value);
        } else if (result->nodeType == Const::NON_ARITHMETIC_OPERATOR) {
            produceResult["variable"].push_back(result->elements[0]->value + "." +
            result->elements[1]->elements[0]->value);
        } else if (result->nodeType == Const::VARIABLE) {
            produceResult["variable"].push_back(result->value);
        } else if (result->nodeType == Const::FUNCTION_BODY) {
            auto nonArithmetic = result->elements[1]->elements[0];
                if (nonArithmetic->elements.empty())
            {
                string variable = nonArithmetic->value;
                string property = "*";
                produceResult["variable"].push_back(result->elements[0]->elements[1]->value
                        + "(" + variable + "." + property + ")");
                produceResult["variable"].push_back("variable");
                produceResult["variable"].push_back("numberOfData");
            } else
            {
                string variable = nonArithmetic->elements[0]->value;
                string property = nonArithmetic->elements[1]->elements[0]->value;
                produceResult["variable"].push_back(result->elements[0]->elements[1]->value
                        + "(" + variable + "." + property + ")");
                produceResult["variable"].push_back("variable");
                produceResult["variable"].push_back("numberOfData");
            }
            if (Operator::isGroupBy)
            {
                produceResult["variable"].push_back("groupByKey");
            }
        }
    }
    return produceResult.dump();
}

Operator *ProduceResults::getOperator() {
    return this->op;
}

void ProduceResults::setOperator(Operator *op) {
    this->op = op;
}

// Filter Implementation
Filter::Filter(Operator* input, vector<pair<string, ASTNode*>> filterCases) : input(input), filterCases(filterCases) {}

string Filter::comparisonOperand(ASTNode *ast) {
    json operand;
    if (ast->nodeType == Const::NON_ARITHMETIC_OPERATOR) {
        // there are more cases to handle
        operand["variable"] = ast->elements[0]->value;
        operand["type"] = Const::PROPERTY_LOOKUP;
        vector<string> property;
        for (auto* prop : ast->elements) {
            if (prop->nodeType == Const::PROPERTY_LOOKUP && prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                property.push_back(prop->elements[0]->value);
            }
        }
        operand["property"] = property;
    } else if (ast->nodeType == Const::PROPERTIES_MAP) {
        operand["type"] = Const::PROPERTIES_MAP;
        map<string, string> property;
        for (auto* prop : ast->elements) {
            if (prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                property.insert(pair<string, string>(prop->elements[0]->value, prop->elements[1]->value));
            }
        }
        operand["property"] = property;
    } else if (ast->nodeType == Const::LIST) {
        operand["type"] = Const::LIST;
        vector<string> element;
        for (auto* prop : ast->elements) {
            element.push_back(prop->value);
        }
        operand["element"] = element;
    } else if (ast->nodeType == Const::FUNCTION_BODY) {
        operand["type"] = Const::FUNCTION;
        operand["functionName"] = ast->elements[0]->elements[1]->value;
        vector<string> arguments;
        for (auto *arg : ast->elements[1]->elements) {
            arguments.push_back(arg->value);
        }
        operand["arguments"] = arguments;
    } else if (ast->nodeType == Const::IS_NOT_NULL || ast->nodeType == Const::IS_NULL) {
        operand["type"] = Const::NULL_STRING;
    } else {
        operand["type"] = ast->nodeType;
        operand["value"] = ast->value;
    }
    return operand.dump();
}

string Filter::analyzeWhere(ASTNode* ast) {
    json where;
    if (ast->nodeType == Const::OR) {
        where["type"] = Const::OR;
        vector<json> comparisons;
        for (auto* element : ast->elements) {
            comparisons.push_back(json::parse(analyzeWhere(element)));
        }
        where["comparisons"] = comparisons;
    } else if (ast->nodeType == Const::AND) {
        where["type"] = Const::AND;
        vector<json> comparisons;
        for (auto* element : ast->elements) {
            comparisons.push_back(json::parse(analyzeWhere(element)));
        }
        where["comparisons"] = comparisons;
    } else if (ast->nodeType == Const::XOR) {
        where["type"] = Const::XOR;
        vector<string> comparisons;
        for (auto* element : ast->elements) {
            comparisons.push_back(json::parse(analyzeWhere(element)));
        }
        where["comparisons"] = comparisons;
    } else if (ast->nodeType == Const::NOT) {
        where["type"] = Const::NOT;
        vector<json> comparisons;
        for (auto* element : ast->elements) {
            comparisons.push_back(json::parse(analyzeWhere(element)));
        }
        where["comparisons"] = comparisons;
    } else if (ast->nodeType == Const::COMPARISON) {
        where["type"] = Const::COMPARISON;
        auto* left = ast->elements[0];
        auto* oparator = ast->elements[1];
        auto* right = oparator->elements[0];
        where["left"] = json::parse(comparisonOperand(left));
        where["operator"] = oparator->nodeType;
        where["right"] = json::parse(comparisonOperand(right));
    } else if (ast->nodeType == Const::PREDICATE_EXPRESSIONS) {
        where["type"] = Const::PREDICATE_EXPRESSIONS;
        auto* left = ast->elements[0];
        auto* opr = ast->elements[1];
        auto* right = opr->elements[0];
        where["left"] = json::parse(comparisonOperand(left));
        if (opr->elements[0]->nodeType == Const::IS_NOT_NULL) {
            where["operator"] = Const::GREATER_THAN_LOWER_THAN;
        } else {
            where["operator"] = Const::DOUBLE_EQUAL;
        }
        where["right"] = json::parse(comparisonOperand(right));
    }
    return where.dump();
}

string Filter::analyzeNodeLabels(pair<std::string, ASTNode *> item) {
    json condition;
    json left;
    json right;
    left["type"] = Const::PROPERTY_LOOKUP;
    left["property"] = json::array({"label"});
    left["variable"] = item.first;
    right["type"] = Const::STRING;
    right["value"] = item.second->elements[0]->value;
    condition["left"] = left;
    condition["operator"] = Const::DOUBLE_EQUAL;
    condition["right"] = right;
    condition["type"] = Const::COMPARISON;
    return condition.dump();
}

string Filter::analyzePropertiesMap(pair<std::string, ASTNode *> item) {
    json condition;
    if (item.second->elements.size() > 1) {
        condition["type"] = Const::AND;
        vector<json> comparisons;
        for (auto* prop : item.second->elements) {
            json comparison;
            json left;
            json right;
            left["type"] = Const::PROPERTY_LOOKUP;
            left["property"] = json::array({prop->elements[0]->value});
            left["variable"] = item.first;
            right["type"] = prop->elements[1]->nodeType;
            right["value"] = prop->elements[1]->value;
            comparison["left"] = left;
            comparison["operator"] = Const::DOUBLE_EQUAL;
            comparison["right"] = right;
            comparison["type"] = Const::COMPARISON;
            comparisons.push_back(comparison);
        }
        condition["comparisons"] = comparisons;
    } else {
        auto prop = item.second->elements[0];
        json left;
        json right;
        left["type"] = Const::PROPERTY_LOOKUP;
        left["property"] = json::array({prop->elements[0]->value});
        left["variable"] = item.first;
        right["type"] = prop->elements[1]->nodeType;
        right["value"] = prop->elements[1]->value;
        condition["left"] = left;
        condition["operator"] = Const::DOUBLE_EQUAL;
        condition["right"] = right;
        condition["type"] = Const::COMPARISON;
    }
    return condition.dump();
}

string Filter::execute() {
    json filter;
    if (input) {
        filter["NextOperator"] = input->execute();
    }
    filter["Operator"] = "Filter";
    for (auto item : filterCases) {
        if (item.second->nodeType == Const::WHERE) {
            filter["condition"] = json::parse(analyzeWhere(item.second->elements[0]));
        } else if (item.second->nodeType == Const::PROPERTIES_MAP) {
            filter["condition"] = json::parse(analyzePropertiesMap(item));
        } else if (item.second->nodeType == Const::NODE_LABEL) {
            filter["condition"] = json::parse(analyzeNodeLabels(item));
        }
    }
    return filter.dump();
}

// Projection Implementation
Projection::Projection(Operator* input, const vector<ASTNode*> columns) : input(input), columns(columns) {}

string Projection::execute() {
    json projection;
    if (input) {
        projection["NextOperator"] = input->execute();
    }
    projection["Operator"] = "Projection";
    projection["project"] = json::array();  // Initialize as an empty array

    for (auto* ast : columns) {
        json operand;

        if (ast->nodeType == Const::NON_ARITHMETIC_OPERATOR) {
            string variable = ast->elements[0]->value;
            string property = ast->elements[1]->elements[0]->value;
            operand["Type"] = Const::PROPERTY_LOOKUP;
            operand["variable"] = variable;
            operand["property"] = property;
            operand["assign"] = variable + "." + property;
        } else if (ast->nodeType == Const::AS) {
            if (ast->elements[0]->nodeType == Const::FUNCTION_BODY) {
                auto function = ast->elements[0];

                operand["functionName"] = function->elements[0]->elements[1]->value;

                if (Operator::isGroupBy){
                    auto function = ast->elements[0];
                    auto nonArithmetic = ast->elements[0]->elements[1]->elements[0];
                    string variable = nonArithmetic->elements[0]->value;
                    string property = nonArithmetic->elements[1]->elements[0]->value;
                    operand["functionName"] = function->elements[0]->elements[1]->value + "(" + variable + "." + property + ")";


                }
            } else {


                auto lookupOpr = ast->elements[0];
                operand["Type"] = Const::PROPERTY_LOOKUP;
                operand["variable"] = lookupOpr->elements[0]->value;
                operand["property"] = lookupOpr->elements[1]->elements[0]->value;
            }
            operand["assign"] = ast->elements[1]->value;



        } else if (ast->nodeType == Const::VARIABLE) {
            continue;
        } else if ( QueryPlanner::isAvailable(Const::FUNCTION_BODY, ast)) {

            auto nonArithmetic = ast->elements[1]->elements[0];
            if (nonArithmetic->elements.empty())
            {
                string variable = nonArithmetic->value;
                string property = "*";
                operand["functionName"] = ast->elements[0]->elements[1]->value;
                operand["assign"] = ast->elements[0]->elements[1]->value + "(" + variable + "." + property + ")";
            }else
            {
                string variable = nonArithmetic->elements[0]->value;
                string property = nonArithmetic->elements[1]->elements[0]->value;
                operand["functionName"] = ast->elements[0]->elements[1]->value;
                operand["assign"] = ast->elements[0]->elements[1]->value + "(" + variable + "." + property + ")";
            }

        }

        projection["project"].push_back(operand);  // Append operand to the array
    }

    return projection.dump();  // Print the final projection JSON with indentation
}


// Limit Implementation
Limit::Limit(Operator* input, ASTNode* limit) : input(input), limit(limit) {}

string Limit::execute() {
    input->execute();
    operatorLogger.debug("Skipping first" + limit->print());
    return "";
}

// Skip Implementation
Skip::Skip(Operator* input, ASTNode* skip) : input(input), skip(skip) {}

string Skip::execute() {
    input->execute();
    operatorLogger.debug("Skipping first" + skip->print());
    return "";
}


// Distinct Implementation
Distinct::Distinct(Operator* input, const vector<ASTNode*> columns) : input(input), columns(columns) {}

string Distinct::execute() {
    json distinct;
    if (input) {
        distinct["NextOperator"] = input->execute();
    }
    distinct["Operator"] = "Distinct";
    distinct["project"] = json::array();  // Initialize as an empty array

    for (auto* ast : columns) {
        json operand;

        if (ast->nodeType == Const::NON_ARITHMETIC_OPERATOR) {
            string variable = ast->elements[0]->value;
            string property = ast->elements[1]->elements[0]->value;
            operand["Type"] = Const::PROPERTY_LOOKUP;
            operand["variable"] = variable;
            operand["property"] = property;
            operand["assign"] = variable + "." + property;
        } else if (ast->nodeType == Const::AS) {
            auto lookupOpr = ast->elements[0];
            operand["Type"] = Const::PROPERTY_LOOKUP;
            operand["variable"] = lookupOpr->elements[0]->value;
            operand["property"] = lookupOpr->elements[1]->elements[0]->value;
            operand["assign"] = ast->elements[1]->value;
        } if (ast->nodeType == Const::VARIABLE) {
            continue;
        }
        distinct["project"].push_back(operand);
    }
    return distinct.dump();
}

OrderBy::OrderBy(Operator* input, ASTNode* orderByClause) : input(input), orderByClause(orderByClause) {}

string OrderBy::execute() {
    json orderBy;
    if (input) {
        orderBy["NextOperator"] = input->execute();
    }
    orderBy["Operator"] = "OrderBy";
    if (this->orderByClause->nodeType == Const::ASC) {
        orderBy["order"] = "ASC";
    } else {
        orderBy["order"] = "DESC";
    }
    if (this->orderByClause->elements[0]->nodeType == Const::VARIABLE) {
        orderBy["variable"] = this->orderByClause->elements[0]->value;
    } else if (this->orderByClause->elements[0]->nodeType == Const::NON_ARITHMETIC_OPERATOR) {
        auto nonArithmeticOperator = this->orderByClause->elements[0];
        orderBy["variable"] = nonArithmeticOperator->elements[0]->value + "." +
                nonArithmeticOperator->elements[1]->elements[0]->value;
    }
    Operator::isAggregate = true;
    Operator::aggregateType = orderBy["order"];
    Operator::aggregateKey = orderBy["variable"];
    return orderBy.dump();
}

// Union Implementation
Union::Union(Operator* left, Operator* right) : left(left), right(right) {}

string Union::execute() {
    left->execute();
    right->execute();
    operatorLogger.debug("Performing Union of results.");
    return "";
}

// Intersection Implementation
Intersection::Intersection(Operator* left, Operator* right) : left(left), right(right) {}

string Intersection::execute() {
    left->execute();
    right->execute();
    operatorLogger.debug("Performing Intersection of results.");
    return "";
}

CacheProperty::CacheProperty(Operator* input, vector<ASTNode*> property) : property(property), input(input) {}

string CacheProperty::execute() {
    return input->execute();;
}

UndirectedRelationshipTypeScan::UndirectedRelationshipTypeScan(string relType, string relvar, string startVar,
                                                               string endVar)
        : relType(relType), relvar(relvar), startVar(startVar), endVar(endVar) {}

// Execute method
string UndirectedRelationshipTypeScan::execute() {
    json undirected;
    undirected["Operator"] = "UndirectedRelationshipTypeScan";
    undirected["sourceVariable"] = startVar;
    undirected["destVariable"] = endVar;
    undirected["relVariable"] = relvar;
    undirected["relType"] = relType;
    return undirected.dump();
}

UndirectedAllRelationshipScan::UndirectedAllRelationshipScan(string startVar, string endVar, string relVar)
        : startVar(startVar), endVar(endVar), relVar(relVar) {}


string UndirectedAllRelationshipScan::execute() {
    json undirected;
    undirected["Operator"] = "UndirectedAllRelationshipScan";
    undirected["sourceVariable"] = startVar;
    undirected["destVariable"] = endVar;
    undirected["relVariable"] = relVar;
    return undirected.dump();
}

DirectedRelationshipTypeScan::DirectedRelationshipTypeScan(string direction, string relType,
                                                           string relvar, string startVar, string endVar)
        : relType(relType), relvar(relvar), startVar(startVar), endVar(endVar), direction(direction) {}


// Execute method
string DirectedRelationshipTypeScan::execute() {
    json directed;
    directed["Operator"] = "DirectedRelationshipTypeScan";
    directed["sourceVariable"] = startVar;
    directed["destVariable"] = endVar;
    directed["relVariable"] = relvar;
    directed["relType"] = relType;
    directed["direction"] = direction;
    return directed.dump();
}


DirectedAllRelationshipScan::DirectedAllRelationshipScan(std::string direction, std::string startVar,
                                                         std::string endVar, std::string relVar)
        : startVar(startVar), endVar(endVar), relVar(relVar), direction(direction) {}

string DirectedAllRelationshipScan::execute() {
    json directed;
    directed["Operator"] = "DirectedAllRelationshipScan";
    directed["sourceVariable"] = startVar;
    directed["destVariable"] = endVar;
    directed["relVariable"] = relVar;
    directed["direction"] = direction;
    return directed.dump();
}

ExpandAll::ExpandAll(Operator *input, std::string startVar, std::string destVar, std::string relVar,
                     std::string relType, std::string direction)
                     : input(input), relType(relType), relVar(relVar), startVar(startVar),
                     destVar(destVar), direction(direction) {}

string ExpandAll::execute() {
    json expandAll;
    expandAll["Operator"] = "ExpandAll";
    expandAll["NextOperator"] = input->execute();
    expandAll["sourceVariable"] = startVar;
    expandAll["destVariable"] = destVar;
    expandAll["relVariable"] = relVar;
    if (relType != "null" && direction == "") {
        expandAll["relType"] = relType;
    } else if (relType == "null" && direction == "right") {
        expandAll["direction"] = direction;
    } else if (relType != "null" && direction == "right") {
        expandAll["relType"] = relType;
        expandAll["direction"] = direction;
    } else if (relType == "null" && direction == "left") {
        expandAll["direction"] = direction;
    } else if (relType != "null" && direction == "left") {
        expandAll["relType"] = relType;
        expandAll["direction"] = direction;
    }
    return expandAll.dump();
}


VarLengthExpandAll::VarLengthExpandAll(Operator *input, std::string startVar, std::string destVar, std::string relVar,
                     std::string relType, std::string direction , string minHops, string maxHops)
                     : input(input), relType(relType), relVar(relVar), startVar(startVar),
                     destVar(destVar), direction(direction), minHops(minHops), maxHops(maxHops) {}

string VarLengthExpandAll::execute() {
    json varLengthExpandAll;
    varLengthExpandAll["Operator"] = "VarLengthExpandAll";
    varLengthExpandAll["NextOperator"] = input->execute();
    varLengthExpandAll["sourceVariable"] = startVar;
    varLengthExpandAll["destVariable"] = destVar;
    varLengthExpandAll["relVariable"] = relVar;
    varLengthExpandAll["minHops"] = minHops;
    varLengthExpandAll["maxHops"] = maxHops;
    if (relType != "null" && direction == "") {
        varLengthExpandAll["relType"] = relType;
    } else if (relType == "null" && direction == "right") {
        varLengthExpandAll["direction"] = direction;
    } else if (relType != "null" && direction == "right") {
        varLengthExpandAll["relType"] = relType;
        varLengthExpandAll["direction"] = direction;
    } else if (relType == "null" && direction == "left") {
        varLengthExpandAll["direction"] = direction;
    } else if (relType != "null" && direction == "left") {
        varLengthExpandAll["relType"] = relType;
        varLengthExpandAll["direction"] = direction;
    }
    return varLengthExpandAll.dump();
}


Apply::Apply(Operator* operator1) : operator1(operator1) {}

void Apply::addOperator(Operator *operator2) {
    this->operator2 = operator2;
}

Operator* Apply:: getNextOperator(Operator *operator2) {
    return this->operator2;
}


// Execute method
string Apply::execute() {


    if (operator1 != nullptr) {
        operatorLogger.debug("left of Apply");
        operator1->execute();
    }
    if (operator2 != nullptr) {
        operatorLogger.debug("right of Apply");
        operator2->execute();
    }

    operatorLogger.debug("Merged left and right of Apply");
    json apply;
    apply["Operator"] = "Apply";
    if (operator1 != nullptr) {
        apply["NextOperator"] = operator1->execute();
    }
    if (operator2 != nullptr) {
        apply["NextOperator2"] = operator2->execute();
    }
    Operator::isApply = true;
    apply["isApply"] = Operator::isApply;
    return apply.dump();

}


void Apply::executeDistributed(const std::string& masterIP, int graphId, int numberOfPartitions,
                               std::vector<std::unique_ptr<SharedBuffer>>& bufferPool) {
    operatorLogger.debug("Starting executeDistributed for Apply operator");
    const auto &workerList = JasmineGraphServer::getWorkers(numberOfPartitions);
    operatorLogger.debug("Fetched worker list for numberOfPartitions=" + std::to_string(numberOfPartitions));

    // ---- STEP 1: Execute LEFT child ----
    std::vector<std::unique_ptr<SharedBuffer>> leftBufferPool;
    leftBufferPool.reserve(numberOfPartitions);
    operatorLogger.debug("Reserved leftBufferPool for " + std::to_string(numberOfPartitions) + " partitions");
    for (size_t i = 0; i < numberOfPartitions; ++i) {
        leftBufferPool.emplace_back(std::make_unique<SharedBuffer>(MASTER_BUFFER_SIZE));
        operatorLogger.debug("Created SharedBuffer for leftBufferPool at index " + std::to_string(i));
    }

    std::string leftQueryPlan = operator1->execute();  // Left plan is a query string
    operatorLogger.debug("Generated leftQueryPlan: " + leftQueryPlan);
    std::vector<std::thread> leftThreads;
    int count = 0;
    for (auto worker : workerList) {
        operatorLogger.debug("Spawning thread for CypherQueryExecutor::doCypherQuery for worker " + worker.hostname + ":" + std::to_string(worker.port));
        leftThreads.emplace_back(
            CypherQueryExecutor::doCypherQuery,
            worker.hostname, worker.port,
            masterIP, graphId, count,
            leftQueryPlan, std::ref(*leftBufferPool[count]));
        count++;
    }

    operatorLogger.debug("All left threads joined");

    // ---- STEP 2: Process LEFT results & run RIGHT plan ----
    int closeFlag = 0;
    operatorLogger.debug("Processing leftBufferPool results");
    while (true) {
        if (closeFlag == numberOfPartitions) {
            operatorLogger.debug("All partitions closed, breaking loop");
            for (auto& t : leftThreads) {
                if (t.joinable()) {
                    operatorLogger.debug("Joining left thread");
                    t.join();
                }
            }
            break;
        }


        //
        // if (leftQueryPlan.find("AggregationFunction") != std::string::npos)
        // {
        //     if (Operator::aggregateType == AggregationFactory::AVERAGE) {
        //         Aggregation* aggregation = AggregationFactory::getAggregationMethod(AggregationFactory::AVERAGE);
        //         while (true) {
        //             if (closeFlag == numberOfPartitions) {
        //                 break;
        //             }
        //             for (size_t i = 0; i < bufferPool.size(); ++i) {
        //                 std::string data;
        //                 if (bufferPool[i]->tryGet(data)) {
        //                     if (data == "-1") {
        //                         closeFlag++;
        //                     } else {
        //                         aggregation->insert(data);
        //                     }
        //                 }
        //             }
        //         }
        //         aggregation->getResult(connFd);
        //     }else if (Operator::aggregateType == AggregationFactory::COUNT) {
        //         Aggregation* aggregation = AggregationFactory::getAggregationMethod(AggregationFactory::COUNT);
        //         while (true) {
        //             if (closeFlag == numberOfPartitions) {
        //                 break;
        //             }
        //             for (size_t i = 0; i < bufferPool.size(); ++i) {
        //                 std::string data;
        //                 if (bufferPool[i]->tryGet(data)) {
        //                     if (data == "-1") {
        //                         closeFlag++;
        //                     } else {
        //                         aggregation->insert(data);
        //                     }
        //                 }
        //             }
        //         }
        //         aggregation->getResult(connFd);
        //     }
        //
        // }


        for (size_t i = 0; i < leftBufferPool.size(); ++i) {
            std::string data;
            if (leftBufferPool[i]->tryGet(data)) {
                operatorLogger.debug("Received data from leftBufferPool[" + std::to_string(i) + "]: " + data);
                if (data == "-1") {
                    bufferPool[i]->add(data);
                    closeFlag++;
                    operatorLogger.debug("Received close flag from leftBufferPool[" + std::to_string(i) + "], closeFlag=" + std::to_string(closeFlag));
                } else {
                    // Build right query plan for this left row
                    std::string rightPlan = operator2->execute();
                    operatorLogger.debug("Generated rightPlan before replacement: " + rightPlan);

                    try {
                        auto parsed = json::parse(data);
                        if (!parsed.is_object()) {
                            operatorLogger.error("Parsed data is not a JSON object: " + data);
                            continue;
                        }
                        for (const auto& [key, value] : parsed.items()) {

                            rightPlan = Utils:: replaceAll(rightPlan, key, value);
                            rightPlan = Utils:: replaceAll(rightPlan, "VARIABLE", "STRING");
                            operatorLogger.debug("Replaced {" + key + "} with " + value.dump() + " in rightPlan");
                            operatorLogger.debug("Replaced {" + key + "} with " + value.dump() + " in rightPlan");
                        }
                    } catch (const std::exception& e) {
                        operatorLogger.error("JSON parse error: " + std::string(e.what()) + " for data: " + data);
                        continue;
                    }
                    operatorLogger.debug("Final rightPlan: " + rightPlan);

                    // ---- STEP 2A: If right is Apply, call recursively ----
                    // if (operator2->isApply) {
                    //     operatorLogger.debug("Right operator is Apply, calling recursively");
                    //     Apply* nestedApply = dynamic_cast<Apply*>(operator2);
                    //     if (!nestedApply) {
                    //         operatorLogger.error("Expected Apply operator on right side, but dynamic_cast failed");
                    //         throw std::runtime_error("Expected Apply operator on right side");
                    //     }
                    //     nestedApply->executeDistributed(masterIP, graphId, numberOfPartitions, std::ref(bufferPool));
                    // }
                    // ---- STEP 2B: Otherwise, send as a normal query ----
                    // else {
                        operatorLogger.debug("Right operator is not Apply, sending as normal query");
                        std::vector<std::unique_ptr<SharedBuffer>> rightBufferPool;
                        rightBufferPool.reserve(numberOfPartitions);
                        operatorLogger.debug("Reserved leftBufferPool for " + std::to_string(numberOfPartitions) + " partitions");
                        for (size_t i = 0; i < numberOfPartitions; ++i) {
                            rightBufferPool.emplace_back(std::make_unique<SharedBuffer>(MASTER_BUFFER_SIZE));
                            operatorLogger.debug("Created SharedBuffer for leftBufferPool at index " + std::to_string(i));
                        }

                        std::vector<std::thread> rightThreads;
                        int count = 0;
                        for (auto worker : workerList) {
                            operatorLogger.debug("Spawning thread for CypherQueryExecutor::doCypherQuery for worker " + worker.hostname + ":" + std::to_string(worker.port));
                            rightThreads.emplace_back(
                                CypherQueryExecutor::doCypherQuery,
                                worker.hostname, worker.port,
                                masterIP, graphId, count,
                                rightPlan, std::ref(*rightBufferPool[count]));
                            count++;
                        }

                        // Stream results to client
                        int closeFlag = 0;
                       count = 0;
                        int result_wr;
                        while (true) {
                            if (closeFlag == numberOfPartitions) {
                                for (auto& t : rightThreads) {
                                    if (t.joinable()) {
                                        operatorLogger.debug("Joining right thread");
                                        t.join();
                                    }
                                }
                                break;
                            }
                            for (size_t i = 0; i < bufferPool.size(); ++i) {
                                std::string data;
                                if (rightBufferPool[i]->tryGet(data)) {
                                    if (data == "-1") {
                                        closeFlag++;

                                    } else {
                                        operatorLogger.debug("ProduceResult: Adding result to buffer: " + data);
                                        bufferPool[i]->add(data);
                                    }
                                }
                            }
                        }




                        operatorLogger.debug("All right threads joined for this left row");
                    }
                }
            }
        }
    // }

    operatorLogger.debug("Finished executeDistributed for Apply operator");
}


AggregationFunction::AggregationFunction(Operator *input, ASTNode *ast, std::string functionName):
    input(input), ast(ast), functionName(functionName) {}

std::string AggregationFunction:: extractFunctionName(const std::string& rawInput) {
    using json = nlohmann::json;

    // Step 1: Parse outermost JSON
    json outer = json::parse(rawInput);
    // std::string current = outer["NextOperator"];


    // Step 2: Unescape & parse until we find FunctionName
    while (true) {

        try {
            Utils::unescapeNestedJson(outer);
            if (outer.contains("FunctionName")) {
                return outer["FunctionName"];
            }
            if (outer.contains("NextOperator")) {
                outer = outer["NextOperator"];
            } else {
                break;
            }
        } catch (...) {
            break;
        }
    }

    return "FunctionName not found";
}

string AggregationFunction::execute() {
    json eagerFunction;
    eagerFunction["Operator"] = "AggregationFunction";
    eagerFunction["NextOperator"] = input->execute();
    eagerFunction["FunctionName"] = functionName;
    if (functionName == "count" || functionName == "COUNT")
    {
        eagerFunction["variable"] = ast->value;
        eagerFunction["property"] = "*";
    } else
    {
        eagerFunction["variable"] = ast->elements[0]->value;
        eagerFunction["property"] = ast->elements[1]->elements[0]->value;
    }

    Operator::isAggregate = true;
    if ( functionName == "avg" || functionName == "AVG"){
        Operator::aggregateType = AggregationFactory::AVERAGE;
    } else if (functionName == "count" || functionName == "COUNT") {
        Operator::aggregateType = AggregationFactory::COUNT;
    };
    return eagerFunction.dump();
}
GroupBy::GroupBy(Operator * input, ASTNode* ast, std::vector<std::unordered_map<string ,string >> groupByColumns ,
           std::vector<std::unordered_map<string ,string >> aggregateColumns) : input(input), ast(ast), groupByColumns(groupByColumns), aggregateColumns(aggregateColumns) {};

string GroupBy::execute()
{
    json groupByOperator;
    groupByOperator["Operator"] = "GroupBy";
    if (input != nullptr) {
        groupByOperator["NextOperator"] = input->execute();
    }
    groupByOperator["groupByColumns"] = groupByColumns;
    groupByOperator["aggregateColumns"] = aggregateColumns;
    Operator::isGroupBy = true;
    return groupByOperator.dump();
}
Create::Create(Operator *input, ASTNode *ast) : ast(ast), input(input) {}

string Create::execute() {
    json create;
    if (input != nullptr) {
        create["NextOperator"] = input->execute();
    }
    create["Operator"] = "Create";
    vector<json> list;
    for (auto* e : ast->elements[0]->elements) {
        if (e->nodeType == Const::NODE_PATTERN) {
            json data;
            data["type"] = "Node";
            map<string, string> property;
            for (auto* element : e->elements) {
                if (element->nodeType == Const::NODE_LABEL) {
                    property.insert(pair<string, string>("label", element->elements[0]->value));
                } else if (element->nodeType == Const::VARIABLE) {
                    data["variable"] = element->value;
                } else if (element->nodeType == Const::PROPERTIES_MAP) {
                    for (auto* prop : element->elements) {
                        if (prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                            property.insert(pair<string, string>(prop->elements[0]->value,
                                                                 prop->elements[1]->value));
                        }
                    }
                }
            }
            if (!property.empty()) {
                data["properties"] = property;
            }
            list.push_back(data);
        } else if (e->nodeType == Const::PATTERN_ELEMENTS) {
            json data;
            data["type"] = "Relationships";
            vector<json> relationships;
            json relationship;
            json source;
            json rel;
            json dest;
            for (auto* patternElement : e->elements) {
                if (patternElement->nodeType == Const::NODE_PATTERN) {
                    map<string, string> property;
                    for (auto* element : patternElement->elements) {
                        if (element->nodeType == Const::NODE_LABEL) {
                            property.insert(pair<string, string>("label", element->elements[0]->value));
                        } else if (element->nodeType == Const::VARIABLE) {
                            source["variable"] = element->value;
                        } else if (element->nodeType == Const::PROPERTIES_MAP) {
                            for (auto* prop : element->elements) {
                                if (prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                                    property.insert(pair<string, string>(prop->elements[0]->value,
                                                                         prop->elements[1]->value));
                                }
                            }
                        }
                    }
                    if (!property.empty()) {
                        source["properties"] = property;
                    }
                } else if (patternElement->nodeType == Const::PATTERN_ELEMENT_CHAIN) {
                    map<string, string> property;
                    for (auto* element : patternElement->elements[0]->elements[1]->elements) {
                        if (element->nodeType == Const::RELATIONSHIP_TYPE) {
                            property.insert(pair<string, string>("type", element->elements[0]->value));
                        } else if (element->nodeType == Const::VARIABLE) {
                            rel["variable"] = element->value;
                        } else if (element->nodeType == Const::PROPERTIES_MAP) {
                            for (auto* prop : element->elements) {
                                if (prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                                    property.insert(pair<string, string>(prop->elements[0]->value,
                                                                         prop->elements[1]->value));
                                }
                            }
                        }
                    }
                    if (!property.empty()) {
                        rel["properties"] = property;
                    }
                    property.clear();
                    for (auto* element : patternElement->elements[1]->elements) {
                        if (element->nodeType == Const::NODE_LABEL) {
                            property.insert(pair<string, string>("label", element->elements[0]->value));
                        } else if (element->nodeType == Const::VARIABLE) {
                            dest["variable"] = element->value;
                        } else if (element->nodeType == Const::PROPERTIES_MAP) {
                            for (auto* prop : element->elements) {
                                if (prop->elements[0]->nodeType != Const::RESERVED_WORD) {
                                    property.insert(pair<string, string>(prop->elements[0]->value,
                                                                         prop->elements[1]->value));
                                }
                            }
                        }
                    }
                    if (!property.empty()) {
                        dest["properties"] = property;
                    }
                    if (patternElement->elements[0]->elements[0]->nodeType == Const::RIGHT_ARROW) {
                        relationship["source"] = source;
                        relationship["dest"] = dest;
                        relationship["rel"] = rel;
                    } else {
                        relationship["source"] = dest;
                        relationship["dest"] = source;
                        relationship["rel"] = rel;
                    }
                    relationships.push_back(relationship);
                    source.clear();
                    rel.clear();
                    source = dest;
                    dest.clear();
                }
            }
            data["relationships"] = relationships;
            list.push_back(data);
        }
    }
    create["elements"] = list;
    return  create.dump();
}

CartesianProduct::CartesianProduct(Operator* left, Operator* right) : left(left), right(right) {}

string CartesianProduct::execute() {
    json cartesianProduct;
    cartesianProduct["Operator"] = "CartesianProduct";
    cartesianProduct["left"] = left->execute();
    cartesianProduct["right"] = right->execute();
    return cartesianProduct.dump();
}

