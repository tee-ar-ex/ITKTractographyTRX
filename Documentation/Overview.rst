TractographyTRX Module Overview
===============================

TractographyTRX provides ITK support for TRX tractography streamlines, backed by the
``trx-cpp`` library. The module includes both lazy, queryable access and a
streaming writer that enforces DPS/DPV synchronization.

Key Types
---------

``itk::TrxStreamlineData``
  ITK data object for streamlines with lazy, TRX-backed storage. Supports
  streamlined access by index, range iteration, AABB queries, and subset
  extraction.

``itk::TrxStreamWriter``
  Streaming TRX writer that ingests ITK 3d Point vectors (streamlines) and metadata 
  while enforcing consistent DPS/DPV lengths.

``itk::TrxStreamlineIO`` / ``itk::TrxStreamlineIOFactory``
  IO and factory integration for ITK's module discovery mechanism.

Coordinate Systems
------------------

TRX internally uses RAS+ world coordinates: a few sign flips different from ITK's LPS+ coordinates.
This fact is something that users will never need to know about.
All ITK-facing functions accept and return LPS+ coordinates.
Spatial queries accept LPS+ points and are converted to
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

Common Use Case: Streaming Generated Tracts
-------------------------------------------

When a tracking algorithm emits streamlines incrementally, use
``itk::TrxStreamWriter`` to push each streamline along with per-vertex scalars
(DPV), per-streamline measures (DPS), and optional group membership. Group
assignments are passed via the ``groupNames`` argument to ``PushStreamline`` and
are stored as TRX ``groups/`` entries when ``Finalize`` is called.

If writing on slow disks, you can optionally buffer streamed positions in
memory before they are flushed to the temporary file:

.. code-block:: cpp

  writer->SetPositionsBufferMaxBytes(16 * 1024);

Example sketch (simplified):

.. code-block:: cpp

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName("streamed.trx");
  writer->RegisterDpsField("shape", "float32");
  writer->RegisterDpvField("scalar", "float32");

  for (size_t i = 0; i < streamlines.size(); ++i)
  {
    const auto & points = streamlines[i];
    const auto & dpv = perVertexScalars[i];
    const double shape = shapeMeasures[i];
    const std::string group = "Group" + std::to_string(i / 10);
    writer->PushStreamline(points, { { "shape", shape } }, { { "scalar", dpv } }, { group });
  }

  writer->Finalize();

Querying an AABB (sketch)
-------------------------

To extract streamlines that intersect a rectangular region, call
``TrxStreamlineData::QueryAabb`` with min/max corners in LPS space. An overload
lets you cap the number of returned streamlines for interactive workflows. This
is a minimal sketch to illustrate the API (error handling omitted):

.. code-block:: cpp

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName("tracks.trx");
  reader->Update();

  auto data = reader->GetOutput();

  itk::TrxStreamlineData::PointType minCorner;
  minCorner[0] = -30.0;
  minCorner[1] = 5.0;
  minCorner[2] = -10.0;
  itk::TrxStreamlineData::PointType maxCorner;
  maxCorner[0] = -5.0;
  maxCorner[1] = 25.0;
  maxCorner[2] = 15.0;

  auto subset = data->QueryAabb(minCorner, maxCorner);
  // subset now contains only streamlines intersecting the box.

Tests
-----

The basic round-trip test ``itkTrxReadWriteTest`` validates streaming write and
read-back behavior.
