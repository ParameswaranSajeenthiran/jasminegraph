#ifndef GROUP_BY_EXECUTOR_H
#define GROUP_BY_EXECUTOR_H

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <sstream>
#include <memory>

#include "Helpers.h"

class GroupByExecutor {
public:
    GroupByExecutor(const std::vector<std::pair<std::string, std::string>>& groupByKeys,
                    const std::vector<AggregationHelper*>& aggregationTemplates);

    // Process a single row
    void insert(const std::string& row);

    // Final aggregated output
    std::vector<std::string> getFinalResults();

private:
    std::vector<std::pair<std::string, std::string>> groupByKeys;
    std::vector<AggregationHelper*> aggregationTemplates;

    // Map from serialized group key to per-group aggregators
    std::map<std::string, std::vector<AggregationHelper*>> groups;

    // Utility: extract group key values from row
    std::vector<std::string> extractKeyValues(const std::string& row);

    // Utility: join vector to string
    std::string join(const std::vector<std::string>& values, const std::string& delim = "|");

    // Optional: parse value from row given variable and property
    std::string extractProperty(const std::string& row, const std::string& variable, const std::string& property);
};

#endif // GROUP_BY_EXECUTOR_H
