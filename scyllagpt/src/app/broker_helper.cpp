#include "scyllagpt/broker_mcp.h"
#include "scyllagpt/ssh_query_executor.h"

int main() {
    if (scyllagpt::ssh_askpass_mode_requested()) return scyllagpt::run_ssh_askpass_helper();
    return scyllagpt::run_broker_mcp_helper();
}
