#include "scyllagpt/connection_policy.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace scyllagpt {
namespace {

std::string lexical_sql(std::string_view sql, bool& multiple) {
    std::string out;
    bool single = false, dbl = false, backtick = false, line_comment = false, block_comment = false;
    int statements = 0;
    bool has_token = false;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        const char c = sql[i];
        const char next = i + 1 < sql.size() ? sql[i + 1] : '\0';
        if (line_comment) { if (c == '\n') line_comment = false; continue; }
        if (block_comment) { if (c == '*' && next == '/') { block_comment = false; ++i; } continue; }
        if (!single && !dbl && !backtick && c == '-' && next == '-') { line_comment = true; ++i; continue; }
        if (!single && !dbl && !backtick && c == '#') { line_comment = true; continue; }
        if (!single && !dbl && !backtick && c == '/' && next == '*') { block_comment = true; ++i; continue; }
        if (!dbl && !backtick && c == '\'' && (i == 0 || sql[i - 1] != '\\')) { single = !single; out += ' '; continue; }
        if (!single && !backtick && c == '"' && (i == 0 || sql[i - 1] != '\\')) { dbl = !dbl; out += ' '; continue; }
        if (!single && !dbl && c == '`') { backtick = !backtick; out += ' '; continue; }
        if (single || dbl || backtick) { out += ' '; continue; }
        if (c == ';') { if (has_token) { ++statements; has_token = false; } out += ' '; continue; }
        if (!std::isspace(static_cast<unsigned char>(c))) has_token = true;
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (has_token) ++statements;
    multiple = statements > 1;
    return out;
}

std::string first_word(std::string_view sql) {
    const auto start = sql.find_first_not_of(" \t\r\n(");
    if (start == std::string_view::npos) return {};
    auto end = start;
    while (end < sql.size() && (std::isalpha(static_cast<unsigned char>(sql[end])) || sql[end] == '_')) ++end;
    return std::string(sql.substr(start, end - start));
}

std::string statement_word_after_ctes(std::string_view sql) {
    int depth = 0;
    for (std::size_t i = 0; i < sql.size();) {
        const unsigned char c = static_cast<unsigned char>(sql[i]);
        if (sql[i] == '(') { ++depth; ++i; continue; }
        if (sql[i] == ')') { if (depth > 0) --depth; ++i; continue; }
        if (!std::isalpha(c) && sql[i] != '_') { ++i; continue; }
        const std::size_t start = i++;
        while (i < sql.size() &&
               (std::isalpha(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) ++i;
        if (depth != 0) continue;
        const std::string word(sql.substr(start, i - start));
        if (word == "SELECT" || word == "INSERT" || word == "UPDATE" ||
            word == "DELETE" || word == "REPLACE") return word;
    }
    return {};
}

ConnectionAuthority authority_for(SqlClass classification, const ConnectionQueryPolicy& policy) {
    switch (classification) {
        case SqlClass::Read: return policy.read;
        case SqlClass::DataModification: return policy.data_modification;
        case SqlClass::SchemaModification: return policy.schema_modification;
        case SqlClass::Administrative: return policy.administrative;
        default: return ConnectionAuthority::Block;
    }
}
}  // namespace

SqlClass classify_sql(std::string_view sql, bool* multiple_statements) {
    bool multiple = false;
    const std::string normalized = lexical_sql(sql, multiple);
    if (multiple_statements) *multiple_statements = multiple;
    std::string keyword = first_word(normalized);
    if (keyword == "WITH") {
        keyword = statement_word_after_ctes(normalized);
    }
    static const std::unordered_set<std::string> read = {"SELECT", "SHOW", "DESCRIBE", "DESC", "EXPLAIN"};
    static const std::unordered_set<std::string> data = {"INSERT", "UPDATE", "DELETE", "REPLACE", "LOAD", "CALL"};
    static const std::unordered_set<std::string> schema = {"CREATE", "ALTER", "DROP", "TRUNCATE", "RENAME"};
    static const std::unordered_set<std::string> admin = {"GRANT", "REVOKE", "KILL", "SET", "FLUSH", "RESET"};
    if (read.contains(keyword)) return SqlClass::Read;
    if (data.contains(keyword)) return SqlClass::DataModification;
    if (schema.contains(keyword)) return SqlClass::SchemaModification;
    if (admin.contains(keyword)) return SqlClass::Administrative;
    return SqlClass::Unknown;
}

SqlAssessment assess_sql(std::string_view sql, const ConnectionQueryPolicy& policy) {
    SqlAssessment result;
    result.classification = classify_sql(sql, &result.multiple_statements);
    if (result.multiple_statements && !policy.allow_multiple_statements) {
        result.reason = "multiple SQL statements are disabled";
        return result;
    }
    if (policy.unrestricted) {
        result.authority = ConnectionAuthority::Auto;
        result.allowed = true;
        return result;
    }
    result.authority = authority_for(result.classification, policy);
    result.allowed = result.authority != ConnectionAuthority::Block;
    result.approval_required = result.authority == ConnectionAuthority::Ask;
    if (result.classification == SqlClass::Unknown) result.reason = "SQL classification is unknown";
    else if (!result.allowed) result.reason = "query class is blocked by connection policy";
    return result;
}

void enforce_result_policy(ConnectionResult& result, const ConnectionResultPolicy& policy) {
    if (result.row_count > policy.max_rows) {
        result.truncated = true;
        result.warning = "Result exceeded the configured row limit.";
    }
    if (result.rows_json.size() > policy.max_bytes) {
        result.rows_json.resize(policy.max_bytes);
        result.truncated = true;
        result.warning = "Result exceeded the configured byte limit.";
    }
    if (policy.visibility == ResultVisibility::HumanOnly || policy.visibility == ResultVisibility::MetadataOnly) {
        result.rows_json.clear();
    }
}

}  // namespace scyllagpt
