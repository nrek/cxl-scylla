# STRATA library

Open **Settings → STRATA** to review indexed plans, blueprints, handoffs, and other documents. Install the STRATA CLI and its Python package first. Workbench ships `strata_workbench_bridge.py` beside its executable; keep that file when copying a build.

The Workspace field names the folder containing `.md/workspace_index.sqlite`. Knowledge sources help Workbench discover that folder. Opening Settings lists the local index's projects without applying the active editor project as an implicit filter. A missing index is reported without creating an empty database.

Select a project and choose **Open** (or double-click), then search within it. **Projects** returns to the project list. A query from that list searches all indexed projects. Search works on indexed text, including archived `db_only` documents, rather than only files still on disk.

Open a document to replace the result list with a read-only, scrollable markdown source view. **Close Document** returns to the same results, selection, scroll position, and query. Local document bodies are capped by STRATA at 200,000 characters; the view reports truncation.

## Optional remote API

Workbench reads STRATA's effective configuration: workspace `.strata/config.json` when present, otherwise the user's `~/.strata/global.json`. **Save Remote** changes `api_base_url` in that effective file while preserving its other settings. The panel identifies the config scope. A blank URL means local-only operation.

Use HTTPS for a remote service; HTTP is accepted for localhost development. Credentials stay in STRATA's normal credential files or `STRATA_API_KEY`; they are not copied into Scylla settings or returned through the bridge. Configure authentication using the STRATA CLI before selecting **Remote**.

**Local** reads SQLite through STRATA's own query APIs and makes no remote request. **Remote** browses and searches the configured API's document library. Authentication or connection failures preserve existing results and leave Local available. Requests run on a worker so the rest of Workbench remains responsive.

With a library project open:

- **Publish** uploads that project's pending indexed documents using STRATA's existing stash rules.
- **Pull** imports that project's remote documents using STRATA's existing conflict and storage rules.
- **Bind Project** saves the association between the active Scylla project and the browsed STRATA project. Browsing alone does not change the binding.

Transfers are explicit, confirmed actions; opening Settings does not sync. Counts report completion and failures. Refresh after a transfer to reload the library. A transfer timeout can leave partial progress; inspect STRATA before retrying. This panel does not automatically inject documents into agent conversations or implement the deferred Context Inspector.

## Verification

With STRATA installed in the Python environment, run from the repository root:

```powershell
cmake --build scyllagpt/build --config Release --target scyllagpt scyllagpt-strata-tests
$env:SCYLLA_STRATA_UI_TEST_EXE = "$PWD\scyllagpt\build\Release\scyllagpt-strata-tests.exe"
python scyllagpt/tests/test_strata_library.py
```

The Python suite uses temporary SQLite/config fixtures and mocks remote requests. When the native test path is supplied, it also tests the real bridge, project drill-down, document opening, and restoration of the result list using a hidden Win32 window. It does not publish or pull production data.
