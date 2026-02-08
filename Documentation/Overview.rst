TractographyTRX Module Overview
===============================

TractographyTRX provides ITK support for TRX tractography streamlines, backed by the
``trx-cpp`` library. The module includes both lazy, queryable access and a
streaming writer that enforces DPS/DPV synchronization.

Design Goals
------------

* Preserve TRX's native RAS+ storage while exposing LPS+ coordinates in ITK.
* Enable lazy access to streamline points and fast subsetting/query operations.
* Provide a streaming writer that keeps per-streamline/per-vertex metadata in
  sync without manual bookkeeping.

Key Types
---------

``itk::TrxStreamlineData``
  ITK data object for streamlines with lazy, TRX-backed storage. Supports
  streamlined access by index, range iteration, AABB queries, and subset
  extraction.

``itk::TrxStreamWriter``
  Streaming TRX writer that ingests ITK streamlines and metadata while enforcing
  consistent DPS/DPV lengths.

``itk::TrxStreamlineIO`` / ``itk::TrxStreamlineIOFactory``
  IO and factory integration for ITK's module discovery mechanism.

Coordinate Systems
------------------

TRX uses RAS+. TractographyTRX preserves that internally and converts to LPS+ when
returning ITK points. Spatial queries accept LPS+ points and are converted to
RAS+ before being passed into ``trx-cpp``.

Lazy Loading and Queries
------------------------

Streamline coordinates are materialized only when needed. Subsetting and AABB
queries delegate to ``trx-cpp`` when a TRX-backed handle is available, returning
new ITK objects that remain lazily backed by the subset.

Streaming Writer Semantics
--------------------------

The streaming writer registers DPS/DPV fields up front and validates that each
streamline provides all required values with correct lengths, preventing
out-of-sync metadata.

Tests
-----

The basic round-trip test ``itkTrxReadWriteTest`` validates streaming write and
read-back behavior.
