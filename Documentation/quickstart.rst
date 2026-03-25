Quick Start
===========

Prerequisites
-------------

- ITK ≥ 5.4
- CMake ≥ 3.20
- A C++17-capable compiler: GCC ≥ 12, Apple Clang ≥ 15, or MSVC 2022
- ``trx-cpp`` is vendored and fetched automatically; no separate installation is needed

Building as an ITK Remote Module
--------------------------------

To build TractographyTRX as part of ITK, enable the module when configuring
ITK:

.. code-block:: bash

   cmake -S ITK \
         -B ITK-build \
         -DModule_TractographyTRX=ON \
         -DITK_WRAP_PYTHON=OFF
   cmake --build ITK-build

This is the standard path for production use. The built module will be
available to any project that links against the resulting ITK installation.

Building Standalone
-------------------

If ITK is already installed, TractographyTRX can be built against it directly:

.. code-block:: bash

   cmake -S . -B build -DITK_DIR=/path/to/ITK-build
   cmake --build build


Verifying the Installation
--------------------------

After building, run the test suite to confirm everything works:

.. code-block:: bash

   ctest --test-dir build -V

All tests should pass. The key test is ``itkTrxReadWriteTest``, which
validates round-trip write and read-back integrity.

Hello World
-----------

The following program opens a TRX file and prints its streamline count. It
requires ITK built with TractographyTRX enabled:

.. code-block:: cpp

   #include "itkTrxFileReader.h"
   #include "itkTrxStreamlineData.h"
   #include <iostream>

   int main(int argc, char * argv[])
   {
     if (argc < 2)
     {
       std::cerr << "Usage: hello_trx <input.trx>\n";
       return EXIT_FAILURE;
     }

     auto reader = itk::TrxFileReader::New();
     reader->SetFileName(argv[1]);
     reader->Update();

     auto data = reader->GetOutput();
     std::cout << "Streamlines: " << data->GetNumberOfStreamlines() << "\n";
     std::cout << "Vertices:    " << data->GetNumberOfVertices()    << "\n";

     return EXIT_SUCCESS;
   }

Link against the TractographyTRX and ITK Common module targets in your
``CMakeLists.txt``:

.. code-block:: cmake

   find_package(ITK REQUIRED COMPONENTS ITKCommon TractographyTRX)
   itk_generate_factory_registration()

   add_executable(hello_trx hello_trx.cxx)
   target_link_libraries(hello_trx
     PRIVATE
       ITK::ITKCommonModule
       ITK::TractographyTRXModule
   )
