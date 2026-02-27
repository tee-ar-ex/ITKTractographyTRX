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
target. It is fetched automatically by default, or you can make it discoverable
via `find_package(trx-cpp)` (e.g. by setting `trx-cpp_DIR`).

Benchmark builds additionally require Google Benchmark (`benchmark::benchmark`).

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

### Benchmarks

```
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTractographyTRX_BUILD_BENCHMARKS=ON
cmake --build build-release --target bench_trx_itk_realdata
```

See `Documentation/Benchmarks.rst` for dataset setup and benchmark flags.
