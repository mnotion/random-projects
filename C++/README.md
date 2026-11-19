# GitHub Activity Window (C++)

This is a desktop window built with **Qt 6 Widgets**. It displays a GitHub-style contribution calendar with fictional activity, humorous text, and working buttons. It is not a web page, and opening `main.cpp` in an editor will only show the source code.

## Requirements

- CMake 3.21 or newer
- A C++17 compiler (Visual Studio 2022 with the **Desktop development with C++** workload is suitable)
- Qt 6.5 or newer, including the **Widgets** component

## Build and run on Windows

1. Install Qt 6 with the Qt Widgets component and install CMake and Visual Studio's C++ desktop tools.
2. Open a terminal in this `C++` folder.
3. Configure the project, making sure CMake can find your Qt installation:

   ```powershell
   cmake -S . -B build
   ```

4. Build the app:

   ```powershell
   cmake --build build --config Release
   ```

5. Run `build\\Release\\SimpleScreen.exe` (or `build\\SimpleScreen.exe` if using a single-configuration generator).

If CMake cannot find Qt, set `CMAKE_PREFIX_PATH` to the Qt installation directory that contains the Qt 6 CMake package files, then rerun the configure command.

## Important

The contribution squares and the displayed count are sample data. The app does not connect to GitHub or read your account activity.
