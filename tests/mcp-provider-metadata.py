"""Opt-in, read-only live contract check. Never opens a browser or uses credentials.

Usage: python tests/mcp-provider-metadata.py build/Release/scylla-core.dll
"""
import ctypes
import json
import sys
from pathlib import Path


def main():
    core = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
    catalog = core.scylla_core_mcp_catalog_json
    catalog.argtypes = [ctypes.c_char_p, ctypes.c_int]
    scopes = core.scylla_core_mcp_scopes_json
    scopes.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    buffer = ctypes.create_string_buffer(128 * 1024)
    assert catalog(buffer, len(buffer)) >= 0, "Catalog ABI failed"
    services = json.loads(buffer.value)
    assert len(services) == 7
    for service in services:
        assert scopes(service["endpoint"].encode(), buffer, len(buffer)) >= 0
        result = json.loads(buffer.value)
        assert set(result["scopes"]) == set(service["scopes"]), (
            service["id"], result["message"], result["scopes"], service["scopes"]
        )
        print(f"PASS {service['id']}: {len(result['scopes'])} advertised scopes", flush=True)


if __name__ == "__main__":
    main()
