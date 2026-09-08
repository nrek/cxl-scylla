#pragma once
#include <string>

namespace scylla {

struct HostCapabilities {
    std::wstring product_name;
    std::wstring edition_id;
    std::wstring display_version;
    std::wstring build;
    bool processmodel_present = false;
    bool experimental_export_present = false;
    std::wstring experimental_usable;  // YES / NO
    std::wstring experimental_error;
    std::wstring selected_backend;  // LEGACY_APPCONTAINER
};

HostCapabilities probe_host_capabilities();
void print_host_capabilities(const HostCapabilities& c);

}  // namespace scylla
