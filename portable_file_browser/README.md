# Portable File Browser

A small file browser for a folder named **stuff** beside the app. It recursively lists files, searches file names and relative paths, and searches inside common text files up to 2 MB. Double-click a result or select it and press **Open** to launch it with the operating system's default application.

## Data layout

    FileBrowser.exe
    stuff/
      backups/
      documents/
      any files you want to find and open

The app creates stuff/ and stuff/backups/ on first launch. Put files in stuff/ or any subfolder. The backups folder is reserved and excluded from search results.

When running from source, stuff/ is beside app.py. When running the packaged executable, stuff/ is beside FileBrowser.exe (not in PyInstaller's temporary extraction directory). Move the executable and its data folder together to retain the files and backups.

## Safe editing and backups

Use **Edit text file** for common UTF-8 text formats. Every changed save through the built-in editor:

1. Writes the new contents to a temporary file in the destination folder and flushes it to disk.
2. Copies the previous version into stuff/backups/, preserving its relative subfolder and using a unique timestamped name.
3. Atomically replaces the original using os.replace only after the backup is safely written.
4. Refuses to save if the file changed externally since it was loaded in the editor.

The original remains in place if preparing the replacement or backup fails. New files have no previous version to back up; unchanged saves do not create redundant backups. Temporary files are created on the same volume as their final destination so replacement can be atomic.

Opening a file in an external application delegates editing and saving to that application. The browser cannot intercept or guarantee atomic saves performed by third-party applications; use **Edit text file** when you need the app's backup-and-replace behavior.

## Build a standalone Windows executable

Build on a Windows 10/11 x64 computer:

1. Install Python 3 on the build computer. Python is only needed to build the executable.
2. Run build_windows.bat.
3. The executable is created at dist/FileBrowser.exe.
4. Copy FileBrowser.exe and the adjacent dist/stuff/ folder together to another Windows PC. Double-click FileBrowser.exe.

The build script creates an isolated build environment, installs PyInstaller, runs unit tests, and packages the app as a **single-file, windowed executable**. The packaged executable includes the Python runtime and Tk support. The target computer does not need Python, pip, PyInstaller, or any third-party Python library installed.

A Windows .exe is not a universal macOS/Linux application. To run on macOS or Linux, build a separate executable on that operating system using PyInstaller there.

## Run from source / tests

    python app.py
    python -m unittest discover -s tests -v

Runtime app code uses only Python's standard library.
