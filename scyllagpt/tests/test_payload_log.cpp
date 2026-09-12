#include "scyllagpt/session.h"
#include <iostream>
static int failures = 0;
static void expect(bool ok, const char* name) { std::cout << (ok ? "ok " : "FAIL ") << name << "\n"; if (!ok) ++failures; }
int main() {
    {
        scyllagpt::Session session;
        session.handle_line(R"({"method":"item/started","params":{"item":{"type":"mcpToolCall","name":"scylla_query"}}})");
        expect(session.payload_log.back().find("scylla_query") != std::string::npos, "runtime tool events reach payload log");
        session.log_payload("IN", R"({"method":"tool/call","params":{"api_key":"secret-value","arguments":"select 1"}})");
        expect(session.payload_log.back().find("secret-value") == std::string::npos, "payload redacts nested credentials");
        expect(session.payload_log.back().find("select 1") != std::string::npos, "payload retains tool arguments");
        session.log_payload("OUT", std::string(3000, 'x'));
        expect(session.payload_log.back().find("[truncated]") != std::string::npos, "payload truncates long entries");
        session.log_payload("IN", "first\nsecond");
        expect(session.payload_log.back().find('\n') == std::string::npos, "payload escapes newlines");
        for (int i = 0; i < 250; ++i) session.log_payload("IN", std::to_string(i));
        expect(session.payload_log.size() == 200, "payload bounds retained entries");
        expect(session.payload_log.front().find(" 50") != std::string::npos, "payload evicts oldest entries");
    }
return failures ? 1 : 0;
}
