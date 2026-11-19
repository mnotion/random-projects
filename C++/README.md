# C++ Starter App

A minimal Qt 6 Widgets desktop application. It opens a simple welcome screen with a working **Say hello** button and a **Close** button.

## Requirements

- CMake 3.21 or newer
- A C++17 compiler
- Qt 6.5 or newer, with the Widgets component

## Build

From this directory:

```sh
cmake -S . -B build
cmake --build build
```

Run the generated `SimpleScreen` executable from the build directory (the exact location depends on your platform and build configuration).

On Windows with a multi-configuration generator, you may need:

```sh
cmake --build build --config Release
```

This is a separate starter project; it does not change the existing applications in the repository.
