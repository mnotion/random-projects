from __future__ import annotations

import os
import queue
import shutil
import subprocess
import tempfile
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

try:
    import fitz  # PyMuPDF
    from docx import Document
    from pdf2docx import Converter
except ImportError as exc:
    missing = str(exc).split("'")[1] if "'" in str(exc) else str(exc)
    raise SystemExit(
        f"Missing dependency: {missing}. Run run.bat (Windows) or install requirements.txt."
    ) from exc

APP_NAME = "PDF ↔ Word Converter"
WORD_EXTENSIONS = {".docx", ".doc", ".docm", ".rtf", ".odt"}


def find_libreoffice() -> str | None:
    """Find LibreOffice's command-line executable on common operating systems."""
    found = shutil.which("soffice") or shutil.which("libreoffice")
    if found:
        return found

    candidates = []
    if os.name == "nt":
        for base in (os.environ.get("PROGRAMFILES"), os.environ.get("PROGRAMFILES(X86)")):
            if base:
                candidates.append(Path(base) / "LibreOffice" / "program" / "soffice.exe")
    elif os.name == "darwin":
        candidates.append(Path("/Applications/LibreOffice.app/Contents/MacOS/soffice"))

    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return None


def page_text_lengths(pdf_path: Path) -> list[int]:
    with fitz.open(pdf_path) as pdf:
        return [len(page.get_text("text").strip()) for page in pdf]


def pdf_to_word(pdf_path: Path, docx_path: Path, ocr_language: str) -> str:
    """Convert a text PDF with layout support, or OCR a scanned PDF into editable text."""
    lengths = page_text_lengths(pdf_path)
    page_count = len(lengths)
    # A practical heuristic: if most pages have selectable text, preserve layout with pdf2docx.
    text_pages = sum(length >= 25 for length in lengths)
    if page_count and text_pages / page_count >= 0.8:
        converter = Converter(str(pdf_path))
        try:
            converter.convert(str(docx_path))
        finally:
            converter.close()
        return "PDF→Word (layout conversion)"

    # Scanned or mixed PDFs: use PyMuPDF's Tesseract-powered OCR and write editable text.
    document = Document()
    with fitz.open(pdf_path) as pdf:
        for index, page in enumerate(pdf):
            text_page = page.get_textpage_ocr(language=ocr_language, dpi=300, full=True)
            extracted = page.get_text("text", textpage=text_page).strip()
            if index > 0:
                document.add_page_break()
            if not extracted:
                document.add_paragraph(f"[No text recognised on page {index + 1}]")
                continue
            for line in extracted.splitlines():
                line = line.strip()
                if line:
                    document.add_paragraph(line)
    document.save(docx_path)
    return f"PDF→Word (OCR: {ocr_language})"


