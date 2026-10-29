# PageConvert — PDF ↔ Word

A small C++/Qt desktop utility for batch-converting a folder of documents. It turns every page into a PNG image and places those images into the requested output format. **It does not run OCR or extract text.**

## What it does

- **PDF → Word:** renders every PDF page to a PNG and embeds each PNG as a full-page image in a `.docx` file.
- **PDF → Word (Legacy .doc):** uses LibreOffice to export the image-page document to the older `.doc` format.
- **Word → PDF:** uses LibreOffice to lay out Word-compatible documents, then renders that PDF to PNG pages and builds an image-only PDF.
- **Render quality:** choose Fast (150 DPI), Balanced (300 DPI, default), or Maximum (600 DPI).
- Scans a selected folder, optionally including subfolders, and preserves the relative folder structure.
- Writes output to Converted_Word or Converted_PDF inside the selected folder. It skips both output folders on later runs.
- Leaves input files untouched. A matching existing output file can be replaced.

## Important: what "lossless PNG" means here

PNG compression is lossless, so the stored raster pixels are not compressed with JPEG-style quality loss. However, a PDF or Word document is being **rasterised** to pixels: this does not preserve the original vector shapes, embedded fonts, searchable text, links, form fields, or document structure. The resulting Word document contains page images rather than editable text, and the resulting PDF contains image pages rather than a selectable text layer. No OCR is performed.

The app defaults to 300 DPI for a speed/quality balance. Choose 600 DPI when fine detail matters more than speed and file size; choose 150 DPI for faster conversions.

## Requirements

- C++17 compiler compatible with your Qt kit.
- CMake 3.21 or newer.
- Qt 6.5 or newer, including the **Qt PDF** module, plus Qt Core, Gui and Widgets.
- LibreOffice for Word → PDF and PDF → Word (Legacy .doc). Standard PDF → Word does not require LibreOffice.

## Build on Windows

1. Install Qt with the Qt Online Installer. Select a Qt 6.5+ kit that matches your compiler (for example, an MSVC 64-bit kit) and include the Qt PDF module.
2. Install CMake and the matching C++ compiler. The compiler/SDK must match the Qt kit you installed.
3. Install LibreOffice from https://www.libreoffice.org/download/download-libreoffice/ if you need Word → PDF or PDF → Word (Legacy .doc).
4. Open CMakeLists.txt in Qt Creator, select the matching desktop kit, then build and run PageConvert.

Alternatively, run build_and_run.bat in a developer command prompt where CMake, the compiler, and the Qt kit are configured. If CMake cannot locate Qt, set CMAKE_PREFIX_PATH to the Qt kit directory first, for example C:\Qt\6.x.x\msvc2022_64.

## Use

1. Launch PageConvert and select the folder containing the source documents.
2. Choose PDF → Word, PDF → Word (Legacy .doc), or Word → PDF.
3. Choose Fast (150 DPI), Balanced (300 DPI), or Maximum (600 DPI).
4. Leave **Include subfolders** enabled to process all matching files below the selected folder.
5. Select **Convert folder**. The app shows progress and logs failed files without stopping the rest of the batch.

Word-compatible formats recognised for the second direction are .docx, .doc, .docm, .rtf, .odt, .ott, and .fodt. Layout in the temporary PDF depends on LibreOffice and can differ from Microsoft Word, especially when fonts or document features are missing.

## Technical notes

- The app and conversion logic are written in C++ using Qt 6 Widgets, Qt PDF, and Qt's PDF writer.
- The .docx package is assembled in C++ and stores the generated PNGs without additional ZIP compression; the PNG files themselves use lossless compression. CRC checksums use a lookup table to reduce processing overhead.
- For unusually large page sizes, the app may report that a page exceeds its safety limit.
- Password-protected or damaged PDFs may not be readable by Qt PDF.
- Source documents are processed locally. No documents are uploaded to a service.

## Create the portable app (one-click script)

1. Double-click **`make_portable.bat`** in the repository folder.
2. The script locates Qt and MinGW, builds a Release version, and collects the runtime files.
3. When it says **DONE**, copy the whole **`dist/PageConvert`** folder to the other Windows PC and open `PageConvert.exe`.

This requires Qt 6.12 MinGW 64-bit, the matching MinGW compiler and CMake installed on the development PC. If the script says one is missing, install it through Qt Maintenance Tool. The other PC does not need Qt installed. This is a portable folder, not a single-file EXE.

**Separate requirement:** Word → PDF and PDF → Word (Legacy .doc) use LibreOffice. The other PC must have LibreOffice installed for those features; the script does not bundle LibreOffice. Standard PDF → Word does not require LibreOffice.
