#pragma once
#include "scyllagpt/store.h"
#include <filesystem>
#include <string>
#include <vector>

namespace scyllagpt {
class KnowledgeStore;
std::wstring project_key(std::wstring value);
std::vector<std::string> open_project_names(const WorkspaceStore& store);
std::vector<std::wstring> scoped_knowledge_paths(const WorkspaceStore& store, const KnowledgeStore& knowledge);
bool project_document_matches(const std::filesystem::path& relative, const std::string& body,
                              const std::vector<std::string>& projects);
std::string scoped_knowledge_context(const WorkspaceStore& store, const std::vector<std::wstring>& grants);
}
