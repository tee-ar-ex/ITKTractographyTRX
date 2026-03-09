Examples
========

The following programs are provided as standalone compilable examples in the
``src/`` directory of the companion manuscript repository. Each can be built
with ``pixi run -e cxx build-src``. See ``src/CMakeLists.txt`` for the
CMake configuration.

Example 1: Reading a TRX File and Inspecting Metadata
-----------------------------------------------------

This program opens a TRX file, prints summary statistics, and lists all named
groups, DPS fields, and DPV fields. It demonstrates the basic reader pipeline
and the enumeration API on ``TrxStreamlineData``.

.. code-block:: cpp

   #include "itkTrxFileReader.h"
   #include "itkTrxStreamlineData.h"
   #include <iostream>

   int main(int argc, char * argv[])
   {
     if (argc < 2)
     {
       std::cerr << "Usage: TrxReadInfo <input.trx>\n";
       return EXIT_FAILURE;
     }

     auto reader = itk::TrxFileReader::New();
     reader->SetFileName(argv[1]);
     reader->Update();

     auto data = reader->GetOutput();

     std::cout << "Streamlines : " << data->GetNumberOfStreamlines() << "\n";
     std::cout << "Vertices    : " << data->GetNumberOfVertices()    << "\n";

     if (data->HasGroups())
     {
       for (const auto & name : data->GetGroupNames())
       {
         auto group = data->GetGroup(name);
         std::cout << "  group '" << name << "': "
                   << group->GetStreamlineIndices().size()
                   << " streamlines\n";
       }
     }

     for (const auto & f : data->GetDpsFieldNames())
       std::cout << "  DPS field: " << f << "\n";
     for (const auto & f : data->GetDpvFieldNames())
       std::cout << "  DPV field: " << f << "\n";

     return EXIT_SUCCESS;
   }

Example 2: Spatial Query with an Axis-Aligned Bounding Box
----------------------------------------------------------

``QueryAabb()`` selects streamlines whose bounding boxes intersect a region
of interest specified in LPS+ coordinates and returns the matching streamlines
as a new ``TrxStreamlineData``. The result can be written directly to a new
TRX file using ``Save()``.

.. code-block:: cpp

   #include "itkTrxFileReader.h"
   #include "itkTrxStreamlineData.h"
   #include <iostream>

   int main(int argc, char * argv[])
   {
     // argv[1] input, argv[2] output,
     // argv[3-5] min LPS corner, argv[6-8] max LPS corner
     if (argc < 9) { return EXIT_FAILURE; }

     auto reader = itk::TrxFileReader::New();
     reader->SetFileName(argv[1]);
     reader->Update();
     auto data = reader->GetOutput();

     itk::TrxStreamlineData::PointType minCorner, maxCorner;
     for (int i = 0; i < 3; ++i)
     {
       minCorner[i] = std::stod(argv[3 + i]);
       maxCorner[i] = std::stod(argv[6 + i]);
     }

     auto subset = data->QueryAabb(minCorner, maxCorner);

     std::cout << "Found " << subset->GetNumberOfStreamlines()
               << " / " << data->GetNumberOfStreamlines()
               << " streamlines in ROI\n";

     subset->Save(argv[2], /*useCompression=*/true);
     return EXIT_SUCCESS;
   }

Example 3: Streaming Writer with DPS and DPV Fields
---------------------------------------------------

``TrxStreamWriter`` is designed for pipelines that generate streamlines
incrementally. Fields must be declared before the first ``PushStreamline()``
call to ensure that all arrays in the output archive have matching lengths.

.. code-block:: cpp

   #include "itkTrxStreamWriter.h"
   #include "itkPoint.h"
   #include <vector>
   #include <iostream>

   int main(int argc, char * argv[])
   {
     if (argc < 2) { return EXIT_FAILURE; }

     using PointType = itk::Point<double, 3>;

     auto writer = itk::TrxStreamWriter::New();
     writer->SetFileName(argv[1]);
     writer->UseCompressionOn();

     // Declare fields before pushing any streamlines
     writer->RegisterDpsField("mean_fa", "float32");
     writer->RegisterDpvField("fa",      "float32");

     for (int s = 0; s < 10; ++s)
     {
       std::vector<PointType> pts;
       std::vector<double>    faPerVertex;

       for (int v = 0; v < 50; ++v)
       {
         PointType p;
         p[0] = static_cast<double>(v);
         p[1] = static_cast<double>(s);
         p[2] = 0.0;
         pts.push_back(p);
         faPerVertex.push_back(0.6 + 0.004 * v);
       }

       writer->PushStreamline(pts,
         /*dps=*/{ { "mean_fa", 0.65 } },
         /*dpv=*/{ { "fa", faPerVertex } });
     }

     writer->Finalize();
     std::cout << "Wrote " << argv[1] << "\n";
     return EXIT_SUCCESS;
   }

Example 4: Working with Named Groups
------------------------------------

Groups represent named anatomical bundles. Group membership is loaded lazily;
``data->GetGroup("CST_left")`` caches the index list on first access.
Underlying streamline positions are fetched only when ``GetStreamlines()`` is
called.

.. code-block:: cpp

   #include "itkTrxFileReader.h"
   #include "itkTrxStreamlineData.h"
   #include <iostream>

   int main(int argc, char * argv[])
   {
     // argv[1] input, argv[2] output bundle file, argv[3] group name
     if (argc < 4) { return EXIT_FAILURE; }

     auto reader = itk::TrxFileReader::New();
     reader->SetFileName(argv[1]);
     reader->Update();
     auto data = reader->GetOutput();

     auto group = data->GetGroup(argv[3]);
     if (!group)
     {
       std::cerr << "Group '" << argv[3] << "' not found\n";
       return EXIT_FAILURE;
     }

     std::cout << argv[3] << ": "
               << group->GetStreamlineIndices().size()
               << " streamlines\n";

     // Lazy subset — no position data is copied
     auto bundle = group->GetStreamlines(data.GetPointer());

     // Iterate point-by-point in LPS+ (default)
     for (itk::SizeValueType i = 0; i < bundle->GetNumberOfStreamlines(); ++i)
     {
       for (const auto & pt : bundle->GetStreamlineRange(i))
       {
         (void)pt; // use pt[0], pt[1], pt[2] ...
       }
     }

     bundle->Save(argv[2], /*useCompression=*/true);
     return EXIT_SUCCESS;
   }
