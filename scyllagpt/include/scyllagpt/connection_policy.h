#pragma once

#include "scyllagpt/project_connection.h"

#include <string>
#include <string_view>

namespace scyllagpt {

enum class SqlClass { Read = 0, DataModification, SchemaModification, Administrative, Unknown };

struct SqlAssessment {
    SqlClass classification = SqlClass::Unknown;
    ConnectionAuthority authority = ConnectionAuthority::Block;
    bool multiple_statements = false;
    bool allowed = false;
    bool approval_required = false;
    std::string reason;
};

struct ConnectionResult {
    bool ok = false;
    std::string columns_json;
    std::string rows_json;
    std::uint64_t row_count = 0;
    std::uint64_t elapsed_ms = 0;
    bool truncated = false;
    std::string warning;
};

SqlClass classify_sql(std::string_view sql, bool* multiple_statements = nullptr);
SqlAssessment assess_sql(std::string_view sql, const ConnectionQueryPolicy& policy);
void enforce_result_policy(ConnectionResult& result, const ConnectionResultPolicy& policy);

}  // namespace scyllagpt
