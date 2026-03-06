Benchmarks
==========

TractographyTRX includes a benchmark suite that mirrors the ``trx-cpp`` real-data
benchmarks and extends them with ITK-specific workloads. The suite focuses on:

* Translate + stream write: read streamlines, apply a +1 mm translation, and
  stream the result to a new TRX file.
* Spatial slab queries: issue 20 AABB slab queries to emulate interactive
  slicing.
* Atlas parcellation: assign streamlines to TRX groups with
  ``TrxParcellationLabeler``.
* Group TDI mapping: produce a density image for a selected TRX group.

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

The helper script writes JSON output for plots and accepts explicit fairness
controls:

.. code-block:: bash

  bash bench/run_benchmarks.sh \
    --reference /path/to/reference.trx \
    --bench-data-dir /path/to/bench-data \
    --query-cap 500 \
    --out-dir bench/results

The benchmark binary also supports ``--reference-trx`` directly:

.. code-block:: bash

  ./build-release/bench/bench_trx_itk_realdata \
    --reference-trx /path/to/reference.trx \
    --benchmark_out=bench/results/bench_trx_itk_realdata.json \
    --benchmark_out_format=json

Plot benchmark results
----------------------

An R plotting helper is provided at
``bench/plot_bench.R``. It reads benchmark JSON output and generates PNG plots
for translate/write runtime + RSS, AABB query runtime + p50/p95, parcellation
runtime + storage overhead, and Group TDI runtime + RSS (when rows are present).

.. code-block:: bash

  Rscript bench/plot_bench.R \
    --bench-dir bench/results/publication \
    --out-dir bench/results/plots \
    --run-type iteration \
    --strict TRUE

Required R packages:

* ``jsonlite``
* ``ggplot2``
* ``dplyr``
* ``tidyr``
* ``scales``

Environment variables
---------------------

``bench_trx_itk_realdata`` reuses several ``trx-cpp`` environment variables:

* ``TRX_BENCH_MAX_STREAMLINES``: cap streamline counts (default 10,000,000)
* ``TRX_BENCH_ONLY_STREAMLINES``: run a single streamline count
* ``TRX_BENCH_LOG_PROGRESS_EVERY``: emit progress every N streamlines
* ``TRX_BENCH_CHUNK_BYTES``: buffer size for ITK streaming writes
* ``TRX_BENCH_MAX_QUERY_STREAMLINES``: cap AABB query results (publication runs
  should use a positive value shared across backends)

Notes on comparison
-------------------

* Publication runs should always use capped query results for both backends.
  The recommended setting is ``TRX_BENCH_MAX_QUERY_STREAMLINES=500`` to match
  interactive viewer behavior.
* ``run_benchmarks.sh`` now enforces a positive cap; zero/uncapped query runs
  are not used for manuscript figures.
