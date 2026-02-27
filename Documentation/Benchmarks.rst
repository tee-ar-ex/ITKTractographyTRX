Benchmarks
==========

TractographyTRX includes a benchmark suite that mirrors the ``trx-cpp`` real-data
benchmarks (excluding file size) so ITK wrapper performance can be compared with
raw ``trx-cpp`` access. The suite focuses on:

* Translate + stream write: read streamlines, apply a +1 mm translation, and
  stream the result to a new TRX file.
* Spatial slab queries: issue 20 AABB slab queries to emulate interactive
  slicing.

Data
----

The recommended reference dataset is the same HCP-derived TRX file used by
``trx-cpp``. For local runs:

``/Users/mcieslak/projects/trx-cpp/test-data/10milHCP_dps-sift2.trx``

Build (standalone)
------------------

Configure and build with benchmarks enabled:

.. code-block:: bash

  cmake -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DTractographyTRX_BUILD_BENCHMARKS=ON
  cmake --build build-release --target bench_trx_itk_realdata

If ``trx-cpp`` or Google Benchmark are installed elsewhere, make sure CMake can
find them using ``trx-cpp_DIR`` and ``benchmark_DIR``. Otherwise, ``trx-cpp`` is
fetched automatically by default.

Run the benchmarks
------------------

The helper script mirrors ``trx-cpp``'s flow and writes JSON output for plots:

.. code-block:: bash

  bash bench/run_benchmarks.sh \
    --reference /path/to/reference.trx \
    --out-dir bench/results

The benchmark binary also supports ``--reference-trx`` directly:

.. code-block:: bash

  ./build-release/bench/bench_trx_itk_realdata \
    --reference-trx /path/to/reference.trx \
    --benchmark_out=bench/results/bench_trx_itk_realdata.json \
    --benchmark_out_format=json

Environment variables
---------------------

``bench_trx_itk_realdata`` reuses several ``trx-cpp`` environment variables:

* ``TRX_BENCH_MAX_STREAMLINES``: cap streamline counts (default 10,000,000)
* ``TRX_BENCH_ONLY_STREAMLINES``: run a single streamline count
* ``TRX_BENCH_LOG_PROGRESS_EVERY``: emit progress every N streamlines
* ``TRX_BENCH_CHUNK_BYTES``: buffer size for ITK streaming writes
* ``TRX_BENCH_MAX_QUERY_STREAMLINES``: cap raw ``trx-cpp`` query results

Notes on comparison
-------------------

* The ITK ``QueryAabb`` wrapper does not currently expose a max-streamline
  sampling cap, so the ITK benchmark always returns the full intersecting set.
  For closer comparison, set ``TRX_BENCH_MAX_QUERY_STREAMLINES=0`` when running
  the raw ``trx-cpp`` benchmark to disable sampling.
