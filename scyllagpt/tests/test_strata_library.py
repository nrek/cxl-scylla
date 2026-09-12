"""Run with Python + STRATA installed; uses temporary SQLite and mocked network."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch, MagicMock

spec = importlib.util.spec_from_file_location("library", Path(__file__).parents[1] / "runtime/strata_bridge_adapter.py")
library = importlib.util.module_from_spec(spec)
spec.loader.exec_module(library)


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.cwd = Path.cwd()
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.user = self.root / "user"
        self.user.mkdir()
        self.patches = [patch.object(library.local_store, "USER_STRATA_DIR", self.user),
                        patch.object(library.local_store, "USER_GLOBAL_FILE", self.user / "global.json")]
        for p in self.patches:
            p.start()
        library.configure_workspace({"workspace_root": str(self.root)})

    def tearDown(self):
        os.chdir(self.cwd)
        for p in self.patches:
            p.stop()
        self.temp.cleanup()

    def call(self, operation, **params):
        return library.handle_library({"workspace_root": str(self.root), "operation": operation, **params})

    def fixture(self):
        with library.db.connect() as conn:
            library.db.init_db(conn)
            for project in ("alpha", "beta"):
                row = dict.fromkeys(["plan_status", "linear_task_id", "files_changed", "deploy_commands",
                                     "tags", "folder_status", "status_mismatch"])
                row.update(id=project, kind="blueprint", project=project, path=f".md/{project}.md",
                           title=f"{project} design", body=f"# {project}\nUnicode: café\nsearchneedle",
                           body_hash=project, created_at="2026-09-01T00:00:00Z",
                           updated_at="2026-09-01T00:00:00Z", storage="db_only", status_mismatch=0)
                library.db.upsert_document(conn, row)

    def test_missing_index_does_not_create_database(self):
        self.assertEqual(self.call("projects")["projects"], [])
        self.assertFalse(Path(library.paths.DB_PATH).exists())

    def test_local_projects_search_and_archived_body_without_network(self):
        self.fixture()
        with patch("cxl_strata.api_client._client", side_effect=AssertionError("network")):
            self.assertEqual({p["project"] for p in self.call("projects")["projects"]}, {"alpha", "beta"})
            self.assertEqual(len(self.call("search", project="alpha")["hits"]), 1)
            hits = self.call("search", query="searchneedle", project="beta")["hits"]
            self.assertEqual([h["project"] for h in hits], ["beta"])
            doc = self.call("get", path=".md/beta.md")["document"]
            self.assertIn("café", doc["body"])
            self.assertEqual(doc["storage"], "db_only")

    def test_remote_config_roundtrip_and_blank_local_only(self):
        library.local_store.update_global_config({"actor_name": "Example"})
        self.call("configure", remote_url="https://memory.example.test/")
        self.assertEqual(self.call("config")["config"]["remote_url"], "https://memory.example.test")
        self.assertEqual(library.local_store.load_global_config()["actor_name"], "Example")
        self.call("configure", remote_url="")
        with self.assertRaisesRegex(ValueError, "No remote"):
            self.call("projects", source="remote")

    def test_save_edits_effective_workspace_config_and_preserves_other_fields(self):
        (self.root / ".strata").mkdir()
        config = self.root / ".strata/config.json"
        config.write_text(json.dumps({"api_base_url": "https://workspace.example.test", "actor_name": "Example"}))
        self.assertEqual(self.call("config")["config"]["config_scope"], "workspace")
        self.call("configure", remote_url="https://other.example.test")
        self.assertEqual(self.call("config")["config"]["remote_url"], "https://other.example.test")
        self.assertEqual(json.loads(config.read_text())["actor_name"], "Example")
        self.assertFalse((self.user / "global.json").exists())

    def test_invalid_remote_does_not_break_local_projects(self):
        self.fixture()
        library.local_store.update_global_config({"api_base_url": "https://user:secret@example.test"})
        result = self.call("projects")
        self.assertEqual(len(result["projects"]), 2)
        self.assertFalse(result["config"]["remote_configured"])

    def test_remote_search_and_document_use_document_endpoints(self):
        self.call("configure", remote_url="https://memory.example.test")
        with patch("cxl_strata.api_client.search_documents", return_value={"results": [
                {"id": "doc-1", "project_slug": "alpha", "title": "Remote"}]}) as search:
            result = self.call("search", source="remote", project="alpha", query="design")
            self.assertEqual(result["hits"][0]["path"], "doc-1")
            search.assert_called_once_with("design", project="alpha", limit=200)
        client = MagicMock()
        client.get.return_value.json.return_value = {"body": "# Remote body"}
        with patch("cxl_strata.api_client._client") as factory:
            factory.return_value.__enter__.return_value = client
            self.assertEqual(self.call("get", source="remote", path="doc-1")["document"]["body"], "# Remote body")
            client.get.assert_called_once_with("/v1/documents/doc-1")

    def test_transfer_requires_project_and_reuses_strata(self):
        self.call("configure", remote_url="https://memory.example.test")
        with self.assertRaisesRegex(ValueError, "Open a project"):
            self.call("publish")
        with patch("cxl_strata.documents.stash_filtered", return_value={"pushed": 2}) as publish:
            self.assertEqual(self.call("publish", project="alpha")["transfer"], {"pushed": 2})
            publish.assert_called_once_with(project="alpha")
        with patch("cxl_strata.pull.pull_documents", return_value={"pulled": 3}) as pull:
            self.assertEqual(self.call("pull", project="alpha")["transfer"], {"pulled": 3})
            pull.assert_called_once_with(project="alpha")

    def test_reject_credential_urls(self):
        for url in ("https://user:secret@example.test", "https://example.test/?token=secret", "http://example.test"):
            with self.assertRaises(ValueError):
                self.call("configure", remote_url=url)

    @unittest.skipUnless(os.environ.get("SCYLLA_STRATA_UI_TEST_EXE"), "native UI test executable not supplied")
    def test_native_ui_navigation(self):
        self.fixture()
        result = subprocess.run([os.environ["SCYLLA_STRATA_UI_TEST_EXE"], str(self.root)],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