def word_to_pdf(word_path: Path, pdf_path: Path, soffice: str) -> None:
    """Convert a Word-compatible file to PDF through LibreOffice in headless mode."""
    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pdf-word-") as temp_dir:
        result = subprocess.run(
            [soffice, "--headless", "--convert-to", "pdf", "--outdir", temp_dir, str(word_path)],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
        generated = Path(temp_dir) / f"{word_path.stem}.pdf"
        if result.returncode != 0 or not generated.is_file():
            detail = (result.stderr or result.stdout or "LibreOffice did not create a PDF.").strip()
            raise RuntimeError(detail)
        shutil.copy2(generated, pdf_path)


class ConverterApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title(APP_NAME)
        self.root.geometry("760x570")
        self.root.minsize(650, 490)
        self.folder_var = tk.StringVar()
        self.direction_var = tk.StringVar(value="pdf_to_word")
        self.language_var = tk.StringVar(value="eng")
        self.recursive_var = tk.BooleanVar(value=True)
        self.events: queue.Queue[tuple] = queue.Queue()
        self.worker: threading.Thread | None = None
        self.soffice: str | None = None
        self._build_ui()
        self.root.after(100, self._process_events)

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, padding=18)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text="PDF ↔ Word Converter", font=("Segoe UI", 18, "bold")).pack(anchor="w")
        ttk.Label(
            outer,
            text="Batch-convert a folder of documents. Scanned PDFs use OCR when needed.",
            wraplength=700,
        ).pack(anchor="w", pady=(4, 16))

        ttk.Label(outer, text="Folder containing your files").pack(anchor="w")
        folder_row = ttk.Frame(outer)
        folder_row.pack(fill="x", pady=(5, 14))
        ttk.Entry(folder_row, textvariable=self.folder_var).pack(side="left", fill="x", expand=True)
        ttk.Button(folder_row, text="Choose folder…", command=self._choose_folder).pack(side="left", padx=(8, 0))

        ttk.Label(outer, text="Conversion direction").pack(anchor="w")
        direction_box = ttk.Frame(outer)
        direction_box.pack(fill="x", pady=(5, 12))
        ttk.Radiobutton(
            direction_box,
            text="PDF → Word (.docx), with OCR for scanned PDFs",
            variable=self.direction_var,
            value="pdf_to_word",
            command=self._update_language_state,
        ).pack(anchor="w", pady=2)
        ttk.Radiobutton(
            direction_box,
            text="Word → PDF (requires LibreOffice)",
            variable=self.direction_var,
            value="word_to_pdf",
            command=self._update_language_state,
        ).pack(anchor="w", pady=2)

        options = ttk.Frame(outer)
        options.pack(fill="x", pady=(0, 12))
        ttk.Checkbutton(
            options, text="Include subfolders", variable=self.recursive_var
        ).pack(side="left")
        ttk.Label(options, text="OCR language code:").pack(side="left", padx=(18, 6))
        self.language_entry = ttk.Entry(options, textvariable=self.language_var, width=10)
        self.language_entry.pack(side="left")
        ttk.Label(options, text="(e.g. eng, fra, deu)", foreground="#555555").pack(side="left", padx=6)

        self.convert_button = ttk.Button(outer, text="Convert files", command=self._start_conversion)
        self.convert_button.pack(anchor="w", pady=(0, 10))
        self.progress = ttk.Progressbar(outer, mode="determinate")
        self.progress.pack(fill="x", pady=(0, 8))

        ttk.Label(outer, text="Activity").pack(anchor="w")
        log_frame = ttk.Frame(outer)
        log_frame.pack(fill="both", expand=True, pady=(5, 0))
        self.log = tk.Text(log_frame, height=12, wrap="word", state="disabled", font=("Consolas", 9))
        scrollbar = ttk.Scrollbar(log_frame, command=self.log.yview)
        self.log.configure(yscrollcommand=scrollbar.set)
        self.log.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self._update_language_state()

    def _choose_folder(self) -> None:
        chosen = filedialog.askdirectory(title="Choose folder to convert")
        if chosen:
            self.folder_var.set(chosen)

    def _update_language_state(self) -> None:
        state = "normal" if self.direction_var.get() == "pdf_to_word" else "disabled"
        self.language_entry.configure(state=state)

    def _append_log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _start_conversion(self) -> None:
        if self.worker and self.worker.is_alive():
            return
        folder_text = self.folder_var.get().strip()
        if not folder_text or not Path(folder_text).is_dir():
            messagebox.showerror("Choose a folder", "Choose an existing folder containing your documents.")
            return

        direction = self.direction_var.get()
        if direction == "word_to_pdf":
            self.soffice = find_libreoffice()
            if not self.soffice:
                messagebox.showerror(
                    "LibreOffice not found",
                    "Install LibreOffice and make sure soffice is on PATH, then reopen the converter.",
                )
                return
        language = self.language_var.get().strip() or "eng"
        self.progress.configure(value=0, maximum=1)
        self.convert_button.configure(state="disabled")
        self._append_log(f"Starting: {folder_text}")
        self.worker = threading.Thread(
            target=self._convert_folder,
            args=(Path(folder_text).resolve(), direction, language, self.recursive_var.get(), self.soffice),
            daemon=True,
        )
        self.worker.start()

    def _convert_folder(
        self, root: Path, direction: str, language: str, recursive: bool, soffice: str | None
    ) -> None:
        output_name = "Converted_Word" if direction == "pdf_to_word" else "Converted_PDF"
        output_root = root / output_name
        accepted = {".pdf"} if direction == "pdf_to_word" else WORD_EXTENSIONS
        iterator = root.rglob("*") if recursive else root.iterdir()
        files = sorted(
            path for path in iterator
            if path.is_file()
            and path.suffix.lower() in accepted
            and output_root not in path.parents
            and not path.name.startswith("~$")
        )

        if not files:
            self.events.put(("log", f"No supported files found ({', '.join(sorted(accepted))})."))
            self.events.put(("finished", 0, 0, 0, str(output_root)))
            return

        self.events.put(("maximum", len(files)))
        succeeded = failed = 0
        for index, source in enumerate(files, start=1):
            try:
                relative = source.relative_to(root)
                suffix = ".docx" if direction == "pdf_to_word" else ".pdf"
                destination = (output_root / relative).with_suffix(suffix)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if direction == "pdf_to_word":
                    method = pdf_to_word(source, destination, language)
                else:
                    assert soffice is not None
                    word_to_pdf(source, destination, soffice)
                    method = "Word→PDF"
                succeeded += 1
                self.events.put(("log", f"[{index}/{len(files)}] OK  {relative} — {method}"))
            except Exception as exc:  # Keep processing the rest of the batch.
                failed += 1
                self.events.put(("log", f"[{index}/{len(files)}] FAIL  {source.relative_to(root)} — {exc}"))
            self.events.put(("progress", index))

        self.events.put(("finished", succeeded, failed, len(files), str(output_root)))

    def _process_events(self) -> None:
        try:
            while True:
                event = self.events.get_nowait()
                kind = event[0]
                if kind == "log":
                    self._append_log(event[1])
                elif kind == "maximum":
                    self.progress.configure(maximum=event[1], value=0)
                elif kind == "progress":
                    self.progress.configure(value=event[1])
                elif kind == "finished":
                    _, succeeded, failed, total, output = event
                    self.convert_button.configure(state="normal")
                    self._append_log(f"Finished: {succeeded} converted, {failed} failed. Output: {output}")
                    if total:
                        messagebox.showinfo(
                            "Conversion complete",
                            f"Converted: {succeeded}\nFailed: {failed}\n\nOutput folder:\n{output}",
                        )
        except queue.Empty:
            pass
        self.root.after(100, self._process_events)


def main() -> None:
    root = tk.Tk()
    ConverterApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
