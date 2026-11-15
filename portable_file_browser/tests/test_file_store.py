from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from file_store import FileChangedError, FileStore, UnsafePathError


class FileStoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name) / "stuff"
        self.store = FileStore(self.root)
        self.store.ensure_layout()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_search_finds_files_by_path_and_text(self) -> None:
        nested = self.root / "documents" / "notes.txt"
        nested.parent.mkdir()
        nested.write_text("The secret phrase is blue planet", encoding="utf-8")
        (self.root / "picture.png").write_bytes(b"not really a png")
        self.assertEqual(self.store.search("notes"), [nested])
        self.assertEqual(self.store.search("blue planet"), [nested])
        self.assertEqual(len(self.store.search("")), 2)

    def test_replacement_creates_backup_before_replacing(self) -> None:
        target = self.root / "notes.txt"
        target.write_text("old version", encoding="utf-8")
        backup = self.store.replace_text(target, "new version")
        self.assertIsNotNone(backup)
        self.assertEqual(target.read_text(encoding="utf-8"), "new version")
        assert backup is not None
        self.assertTrue(backup.is_file())
        self.assertEqual(backup.read_text(encoding="utf-8"), "old version")
        self.assertEqual(backup.parent, self.store.backup_dir)
        self.assertEqual(self.store.search("old version"), [])

    def test_unchanged_content_does_not_create_backup(self) -> None:
        target = self.root / "same.txt"
        target.write_text("same", encoding="utf-8")
        self.assertIsNone(self.store.replace_text(target, "same"))
        self.assertEqual(list(self.store.backup_dir.rglob("*.bak")), [])

    def test_new_file_is_created_without_backup(self) -> None:
        target = self.root / "nested" / "new.txt"
        self.assertIsNone(self.store.replace_text(target, "first version"))
        self.assertEqual(target.read_text(encoding="utf-8"), "first version")

    def test_stale_editor_write_is_rejected(self) -> None:
        target = self.root / "notes.txt"
        target.write_text("version 1", encoding="utf-8")
        loaded_hash = self.store.sha256(target.read_bytes())
        target.write_text("version 2 from another app", encoding="utf-8")
        with self.assertRaises(FileChangedError):
            self.store.replace_text(target, "stale edit", expected_sha256=loaded_hash)
        self.assertEqual(target.read_text(encoding="utf-8"), "version 2 from another app")

    def test_path_traversal_is_rejected(self) -> None:
        with self.assertRaises(UnsafePathError):
            self.store.resolve_file(self.root.parent / "outside.txt")
        with self.assertRaises(UnsafePathError):
            self.store.resolve_file("../outside.txt")

    def test_backup_directory_is_not_searchable(self) -> None:
        target = self.root / "notes.txt"
        target.write_text("public text", encoding="utf-8")
        self.store.replace_text(target, "updated text")
        self.assertEqual(self.store.search("public text"), [])


if __name__ == "__main__":
    unittest.main()
