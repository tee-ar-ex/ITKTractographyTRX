TractographyTRX
===============

TractographyTRX is an ITK remote module that provides native support for the
`TRX tractography file format <https://github.com/tee-ar-ex/trx-spec>`_.
It exposes TRX files as ITK ``DataObject`` instances with lazy position
loading, axis-aligned bounding box (AABB) spatial queries, and group-aware
analysis. A streaming incremental writer supports large-scale parallel
tractogram generation. Coordinate system conversion between the TRX RAS+
convention and ITK's LPS+ convention is handled transparently.

The implementation is backed by `trx-cpp <https://github.com/tee-ar-ex/trx-cpp>`_,
which is vendored and fetched automatically at configure time.

.. toctree::
   :maxdepth: 2
   :caption: Contents

   quickstart
   user_guide
   examples
   cli
   api
   Benchmarks
