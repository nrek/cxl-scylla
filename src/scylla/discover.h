#pragma once
#include <string>
#include <vector>

namespace scylla {

struct DiscoveredApp {
    std::wstring id;
    std::wstring display_name;
    std::wstring publisher;
    std::wstring kind;  // win32 | msix | cli | manual
    bool available = false;
    std::wstring status;  // installed | not_found | ambiguous | unverified
    std::wstring compatibility;  // unknown | discovered
    std::wstring executable;
    std::wstring package_family_name;
    std::wstring app_id;
    std::wstring aumid;
    std::wstring install_root;
    std::wstring discovery_source;
    std::wstring group;  // ai | other
};

int cmd_discover(int argc, wchar_t** argv);

}  // namespace scylla
