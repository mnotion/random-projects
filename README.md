# PDF ↔ Word Converter

A small desktop utility that batch-converts documents in a chosen folder:

- **PDF → Word:** creates editable .docx files. PDFs with selectable text use a layout-aware converter; scanned or mixed PDFs use OCR to recognise text.
- **Word → PDF:** converts Word-compatible documents to PDF through LibreOffice.
- Processes files in a folder, optionally including subfolders. The relative folder structure is preserved.
- Writes results into Converted_Word or Converted_PDF inside the selected folder; source files are left untouched.

## Windows setup

1. Install Python 3.11 or newer from https://www.python.org/downloads/.
2. Install LibreOffice from https://www.libreoffice.org/download/download-libreoffice/ for Word → PDF.
3. Install Tesseract OCR from https://github.com/UB-Mannheim/tesseract/wiki for scanned PDF → Word. Include the language data you need and make sure Tesseract is on your PATH. English uses the language code eng. The app tries to find the tessdata folder automatically; if OCR cannot find it, set the TESSDATA_PREFIX environment variable to that folder (commonly C:/Program Files/Tesseract-OCR/tessdata).
4. Double-click run.bat. On first launch it creates a local virtual environment and installs the Python packages.
5. Choose the folder, select a conversion direction, and click Convert files.

The Python dependencies can also be installed manually with python -m pip install -r requirements.txt, then started with python pdf_word_converter.py.

## Notes and limitations

- This initial version converts PDF files to .docx and supports .docx, .doc, .docm, .rtf, and .odt files in the Word → PDF direction.
- OCR results depend on scan quality, page rotation, language packs, and the document font. OCR-created Word files prioritise editable recognised text; their layout may not exactly match the scanned pages.
- Text-based PDF → Word conversion preserves layout better, but complex columns, forms, unusual fonts, and tables may still need correction.
- LibreOffice is an external requirement for Word → PDF. Tesseract OCR is an external requirement when a PDF needs OCR.
- The tool never deletes the original files. If an output already exists, it may be replaced by the new conversion.
