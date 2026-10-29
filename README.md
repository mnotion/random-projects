# PageConvert — PDF ↔ Word

A small C++/Qt desktop utility for batch-converting a folder of documents. It turns every page into a PNG image and places those images into the requested output format. **It does not run OCR or extract text.**

## What it does

- **PDF → Word:** renders every PDF page to a PNG at 600 DPI and embeds each PNG as a full-page image in a .docx file.
- **Word → PDF:** uses LibreOffice to lay out Word-compatible documents, then renders that PDF to PNG pages and builds an image-only PDF.
- Scans a selected folder, optionally including subfolders, and preserves the relative folder structure.
- Writes output to Converted_Word or Converted_PDF inside the selected folder. It skips both output folders on later runs.
- Leaves input files untouched. A matching existing output file can be replaced.

## Important: what "lossless PNG" means here

PNG compression is lossless, so the stored raster pixels are not compressed with JPEG-style quality loss. However, a PDF or Word document is being **rasterised** to pixels: this does not preserve the original vector shapes, embedded fonts, searchable text, links, form fields, or document structure. The resulting Word document contains page images rather than editable text, and the resulting PDF contains image pages rather than a selectable text layer. No OCR is performed.

The app always renders at 600 DPI. Higher-resolution output can produce large files and use substantial memory.

## Requirements

- C++17 compiler compatible with your Qt kit.
- CMake 3.21 or newer.
- Qt 6.5 or newer, including the **Qt PDF** module, plus Qt Core, Gui and Widgets.
- LibreOffice for Word → PDF. PDF → Word does not require LibreOffice.

## Build on Windows

1. Install Qt with the Qt Online Installer. Select a Qt 6.5+ kit that matches your compiler (for example, an MSVC 64-bit kit) and include the Qt PDF module.
2. Install CMake and the matching C++ compiler. The compiler/SDK must match the Qt kit you installed.
3. Install LibreOffice from https://www.libreoffice.org/download/download-libreoffice/ if you need Word → PDF.
4. Open CMakeLists.txt in Qt Creator, select the matching desktop kit, then build and run PageConvert.

Alternatively, run build_and_run.bat in a developer command prompt where CMake, the compiler, and the Qt kit are configured. If CMake cannot locate Qt, set CMAKE_PREFIX_PATH to the Qt kit directory first, for example C:\Qt\6.x.x\msvc2022_64.

## Use

1. Launch PageConvert and select the folder containing the source documents.
2. Choose PDF → Word or Word → PDF.
3. Conversion always uses the maximum render setting: 600 DPI.
4. Leave **Include subfolders** enabled to process all matching files below the selected folder.
5. Select **Convert folder**. The app shows progress and logs failed files without stopping the rest of the batch.

Word-compatible formats recognised for the second direction are .docx, .doc, .docm, .rtf, .odt, .ott, and .fodt. Layout in the temporary PDF depends on LibreOffice and can differ from Microsoft Word, especially when fonts or document features are missing.

## Technical notes

- The app and conversion logic are written in C++ using Qt 6 Widgets, Qt PDF, and Qt's PDF writer.
- The .docx package is assembled in C++ and stores the generated PNGs without additional ZIP compression; the PNG files themselves use lossless compression.
- For unusually large page sizes, the app may report that a page exceeds its safety limit.
- Password-protected or damaged PDFs may not be readable by Qt PDF.
- Source documents are processed locally. No documents are uploaded to a service.

## Create the portable app (one-click script)

1. Open the project in Qt Creator and make sure it has been configured with the **Desktop Qt 6.12.0 MinGW 64-bit** kit at least once.
2. Double-click **`make_portable.bat`** in the repository folder.
3. When it says **DONE**, copy the whole **`dist/PageConvert`** folder to the other Windows PC and open `PageConvert.exe`.

The script automatically builds a Release version and collects the Qt runtime files. The other PC does not need Qt installed. This is a portable folder, not a single-file EXE. If the script reports that it cannot find the kit, open the project in Qt Creator and configure the MinGW kit first.

**Separate requirement:** Word → PDF uses LibreOffice to lay out documents. The other PC must have LibreOffice installed for that feature; the script does not bundle LibreOffice. PDF → Word does not require LibreOffice.
