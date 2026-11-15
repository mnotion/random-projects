"""Portable storage and search layer for FileBrowser (stdlib only)."""
from __future__ import annotations

import hashlib
import os
import stat
import tempfile
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterator

TEXT_EXTENSIONS = {
    ".cfg", ".conf", ".csv", ".css", ".html", ".htm", ".ini", ".js",
    ".json", ".log", ".md", ".py", ".rst", ".sql", ".svg", ".toml",
    ".ts", ".tsx", ".txt", ".xml", ".yaml", ".yml",
}


class UnsafePathError(ValueError):
    """A requested path escapes the data folder or traverses a symlink."""


class FileChangedError(RuntimeError):
    """A file changed after it was loaded for editing."""


class FileStore:
    """Recursive search and backup-first atomic writes within a single root."""

    def __init__(self, root: Path | str, *, max_index_bytes: int = 2_000_000) -> None:
        self.root = Path(root).expanduser().resolve()
        self.backup_dir = self.root / "backups"
        self.max_index_bytes = max_index_bytes
        self._write_lock = threading.RLock()

    def ensure_layout(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        self.backup_dir.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def _is_relative_to(path: Path, parent: Path) -> bool:
        try:
            path.relative_to(parent)
            return True
        except ValueError:
            return False

    def resolve_file(self, path: Path | str) -> Path:
        """Resolve a path while rejecting traversal and symlink components."""
        self.ensure_layout()
        raw = Path(path).expanduser()
        candidate = raw if raw.is_absolute() else self.root / raw
        lexical = Path(os.path.abspath(candidate))
        if not self._is_relative_to(lexical, self.root) or lexical == self.root:
            raise UnsafePathError("The selected file must be inside the stuff folder.")

        current = self.root
        for component in lexical.relative_to(self.root).parts:
            current = current / component
            if current.is_symlink():
                raise UnsafePathError("Symbolic links are not supported in the stuff folder.")

        resolved = lexical.resolve(strict=False)
        if not self._is_relative_to(resolved, self.root):
            raise UnsafePathError("The selected file must be inside the stuff folder.")
        if self._is_relative_to(resolved, self.backup_dir.resolve()):
            raise UnsafePathError("The backups folder is reserved for automatic backups.")
        if resolved.exists() and not resolved.is_file():
            raise IsADirectoryError(str(resolved))
        return resolved

    def iter_files(self) -> Iterator[Path]:
        """Yield regular files recursively, excluding backups and symlinks."""
        self.ensure_layout()
        backup_resolved = self.backup_dir.resolve()
        for current_dir, directories, filenames in os.walk(self.root, followlinks=False):
            current = Path(current_dir)
            kept: list[str] = []
            for directory in directories:
                child = current / directory
                if child.is_symlink():
                    continue
                try:
                    if child.resolve() == backup_resolved:
                        continue
                except OSError:
                    continue
                kept.append(directory)
            directories[:] = kept
            for filename in filenames:
                candidate = current / filename
                if candidate.is_symlink():
                    continue
                try:
                    if candidate.is_file():
                        yield candidate
                except OSError:
                    continue

    def search(self, query: str = "", *, search_text: bool = True) -> list[Path]:
        """Search relative paths and small common-text files case-insensitively."""
        needle = query.strip().casefold()
        matches: list[Path] = []
        for path in self.iter_files():
            try:
                relative_name = path.relative_to(self.root).as_posix()
                if not needle or needle in relative_name.casefold():
                    matches.append(path)
                    continue
                if not search_text or path.suffix.casefold() not in TEXT_EXTENSIONS:
                    continue
                if path.stat().st_size > self.max_index_bytes:
                    continue
                if needle in path.read_text(encoding="utf-8", errors="ignore").casefold():
                    matches.append(path)
            except (OSError, UnicodeError, ValueError):
                continue
        return sorted(matches, key=lambda item: item.relative_to(self.root).as_posix().casefold())

    def read_bytes(self, path: Path | str, *, max_bytes: int | None = None) -> bytes:
        target = self.resolve_file(path)
        if max_bytes is not None and target.stat().st_size > max_bytes:
            raise ValueError(f"File is larger than the {max_bytes:,}-byte editor limit.")
        return target.read_bytes()

    @staticmethod
    def sha256(data: bytes) -> str:
        return hashlib.sha256(data).hexdigest()

    @staticmethod
    def _hash_file(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()

    @staticmethod
    def _fsync_directory(directory: Path) -> None:
        try:
            descriptor = os.open(str(directory), os.O_RDONLY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
        except OSError:
            pass

    @staticmethod
    def _write_temp_file(directory: Path, filename: str, data: bytes, mode: int | None) -> Path:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{filename}.", suffix=".tmp", dir=str(directory)
        )
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            if mode is not None:
                os.chmod(temporary, mode)
            return temporary
        except Exception:
            temporary.unlink(missing_ok=True)
            raise

    def _create_backup(self, source: Path, relative_path: Path, expected_hash: str) -> Path:
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        folder = self.backup_dir / relative_path.parent
        folder.mkdir(parents=True, exist_ok=True)
        backup_path = folder / f"{relative_path.name}.{timestamp}.{uuid.uuid4().hex[:8]}.bak"
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{backup_path.name}.", suffix=".tmp", dir=str(folder)
        )
        temporary = Path(temporary_name)
        digest = hashlib.sha256()
        try:
            with source.open("rb") as original, os.fdopen(descriptor, "wb") as backup:
                for block in iter(lambda: original.read(1024 * 1024), b""):
                    backup.write(block)
                    digest.update(block)
                backup.flush()
                os.fsync(backup.fileno())
            if digest.hexdigest() != expected_hash:
                raise FileChangedError("The file changed while its backup was being created.")
            os.chmod(temporary, stat.S_IMODE(source.stat().st_mode))
            os.replace(temporary, backup_path)
            self._fsync_directory(folder)
            return backup_path
        except Exception:
            temporary.unlink(missing_ok=True)
            raise

    def replace_bytes(
        self,
        path: Path | str,
        new_data: bytes,
        *,
        expected_sha256: str | None = None,
    ) -> Path | None:
        """Atomically replace a file after safely writing its previous version.

        Returns the backup path, or None for new/unchanged files. Supplying
        expected_sha256 prevents stale editor contents from overwriting changes.
        """
        if not isinstance(new_data, bytes):
            raise TypeError("new_data must be bytes")
        target = self.resolve_file(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        relative_path = target.relative_to(self.root)

        with self._write_lock:
            existed = target.exists()
            if existed:
                original_hash = self._hash_file(target)
                if expected_sha256 is not None and original_hash != expected_sha256:
                    raise FileChangedError("The file changed on disk. Reload it before saving.")
                if original_hash == self.sha256(new_data):
                    return None
                mode = stat.S_IMODE(target.stat().st_mode)
            else:
                if expected_sha256 is not None:
                    raise FileChangedError("The original file no longer exists. Reload it before saving.")
                original_hash = None
                mode = None

            temporary = self._write_temp_file(target.parent, target.name, new_data, mode)
            try:
                backup_path: Path | None = None
                if existed:
                    backup_path = self._create_backup(target, relative_path, original_hash or "")
                    if self._hash_file(target) != original_hash:
                        raise FileChangedError("The file changed before it could be replaced.")
                elif target.exists():
                    raise FileChangedError("A file with this name appeared before it could be created.")

                os.replace(temporary, target)
                self._fsync_directory(target.parent)
                return backup_path
            except Exception:
                temporary.unlink(missing_ok=True)
                raise

    def replace_text(
        self,
        path: Path | str,
        text: str,
        *,
        expected_sha256: str | None = None,
    ) -> Path | None:
        return self.replace_bytes(path, text.encode("utf-8"), expected_sha256=expected_sha256)
