"""Workbench extension to the installed STRATA bridge (no private SQLite schema).

Uses STRATA's query, configuration and transfer APIs. Credentials never cross stdio.
Local browsing never contacts the remote. Mutations require an explicit UI action.
"""
from __future__ import annotations

import os
import json
import tempfile
from pathlib import Path
from urllib.parse import urlsplit, quote

from cxl_strata import bridge, local_store
from cxl_strata.workspace_index import db, nl_query, paths


def configure_workspace(params):
    root = Path(params.get("workspace_root") or paths.WORKSPACE_ROOT).resolve()
    if root.name.lower() == ".md":
        root = root.parent
    if not root.is_dir():
        raise ValueError("Workspace folder does not exist")
    paths.set_workspace_root(root)
    os.chdir(root)  # STRATA config/queue paths are relative to the workspace.
    return root


def config_info():
    try:
        cfg = local_store.load_config()
    except FileNotFoundError:
        cfg = {}
    except Exception:
        return {"remote_url": "", "remote_configured": False, "config_scope": "invalid",
                "message": "STRATA configuration could not be read; local library remains available"}
    url = str(cfg.get("api_base_url") or "").strip().rstrip("/")
    # Do not reflect embedded credentials or URL query tokens into UI/diagnostics.
    if url:
        try:
            validate_url(url)
        except ValueError:
            return {"remote_url": "", "remote_configured": False, "config_scope": "invalid",
                    "message": "Remote URL is invalid; local library remains available"}
    workspace_config = Path(local_store.CONFIG_FILE).is_file()
    return {"remote_url": url, "remote_configured": bool(url),
            "config_scope": "workspace" if workspace_config else "user",
            "workspace_root": str(paths.WORKSPACE_ROOT)}


def validate_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme not in ("https", "http") or not parsed.hostname or
            parsed.username or parsed.password or parsed.query or parsed.fragment):
        raise ValueError("Remote must be an HTTP(S) URL without credentials, query or fragment")
    if parsed.scheme == "http" and parsed.hostname not in ("localhost", "127.0.0.1", "::1"):
        raise ValueError("Use HTTPS for a remote API; HTTP is only supported for localhost")


def require_remote():
    if not config_info()["remote_configured"]:
        raise ValueError("No remote configured; local knowledge is still available")


def remote_rows(project=None):
    from cxl_strata import api_client
    rows, offset = [], 0
    while True:
        batch = api_client.list_documents(project=project or None, limit=200,
                                          offset=offset, include_body=False)
        rows.extend(batch)
        if len(batch) < 200:
            return rows
        offset += len(batch)


def remote_hit(row):
    project = row.get("project") or row.get("project_slug") or ""
    if isinstance(project, dict):
        project = project.get("slug", "")
    return {**row, "path": str(row.get("id") or ""), "project": project,
            "origin": "shared", "sync_status": "shared"}


def handle_library(params):
    params = params or {}
    configure_workspace(params)
    op = params.get("operation", "projects")
    source = params.get("source", "local")
    project = str(params.get("project") or "")
    query = str(params.get("query") or "").strip()
    if op == "configure":
        url = str(params.get("remote_url") or "").strip().rstrip("/")
        if url:
            validate_url(url)
        # Edit the effective config, preserving its other values. Never write secrets.
        target = (Path(local_store.CONFIG_FILE) if Path(local_store.CONFIG_FILE).is_file()
                  else Path(local_store.USER_GLOBAL_FILE))
        cfg = json.loads(target.read_text(encoding="utf-8-sig")) if target.is_file() else {}
        cfg["api_base_url"] = url
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=".remote-", dir=target.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump(cfg, stream, indent=2, ensure_ascii=False)
                stream.write("\n")
            os.replace(temporary, target)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        return {"config": config_info(), "message": "Remote saved in STRATA " + config_info()["config_scope"] + " config"}
    if op == "config":
        return {"config": config_info()}
    if source not in ("local", "remote"):
        raise ValueError("Unknown knowledge source")
    if source == "remote" or op in ("publish", "pull"):
        require_remote()
    if op in ("publish", "pull"):
        if not project:
            raise ValueError("Open a project before transferring its documents")
        if op == "publish":
            from cxl_strata.documents import stash_filtered
            result = stash_filtered(project=project)
        else:
            from cxl_strata.pull import pull_documents
            result = pull_documents(project=project)
        counts = {key: value for key, value in result.items() if isinstance(value, (int, bool))}
        for key in ("errors", "failed", "blocked"):
            if isinstance(result.get(key), list):
                counts[key] = len(result[key])
        return {"transfer": counts, "message": "Transfer finished; review the result counts"}
    if source == "remote":
        from cxl_strata import api_client
        if op == "get":
            document_id = str(params.get("path") or "")
            if not document_id:
                raise ValueError("Select a document")
            with api_client._client(timeout=20) as client:
                response = client.get("/v1/documents/" + quote(document_id, safe=""))
                response.raise_for_status()
                return {"document": response.json()}
        rows = (api_client.search_documents(query, project=project or None, limit=200).get("results", [])
                if query else remote_rows(project))
        hits = [remote_hit(r) for r in rows]
        if op == "projects":
            counts = {}
            for hit in hits:
                name = hit["project"]
                if name:
                    counts[name] = counts.get(name, 0) + 1
            return {"projects": [{"project": p, "total": n} for p, n in sorted(counts.items())]}
        return {"hits": hits, "truncated": bool(query and len(hits) == 200)}
    # Do not create a new empty database just because the user typed a wrong root.
    if not Path(paths.DB_PATH).is_file() or Path(paths.DB_PATH).stat().st_size == 0:
        return {"projects": [], "hits": [], "config": config_info(),
                "message": "No local index here. Select an indexed workspace or run strata index there."}
    if op == "projects":
        with db.connect() as conn:
            projects = nl_query.list_projects(conn)
        return {"projects": projects, "config": config_info()}
    if op == "get":
        return bridge.handle_get(params)
    if op == "recent":
        return bridge.handle_recent(params)
    if query:
        return bridge.handle_search({**params, "query": query, "limit": 200})
    if not project:
        return handle_library({**params, "operation": "projects"})
    with db.connect() as conn:
        library = nl_query.project_library(conn, project=project, limit=10000)
    return {"hits": library["events"], "truncated": library.get("truncated", False)}


def safe_library(params):
    try:
        result = handle_library(params)
        result.setdefault("config", config_info())
        return result
    except (ValueError, FileNotFoundError) as exc:
        # These errors are ours or missing local config. No auth values included.
        raise ValueError(str(exc)) from None
    except Exception as exc:
        # HTTP exceptions may contain request URLs; never echo headers or bodies.
        status = getattr(getattr(exc, "response", None), "status_code", None)
        raise RuntimeError(f"STRATA operation failed ({type(exc).__name__}"
                           + (f", HTTP {status}" if status else "")
                           + "). Check configuration and credentials; local browsing remains available.") from None


if __name__ == "__main__":
    bridge.METHODS = (*bridge.METHODS, "library")
    bridge.HANDLERS["library"] = safe_library
    bridge.run_bridge()
