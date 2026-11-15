"""Portable file browser; runtime dependencies are Python's standard library."""
from __future__ import annotations

import os
import subprocess
import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

from file_store import FileChangedError, FileStore, TEXT_EXTENSIONS

EDITOR_LIMIT_BYTES = 8 * 1024 * 1024


def application_directory() -> Path:
    """Use the EXE's parent when frozen, not PyInstaller's temporary directory."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def open_with_system(path: Path) -> None:
    """Open a file or folder using the operating system's default handler."""
    path = path.resolve(strict=True)
    if sys.platform == "win32":
        os.startfile(str(path))  # type: ignore[attr-defined]
    elif sys.platform == "darwin":
        subprocess.Popen(["open", str(path)], close_fds=True)
    else:
        subprocess.Popen(["xdg-open", str(path)], close_fds=True)


class FileBrowserApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.store = FileStore(application_directory() / "stuff")
        self.store.ensure_layout()
        self._visible_paths: dict[str, Path] = {}

        root.title("Portable File Browser")
        root.geometry("920x560")
        root.minsize(620, 360)

        outer = ttk.Frame(root, padding=10)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text=f"Search folder: {self.store.root}").pack(anchor="w", pady=(0, 8))

        search_row = ttk.Frame(outer)
        search_row.pack(fill="x", pady=(0, 8))
        self.query = tk.StringVar()
        self.search_entry = ttk.Entry(search_row, textvariable=self.query)
        self.search_entry.pack(side="left", fill="x", expand=True)
        self.search_entry.bind("<Return>", lambda _event: self.refresh())
        ttk.Button(search_row, text="Search", command=self.refresh).pack(side="left", padx=(6, 0))
        ttk.Button(search_row, text="Refresh", command=self.refresh).pack(side="left", padx=(6, 0))

        columns = ("name", "path", "size")
        self.results = ttk.Treeview(outer, columns=columns, show="headings", selectmode="browse")
        self.results.heading("name", text="File")
        self.results.heading("path", text="Relative path")
        self.results.heading("size", text="Size")
        self.results.tag_configure("word", foreground="blue")
        self.results.tag_configure("pdf", foreground="red")
        self.results.column("name", width=220, anchor="w")
        self.results.column("path", width=550, anchor="w")
        self.results.column("size", width=90, anchor="e")
        self.results.pack(fill="both", expand=True)
        self.results.bind("<Double-1>", lambda _event: self.open_selected())

        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=(8, 0))
        ttk.Button(actions, text="Open", command=self.open_selected).pack(side="left")
        ttk.Button(actions, text="Open with Word", command=self.open_with_word).pack(side="left", padx=(6, 0))
        ttk.Button(actions, text="Edit text file", command=self.edit_selected).pack(side="left", padx=(6, 0))
        ttk.Button(actions, text="Open stuff folder", command=self.open_data_folder).pack(side="left", padx=(6, 0))

        self.status = tk.StringVar(value="Ready")
        ttk.Label(outer, textvariable=self.status).pack(anchor="w", pady=(8, 0))
        self.refresh()
        self.search_entry.focus_set()

    @staticmethod
    def _format_size(size: int) -> str:
        value = float(size)
        for unit in ("B", "KB", "MB", "GB"):
            if value < 1024 or unit == "GB":
                return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
            value /= 1024
        return f"{size} B"

    def refresh(self) -> None:
        for item in self.results.get_children():
            self.results.delete(item)
        self._visible_paths.clear()
        files = self.store.search(self.query.get())
        for index, path in enumerate(files):
            relative = path.relative_to(self.store.root)
            try:
                size = path.stat().st_size
            except OSError:
                size = 0
            item_id = str(index)
            self._visible_paths[item_id] = path
            extension = path.suffix.casefold()
            tags = ("word",) if extension in {".doc", ".docx"} else (("pdf",) if extension == ".pdf" else ())
            self.results.insert(
                "", "end", iid=item_id,
                values=(path.name, relative.as_posix(), self._format_size(size)),
                tags=tags,
            )
        self.status.set(f"{len(files):,} file(s) found. Double-click a file or select it and choose Open.")

    def selected_path(self) -> Path | None:
        selection = self.results.selection()
        return self._visible_paths.get(selection[0]) if selection else None

    def open_selected(self) -> None:
        path = self.selected_path()
        if path is None:
            messagebox.showinfo("Open file", "Select a file first.", parent=self.root)
            return
        try:
            open_with_system(path)
        except Exception as error:
            messagebox.showerror("Could not open file", str(error), parent=self.root)

    def open_with_word(self) -> None:
        path = self.selected_path()
        if path is None:
            messagebox.showinfo("Open with Word", "Select a file first.", parent=self.root)
            return
        if sys.platform != "win32":
            messagebox.showerror("Microsoft Word", "This button requires Microsoft Word on Windows.", parent=self.root)
            return
        try:
            subprocess.Popen(["winword", str(path)], close_fds=True)
        except FileNotFoundError:
            messagebox.showerror(
                "Microsoft Word not found",
                "Microsoft Word is not installed or winword.exe is not on PATH.",
                parent=self.root,
            )
        except OSError as error:
            messagebox.showerror("Could not open with Word", str(error), parent=self.root)

    def open_data_folder(self) -> None:
        try:
            open_with_system(self.store.root)
        except Exception as error:
            messagebox.showerror("Could not open folder", str(error), parent=self.root)

    def edit_selected(self) -> None:
        path = self.selected_path()
        if path is None:
            messagebox.showinfo("Edit text file", "Select a file first.", parent=self.root)
            return
        if path.suffix.casefold() not in TEXT_EXTENSIONS:
            messagebox.showinfo(
                "Edit text file",
                "The built-in editor supports common text files only. Use Open for other file types.",
                parent=self.root,
            )
            return
        try:
            original = self.store.read_bytes(path, max_bytes=EDITOR_LIMIT_BYTES)
            content = original.decode("utf-8")
        except (OSError, UnicodeError, ValueError) as error:
            messagebox.showerror("Could not edit file", str(error), parent=self.root)
            return

        original_hash = self.store.sha256(original)
        editor = tk.Toplevel(self.root)
        editor.title(f"Edit - {path.name}")
        editor.geometry("820x560")
        editor.transient(self.root)

        frame = ttk.Frame(editor, padding=8)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text=str(path.relative_to(self.store.root))).pack(anchor="w", pady=(0, 6))
        text = tk.Text(frame, wrap="none", undo=True)
        vertical = ttk.Scrollbar(frame, orient="vertical", command=text.yview)
        horizontal = ttk.Scrollbar(frame, orient="horizontal", command=text.xview)
        text.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        text.grid(row=1, column=0, sticky="nsew")
        vertical.grid(row=1, column=1, sticky="ns")
        horizontal.grid(row=2, column=0, sticky="ew")
        frame.rowconfigure(1, weight=1)
        frame.columnconfigure(0, weight=1)
        text.insert("1.0", content)

        def save() -> None:
            new_content = text.get("1.0", "end-1c")
            try:
                backup = self.store.replace_text(
                    path, new_content, expected_sha256=original_hash
                )
            except FileChangedError as error:
                messagebox.showwarning("File changed", str(error), parent=editor)
                return
            except (OSError, ValueError) as error:
                messagebox.showerror("Save failed", str(error), parent=editor)
                return

            if backup is None:
                messagebox.showinfo("Saved", "No changes to save.", parent=editor)
            else:
                messagebox.showinfo(
                    "Saved",
                    f"Saved atomically. Previous version backed up to:\n{backup}",
                    parent=editor,
                )
            editor.destroy()
            self.refresh()

        buttons = ttk.Frame(frame)
        buttons.grid(row=3, column=0, columnspan=2, sticky="e", pady=(8, 0))
        ttk.Button(buttons, text="Save", command=save).pack(side="right")
        ttk.Button(buttons, text="Cancel", command=editor.destroy).pack(side="right", padx=(0, 6))


def main() -> None:
    root = tk.Tk()
    FileBrowserApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
