#include "scyllagpt/connection_policy.h"

#include <iostream>

namespace {
int failures = 0;
void expect(bool value, const char* name) {
    if (value) std::cout << "ok   " << name << "\n";
    else { std::cerr << "FAIL " << name << "\n"; ++failures; }
}
}

int run_connection_policy_tests() {
    using namespace scyllagpt;
    failures = 0;
    bool multiple = false;
    expect(classify_sql("-- comment\nSELECT * FROM users", &multiple) == SqlClass::Read && !multiple,
           "classify read query");
    expect(classify_sql("WITH rows AS (SELECT 1) UPDATE users SET active=1", nullptr) ==
               SqlClass::DataModification,
           "classify modifying CTE");
    expect(classify_sql("SELECT ';' AS value; SELECT 2", &multiple) == SqlClass::Read && multiple,
           "detect multiple statements outside literals");
    expect(classify_sql("DROP TABLE users", nullptr) == SqlClass::SchemaModification,
           "classify schema query");

    ConnectionQueryPolicy policy;
    auto read = assess_sql("SELECT 1", policy);
    expect(read.allowed && !read.approval_required, "auto allow read query");
    auto write = assess_sql("UPDATE users SET active=1", policy);
    expect(write.allowed && write.approval_required, "ask for data modification");
    auto batch = assess_sql("SELECT 1; SELECT 2", policy);
    expect(!batch.allowed && !batch.reason.empty(), "block statement batch");

    ConnectionResult result;
    result.ok = true;
    result.row_count = 10;
    result.rows_json = "123456789";
    ConnectionResultPolicy result_policy;
    result_policy.max_rows = 5;
    result_policy.max_bytes = 4;
    enforce_result_policy(result, result_policy);
    expect(result.truncated && result.rows_json.size() == 4, "enforce result limits");
    result_policy.visibility = ResultVisibility::MetadataOnly;
    enforce_result_policy(result, result_policy);
    expect(result.rows_json.empty(), "hide rows for metadata-only results");
    return failures;
}
