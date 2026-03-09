TractographyTRX
===============

TractographyTRX is an ITK remote module that provides native support for the
`TRX tractography file format <https://github.com/tee-ar-ex/trx-spec>`_.
It exposes TRX files as ITK ``DataObject``s with efficient access to 
streamlines coordinates, scalar data, and named groups.

The implementation is backed by `trx-cpp <https://github.com/tee-ar-ex/trx-cpp>`_,
which is vendored and fetched automatically at configure time.

Why ITK and TRX?
----------------

At their core, streamlines are just 3D points. ITK specializes in 
spatial objects, and their associated transforms and processing.
`TracrographyTRX` brings the highly-efficient TRX format to ITK
with very little overhead (see :ref:`benchmarks`). 
Transform your streamlines along with your images all in one place!


.. toctree::
   :maxdepth: 2
   :caption: Contents

   quickstart
   user_guide
   examples
   cli
   api
   Benchmarks
