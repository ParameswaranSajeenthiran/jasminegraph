#include "GroupByExecutor.h"
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <stdexcept>

#include "Helpers.h"

GroupByExecutor::GroupByExecutor(
    const std::vector<std::pair<std::string, std::string>>& groupByKeys,
    const std::vector<AggregationHelper*>& aggregationTemplates)
    : groupByKeys(groupByKeys), aggregationTemplates(aggregationTemplates) {}

// Insert a row into the appropriate group
void GroupByExecutor::insert(const std::string& row) {
    std::vector<std::string> keyValues = extractKeyValues(row);
    std::string groupKey = join(keyValues);

    // Initialize group if new
    if (groups.find(groupKey) == groups.end()) {
        std::vector<AggregationHelper*> clones;
        for (AggregationHelper* agg : aggregationTemplates) {
            // clones.push_back(agg->clone());
        }
        groups[groupKey] = clones;
    }

    // Insert row into each aggregator
    for (AggregationHelper* agg : groups[groupKey]) {
        agg->insertData(row);
    }
}

// Retrieve final aggregated results
std::vector<std::string> GroupByExecutor::getFinalResults() {
    std::vector<std::string> results;

    for (const auto& pair : groups) {
        const std::string& groupKey = pair.first;
        const std::vector<AggregationHelper*>& aggs = pair.second;

        std::ostringstream oss;
        oss << groupKey;

        for (AggregationHelper* agg : aggs) {
            // oss << "|" << agg->getResult();
        }

        results.push_back(oss.str());
    }

    return results;
}

// Extract group key values from row
std::vector<std::string> GroupByExecutor::extractKeyValues(const std::string& row) {
    std::vector<std::string> keyValues;

    for (const auto& [variable, property] : groupByKeys) {
        keyValues.push_back(extractProperty(row, variable, property));
    }

    return keyValues;
}

// Join a vector of strings with delimiter
std::string GroupByExecutor::join(const std::vector<std::string>& values, const std::string& delim) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << delim;
        oss << values[i];
    }
    return oss.str();
}

// Extract a specific property from row string
std::string GroupByExecutor::extractProperty(const std::string& row, const std::string& variable, const std::string& property) {
    // Assumes format like "c.country=USA|p.name=Alice"
    std::string key = variable + "." + property;
    std::istringstream iss(row);
    std::string token;

    while (std::getline(iss, token, '|')) {
        size_t pos = token.find('=');
        if (pos != std::string::npos) {
            std::string k = token.substr(0, pos);
            std::string v = token.substr(pos + 1);
            if (k == key) {
                return v;
            }
        }
    }

    return "";  // Or throw exception if you want to enforce strict parsing
}
