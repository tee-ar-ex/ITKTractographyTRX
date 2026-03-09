API Reference
=============

This page provides a concise reference for the public API of each class.
Full Doxygen documentation is available in the header files under ``include/``.

----

``itk::TrxStreamlineData``
---------------------------

Central data container for TRX tractography data. Inherits ``itk::DataObject``.
Holds lazy-loaded streamline positions (float16, float32, or float64) and
per-streamline offsets. Supports AABB spatial queries, lazy and eager subsetting,
named group access, DPS/DPV field retrieval, and group connectivity computation.

**Streamline access**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``GetNumberOfStreamlines()``
     - Total number of streamlines
   * - ``GetNumberOfVertices()``
     - Total number of vertices across all streamlines
   * - ``GetStreamline(i)``
     - Streamline *i* as ``std::vector<PointType>`` in LPS+
   * - ``GetStreamlineRange(i)``
     - Forward-iterable range over LPS+ points of streamline *i*
   * - ``GetStreamlineView(i, view)``
     - Zero-copy raw pointer view; returns false if positions are not yet loaded
   * - ``ForEachStreamlineChunked(fn)``
     - Iterate all streamlines with on-the-fly coordinate conversion

**Subsetting and queries**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``QueryAabb(min, max)``
     - Streamlines whose bounding boxes intersect the LPS+ box
   * - ``QueryAabb(min, max, cache, maxN, seed)``
     - As above, capped at *maxN* streamlines (random sample when over limit)
   * - ``SubsetStreamlines(ids)``
     - Eager subset by index list
   * - ``SubsetStreamlinesLazy(ids)``
     - Lazy subset; delegates to TRX handle when available

**Groups**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``HasGroups()``
     - True if the file contains at least one named group
   * - ``GetGroupNames()``
     - Names of all groups in file order
   * - ``GetGroup(name)``
     - ``TrxGroup::Pointer`` for the named group; cached after first call
   * - ``GetGroupStreamlineCount(name)``
     - Streamline count for a group without loading indices

**DPS / DPV fields**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``GetDpsFieldNames()``
     - Names of all per-streamline scalar fields
   * - ``GetDpvFieldNames()``
     - Names of all per-vertex scalar fields
   * - ``GetDpsField(name)``
     - Per-streamline values widened to ``float``; flat row-major for multi-column fields
   * - ``GetDpvField(name)``
     - Per-vertex values widened to ``float``

**Transforms**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``TransformInPlace(transform)``
     - Apply ITK transform to all positions in memory
   * - ``TransformToWriterChunked(transform, writer)``
     - Stream transformed positions to a ``TrxStreamWriter`` in chunks
   * - ``TransformToWriterChunkedReuseVnlBuffer(transform, writer, buf)``
     - As above, reusing a caller-supplied ``vnl_matrix<double>`` buffer

**Connectivity**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``ComputeGroupConnectivity(dpsField="")``
     - N×N symmetric matrix of shared streamline counts (or weighted sums)

**Metadata**

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``GetVoxelToRasMatrix()``
     - 4×4 voxel-to-RAS affine from the TRX header
   * - ``GetVoxelToLpsMatrix()``
     - 4×4 voxel-to-LPS affine
   * - ``GetDimensions()``
     - Grid dimensions from the TRX header
   * - ``Save(filename, useCompression)``
     - Write the data to a new TRX file

----

``itk::TrxStreamWriter``
-------------------------

Incremental streaming writer for TRX files. Inherits ``itk::Object``. DPS and
DPV fields must be registered before the first ``PushStreamline()`` call. Every
push must supply values for all registered fields with consistent lengths.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``SetFileName(path)``
     - Output file path
   * - ``UseCompressionOn()`` / ``UseCompressionOff()``
     - Enable or disable ZIP compression (default: off)
   * - ``RegisterDpsField(name, dtype)``
     - Declare a per-streamline field; ``dtype`` is ``"float32"``, ``"float64"``, etc.
   * - ``RegisterDpvField(name, dtype)``
     - Declare a per-vertex field
   * - ``PushStreamline(pts, dps, dpv)``
     - Push one streamline as ``std::vector<itk::Point<double,3>>``
   * - ``PushStreamline(mat, dps, dpv)``
     - Push one streamline as an N×3 ``vnl_matrix<double>``
   * - ``Finalize()``
     - Seal the ZIP archive; required before the file is usable
   * - ``SetPositionsBufferMaxBytes(n)``
     - Buffer positions in memory before flushing (useful on slow disks)

----

``itk::TrxFileReader``
-----------------------

``ProcessObject`` wrapper for reading TRX files into a ``TrxStreamlineData``.
Follows the ``itk::ImageFileReader`` interface pattern.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``SetFileName(path)``
     - Input file path
   * - ``Update()``
     - Open the file and populate the output object (positions remain lazy)
   * - ``GetOutput()``
     - ``TrxStreamlineData::Pointer``

----

``itk::TrxFileWriter``
-----------------------

``ProcessObject`` wrapper for writing a ``TrxStreamlineData`` to a TRX file.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``SetFileName(path)``
     - Output file path
   * - ``SetInput(data)``
     - ``TrxStreamlineData`` to write
   * - ``Update()``
     - Write the file

----

``itk::TrxGroup``
------------------

Represents a named bundle of streamlines. Inherits ``itk::Object``. Created
by ``TrxStreamlineData::GetGroup()``; do not construct directly. Streamline
indices are loaded lazily on first access.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``GetName()``
     - Group name as stored in the TRX file
   * - ``GetStreamlineIndices()``
     - Indices into the parent ``TrxStreamlineData``
   * - ``GetStreamlines(parent)``
     - Lazy ``TrxStreamlineData`` subset; no position data is copied
   * - ``GetDpgFieldNames()``
     - Names of per-group scalar metadata fields
   * - ``HasDpgField(name)``
     - True if the named DPG field exists
   * - ``GetDpgField(name)``
     - Per-group scalar values as ``std::vector<float>``
   * - ``SetVisible(bool)``
     - Observer-pattern visibility toggle (for GUI integration)
   * - ``SetColor(r, g, b)``
     - Observer-pattern color assignment (for GUI integration)

----

``itk::TrxParcellationLabeler``
--------------------------------

Assigns streamlines to named TRX groups based on one or more NIfTI-1
parcellation images. Inherits ``itk::Object``. Each label image contributes
groups whose names follow the pattern ``<prefix>_<label_name>``.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``SetInput(data)``
     - ``TrxStreamlineData`` to parcellate
   * - ``AddAtlas(niftiPath, labelTxtPath, prefix)``
     - Register a parcellation atlas; may be called multiple times
   * - ``SetDilationRadius(n)``
     - Expand each parcel by *n* voxels before intersection testing
   * - ``Update()``
     - Run parcellation and populate output groups
   * - ``GetOutput()``
     - ``TrxStreamlineData::Pointer`` with groups appended

----

``itk::TrxGroupTdiMapper``
---------------------------

Maps streamlines from selected groups onto a reference NIfTI grid to produce
a track density image. Inherits ``itk::Object``.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Method
     - Description
   * - ``SetInput(data)``
     - ``TrxStreamlineData`` to map
   * - ``SetReferenceImage(img)``
     - NIfTI reference image defining the output grid
   * - ``SetGroupPredicate(predicate)``
     - Select streamlines by group membership (all-of, any-of, none-of)
   * - ``SetWeightField(name)``
     - DPS field to use as per-streamline weight (empty = unweighted count)
   * - ``Update()``
     - Compute the density image
   * - ``GetOutput()``
     - ``itk::Image<float, 3>::Pointer`` track density image
