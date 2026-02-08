# TractographyTRX Remote Module

This repository provides the TractographyTRX module for TRX tractography,
including lazy streamline access, spatial queries/subsets, and a streaming
writer backed by `trx-cpp`.

## Documentation

Developer-facing documentation lives under `Documentation/` and uses the
ITK/InsightToolkit reStructuredText style. Start with
`Documentation/Overview.rst`.

## Build

### Dependencies

TractographyTRX depends on `trx-cpp`, which provides the `trx-cpp::trx` CMake
target. Ensure `trx-cpp` is discoverable via `find_package(trx-cpp)`.

### Standalone build (requires ITK)

```
mkdir -p build
cd build
cmake ..
cmake --build .
```

### Build as an ITK remote module

Use `TractographyTRX.remote.cmake` with an ITK source tree, or pass the module source
directly:

```
cmake -S path/to/ITK -B itk-build \
  -DITK_EXTERNAL_MODULES=/path/to/trx-itk \
  -DModule_TractographyTRX=ON \
  -Dtrx-cpp_DIR=/path/to/trx-cpp/lib/cmake/trx-cpp
cmake --build itk-build
```
