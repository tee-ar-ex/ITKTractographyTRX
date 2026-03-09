Command-Line Tools
==================

Four command-line tools are provided in the ``examples/`` directory and built
as part of the module. In the Pixi manuscript workflow, build them with
``pixi run -e cxx build-examples``; the binaries land in
``build/ITK-build/bin``.

``trx_info`` — Inspect a tractogram
-------------------------------------

Prints a concise summary of a TRX file: streamline and vertex counts,
coordinate dtype and system, voxel-to-RAS affine, and all DPS, DPV, and group
fields with their sizes.

.. code-block:: bash

   trx_info --input tractogram.trx

Example output::

   TRX info: hcp_1M.trx

   Streamlines: 1000000
   Vertices:    67241234
   Coord dtype: float32
   Coord space: LPS
   Dims:        [145, 174, 145]

   VoxelToRAS:
       [  1.250000   0.000000   0.000000 -90.000000]
       [  0.000000   1.250000   0.000000 -126.000000]
       [  0.000000   0.000000   1.250000 -72.000000]
       [  0.000000   0.000000   0.000000   1.000000]

   DPS fields (1):
     - sift2_weights: values=1000000 (1 col)
   DPV fields (0):
   Groups (0):

``trx_add_groups`` — Atlas-based parcellation
----------------------------------------------

Wraps ``TrxParcellationLabeler`` to assign streamlines to named TRX groups
from one or more NIfTI segmentation atlases. Each atlas is specified as a
comma-separated triple ``<nifti>,<label_txt>,<prefix>``.

.. code-block:: bash

   trx_add_groups \
     --input subj.trx \
     --in-place \
     --atlas-spec native_seg.nii.gz,native_seg.txt,Glasser \
     --dilation-radius 1

Use ``--in-place`` to append groups to the existing file rather than writing
a new one. Multiple ``--atlas-spec`` arguments parcellate against several
atlases in a single pass.

``--dilation-radius N`` expands each parcel by *N* voxels before intersection
testing, which increases overlap with streamlines that terminate slightly
outside parcel boundaries due to registration imprecision.

``trx_group_tdi`` — Track Density Image
-----------------------------------------

Maps streamlines from selected groups onto a reference NIfTI grid, producing
a voxel-count or weighted-count image.

.. code-block:: bash

   trx_group_tdi \
     --input parcellated.trx \
     --reference T1w_1mm.nii.gz \
     --output cst_tdi.nii.gz \
     --group CST_left \
     --stat sum

Group selection predicates support set-algebra queries:

- ``--groups-all-of A B`` — streamlines belonging to **all** listed groups
- ``--groups-any-of A B`` — streamlines belonging to **any** listed group
- ``--groups-none-of A B`` — streamlines belonging to **none** of the listed groups

``--weight-field <dps_name>`` produces a weighted TDI using per-streamline
scalar values (e.g. SIFT2 weights for quantitative connectivity).

``trx_extract_subset`` — Extract streamlines
---------------------------------------------

Extracts streamlines by index list or named group membership and writes a
compact TRX file. Useful as a bridge between interactive AABB queries and
downstream batch processing.

.. code-block:: bash

   trx_extract_subset \
     --input parcellated.trx \
     --output subset.trx \
     --group CST_left
