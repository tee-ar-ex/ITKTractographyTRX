/*=========================================================================
 *
 *  Copyright NumFOCUS
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

/**
 * \file itkTrxCoverageTest.cxx
 *
 * Covers code paths left unexercised by the existing test suite:
 *
 *  1. TrxStreamWriter::PushStreamline(vnl_matrix<double>)
 *     and FlattenPointsToRas(vnl_matrix) — the vnl overload.
 *  2. TrxStreamlineData::TransformToWriterChunkedReuseVnlBuffer
 *  3. TrxStreamWriter error paths (finalize-twice, push-after-finalize,
 *     empty field names, empty filename, DPS/DPV validation failures).
 *  4. TrxFileReader error paths (empty filename, missing file).
 *  5. TrxFileWriter round-trip and error paths.
 *  6. TrxStreamlineData: GetStreamline(), GetStreamlineView(),
 *     HasFloat16/64Positions(), GetCoordinateType(), FlipXYInPlace(),
 *     SubsetStreamlinesLazy(), ForEachStreamlineChunked(), Save(),
 *     InvalidateAabbCache(), GetGroupStreamlineCount(),
 *     SetCoordinateSystem()/GetCoordinateSystem().
 *  7. TrxGroupTdiMapper: DPS weight field, static Compute() helpers.
 *  8. TrxParcellationLabeler: morphological dilation, multiple atlases,
 *     SetInputFileName copy-through mode.
 */

#include "itkTrxFileReader.h"
#include "itkTrxFileWriter.h"
#include "itkTrxGroupTdiMapper.h"
#include "itkTrxParcellationLabeler.h"
#include "itkTrxStreamWriter.h"
#include "itkTrxStreamlineData.h"

#include "itkImage.h"
#include "itkImageFileWriter.h"
#include "itkNiftiImageIO.h"
#include "itkTranslationTransform.h"

#include "itksys/SystemTools.hxx"
#include "vnl/vnl_matrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Common utilities
// ---------------------------------------------------------------------------

void
Cleanup(const std::string & path)
{
  if (itksys::SystemTools::FileIsDirectory(path))
  {
    itksys::SystemTools::RemoveADirectory(path);
    return;
  }
  if (itksys::SystemTools::FileExists(path, true))
  {
    itksys::SystemTools::RemoveFile(path);
  }
}

itk::TrxStreamWriter::MatrixType
MakeIdentityRas()
{
  itk::TrxStreamWriter::MatrixType m;
  m.SetIdentity();
  return m;
}

itk::TrxStreamWriter::DimensionsType
MakeDims(uint16_t n = 20)
{
  itk::TrxStreamWriter::DimensionsType d;
  d[0] = d[1] = d[2] = n;
  return d;
}

itk::TrxStreamWriter::StreamlineType
MakeSL(int base, int n)
{
  itk::TrxStreamWriter::StreamlineType sl;
  sl.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    itk::Point<double, 3> p;
    p[0] = base + i;
    p[1] = base + i;
    p[2] = base + i;
    sl.push_back(p);
  }
  return sl;
}

/** Write a 20x20x20 1mm LPS identity reference image usable by TDI mapper. */
bool
WriteReferenceNifti(const std::string & path)
{
  using ImageType = itk::Image<float, 3>;
  ImageType::IndexType start;
  start.Fill(0);
  ImageType::SizeType size;
  size.Fill(20);
  auto image = ImageType::New();
  image->SetRegions(ImageType::RegionType(start, size));
  image->Allocate(true);
  ImageType::SpacingType sp;
  sp.Fill(1.0);
  image->SetSpacing(sp);
  ImageType::PointType origin;
  origin.Fill(0.0);
  image->SetOrigin(origin);
  ImageType::DirectionType dir;
  dir.SetIdentity();
  image->SetDirection(dir);

  auto io = itk::NiftiImageIO::New();
  auto writer = itk::ImageFileWriter<ImageType>::New();
  writer->SetImageIO(io);
  writer->SetFileName(path);
  writer->SetInput(image);
  try
  {
    writer->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteReferenceNifti: " << e << "\n";
    return false;
  }
  return true;
}

/** Write a synthetic 20x20x20 1mm LPS label image.
 *  Region 1: voxels [2,5]^3.  Region 2: voxels [12,15]^3. */
bool
WriteParcellationNifti(const std::string & path)
{
  using LabelImage = itk::Image<int32_t, 3>;
  LabelImage::IndexType start;
  start.Fill(0);
  LabelImage::SizeType size;
  size.Fill(20);
  auto image = LabelImage::New();
  image->SetRegions(LabelImage::RegionType(start, size));
  image->Allocate(true);
  LabelImage::SpacingType sp;
  sp.Fill(1.0);
  image->SetSpacing(sp);
  LabelImage::PointType origin;
  origin.Fill(0.0);
  image->SetOrigin(origin);
  LabelImage::DirectionType dir;
  dir.SetIdentity();
  image->SetDirection(dir);
  for (int k = 2; k <= 5; ++k)
    for (int j = 2; j <= 5; ++j)
      for (int i = 2; i <= 5; ++i)
        image->SetPixel({ { i, j, k } }, 1);
  for (int k = 12; k <= 15; ++k)
    for (int j = 12; j <= 15; ++j)
      for (int i = 12; i <= 15; ++i)
        image->SetPixel({ { i, j, k } }, 2);

  auto io = itk::NiftiImageIO::New();
  auto writer = itk::ImageFileWriter<LabelImage>::New();
  writer->SetImageIO(io);
  writer->SetFileName(path);
  writer->SetInput(image);
  try
  {
    writer->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteParcellationNifti: " << e << "\n";
    return false;
  }
  return true;
}

bool
WriteLabelFileTwoRegions(const std::string & path)
{
  std::ofstream out(path);
  if (!out.is_open())
  {
    return false;
  }
  out << "1\tRegion_A\n";
  out << "2\tRegion_B\n";
  return true;
}

// ---------------------------------------------------------------------------
// 1. vnl_matrix overload
// ---------------------------------------------------------------------------

bool
TestVnlMatrixWriter(const std::string & basePath)
{
  const std::string outPath = basePath + "_vnl.trx";
  Cleanup(outPath);
  struct Guard
  {
    std::string p;
    ~Guard() { Cleanup(p); }
  } g{ outPath };

  // Build a 3-point streamline as an N×3 vnl_matrix (LPS coords)
  vnl_matrix<double> mat(3, 3);
  mat(0, 0) = 1.0;
  mat(0, 1) = 2.0;
  mat(0, 2) = 3.0;
  mat(1, 0) = 4.0;
  mat(1, 1) = 5.0;
  mat(1, 2) = 6.0;
  mat(2, 0) = 7.0;
  mat(2, 1) = 8.0;
  mat(2, 2) = 9.0;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outPath);
  writer->SetUseCompression(true);
  writer->SetVoxelToRasMatrix(MakeIdentityRas());
  writer->SetDimensions(MakeDims());
  writer->PushStreamline(mat);
  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[VnlMatrixWriter] null output\n";
    return false;
  }
  if (data->GetNumberOfStreamlines() != 1 || data->GetNumberOfVertices() != 3)
  {
    std::cerr << "[VnlMatrixWriter] unexpected counts: sl=" << data->GetNumberOfStreamlines()
              << " v=" << data->GetNumberOfVertices() << "\n";
    return false;
  }

  // Verify that the points round-tripped correctly (LPS->RAS->LPS).
  const auto range = data->GetStreamlineRange(0);
  std::vector<itk::Point<double, 3>> pts;
  for (const auto & p : range)
    pts.push_back(p);

  if (pts.size() != 3)
  {
    std::cerr << "[VnlMatrixWriter] wrong point count: " << pts.size() << "\n";
    return false;
  }

  constexpr double tol = 1e-4;
  for (size_t i = 0; i < 3; ++i)
  {
    for (unsigned int dim = 0; dim < 3; ++dim)
    {
      if (std::abs(pts[i][dim] - mat(static_cast<unsigned int>(i), dim)) > tol)
      {
        std::cerr << "[VnlMatrixWriter] coord mismatch at pt " << i << " dim " << dim << "\n";
        return false;
      }
    }
  }

  // Also exercise the "bad matrix" error path (wrong column count).
  {
    vnl_matrix<double> bad(2, 2);
    bool threw = false;
    try
    {
      writer->PushStreamline(bad);
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "[VnlMatrixWriter] expected exception for non-3-column matrix\n";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// 2. TransformToWriterChunkedReuseVnlBuffer
// ---------------------------------------------------------------------------

bool
TestTransformVnlBuffer(const std::string & basePath)
{
  // Write a source TRX
  const std::string srcPath = basePath + "_vnlbuf_src.trx";
  const std::string dstPath = basePath + "_vnlbuf_dst.trx";
  Cleanup(srcPath);
  Cleanup(dstPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { srcPath, dstPath } };

  {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(srcPath);
    writer->SetUseCompression(true);
    writer->SetVoxelToRasMatrix(MakeIdentityRas());
    writer->SetDimensions(MakeDims());
    writer->PushStreamline(MakeSL(0, 3));
    writer->PushStreamline(MakeSL(10, 2));
    writer->Finalize();
  }

  // Read back and stream through TransformToWriterChunkedReuseVnlBuffer
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(srcPath);
  reader->Update();
  auto src = reader->GetOutput();
  if (!src)
  {
    std::cerr << "[TransformVnlBuffer] failed to read source\n";
    return false;
  }

  using TransformType = itk::TranslationTransform<double, 3>;
  auto transform = TransformType::New();
  TransformType::OutputVectorType offset;
  offset[0] = 0.0;
  offset[1] = 0.0;
  offset[2] = 0.0;
  transform->Translate(offset); // identity translation

  auto dstWriter = itk::TrxStreamWriter::New();
  dstWriter->SetFileName(dstPath);
  dstWriter->SetUseCompression(true);
  dstWriter->SetVoxelToRasMatrix(MakeIdentityRas());
  dstWriter->SetDimensions(MakeDims());

  vnl_matrix<double> buffer;
  src->TransformToWriterChunkedReuseVnlBuffer(transform.GetPointer(), dstWriter.GetPointer(), buffer);
  dstWriter->Finalize();

  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(dstPath);
  reader2->Update();
  auto dst = reader2->GetOutput();
  if (!dst)
  {
    std::cerr << "[TransformVnlBuffer] failed to read destination\n";
    return false;
  }
  if (dst->GetNumberOfStreamlines() != 2 || dst->GetNumberOfVertices() != 5)
  {
    std::cerr << "[TransformVnlBuffer] unexpected counts: sl=" << dst->GetNumberOfStreamlines()
              << " v=" << dst->GetNumberOfVertices() << "\n";
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// 3. Writer error paths
// ---------------------------------------------------------------------------

bool
TestWriterErrorPaths(const std::string & basePath)
{
  const std::string outPath = basePath + "_errpath.trx";

  // Helper: expect an exception from a lambda, return false if no throw.
  auto Expect = [](const char * label, auto fn) -> bool {
    bool threw = false;
    try
    {
      fn();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "[WriterErrors] expected exception for: " << label << "\n";
      return false;
    }
    return true;
  };

  // 3a. RegisterDpsField with empty name
  {
    auto w = itk::TrxStreamWriter::New();
    if (!Expect("RegisterDpsField empty name", [&] { w->RegisterDpsField("", "float32"); }))
      return false;
  }

  // 3b. RegisterDpvField with empty name
  {
    auto w = itk::TrxStreamWriter::New();
    if (!Expect("RegisterDpvField empty name", [&] { w->RegisterDpvField("", "float32"); }))
      return false;
  }

  // 3c. Finalize with empty filename
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->PushStreamline(MakeSL(0, 2));
    if (!Expect("Finalize empty filename", [&] { w->Finalize(); }))
      return false;
  }

  // 3d. Finalize called twice
  {
    Cleanup(outPath);
    struct Guard
    {
      std::string p;
      ~Guard() { Cleanup(p); }
    } g{ outPath };
    auto w = itk::TrxStreamWriter::New();
    w->SetFileName(outPath);
    w->SetUseCompression(true);
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->PushStreamline(MakeSL(0, 2));
    w->Finalize();
    if (!Expect("Finalize twice", [&] { w->Finalize(); }))
      return false;
  }

  // 3e. PushStreamline after Finalize
  {
    Cleanup(outPath);
    struct Guard
    {
      std::string p;
      ~Guard() { Cleanup(p); }
    } g{ outPath };
    auto w = itk::TrxStreamWriter::New();
    w->SetFileName(outPath);
    w->SetUseCompression(true);
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->PushStreamline(MakeSL(0, 2));
    w->Finalize();
    if (!Expect("PushStreamline after Finalize", [&] { w->PushStreamline(MakeSL(5, 2)); }))
      return false;
  }

  // 3f. DPS values provided without any field registered
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    if (!Expect("DPS values without registration", [&] {
          w->PushStreamline(MakeSL(0, 2), { { "weight", 1.0 } }, {}, {});
        }))
      return false;
  }

  // 3g. DPS registered but value missing (size mismatch: 0 provided vs 1 expected)
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->RegisterDpsField("FA", "float32");
    if (!Expect("DPS missing value (size mismatch)", [&] {
          w->PushStreamline(MakeSL(0, 2), {}, {}, {});
        }))
      return false;
  }

  // 3h. DPS registered as "FA" but wrong key "weight" provided (size match, key mismatch)
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->RegisterDpsField("FA", "float32");
    if (!Expect("DPS wrong key", [&] {
          w->PushStreamline(MakeSL(0, 2), { { "weight", 1.0 } }, {}, {});
        }))
      return false;
  }

  // 3i. DPV values provided without any field registered
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    if (!Expect("DPV values without registration", [&] {
          w->PushStreamline(MakeSL(0, 2), {}, { { "sig", { 0.1, 0.2 } } }, {});
        }))
      return false;
  }

  // 3j. DPV registered but vector length != point count
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->RegisterDpvField("sig", "float32");
    if (!Expect("DPV size mismatch", [&] {
          // streamline has 2 points but only 1 DPV value
          w->PushStreamline(MakeSL(0, 2), {}, { { "sig", { 0.1 } } }, {});
        }))
      return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// 4. Reader error paths
// ---------------------------------------------------------------------------

bool
TestReaderErrorPaths()
{
  auto Expect = [](const char * label, auto fn) -> bool {
    bool threw = false;
    try
    {
      fn();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "[ReaderErrors] expected exception for: " << label << "\n";
      return false;
    }
    return true;
  };

  // 4a. Empty filename
  if (!Expect("Reader empty filename", [] {
        auto r = itk::TrxFileReader::New();
        r->Update(); // m_FileName is empty
      }))
    return false;

  // 4b. Non-existent file
  if (!Expect("Reader non-existent file", [] {
        auto r = itk::TrxFileReader::New();
        r->SetFileName("/nonexistent/path/to/file.trx");
        r->Update();
      }))
    return false;

  return true;
}

// ---------------------------------------------------------------------------
// 5. TrxFileWriter
// ---------------------------------------------------------------------------

bool
TestFileWriter(const std::string & basePath)
{
  const std::string srcPath = basePath + "_fw_src.trx";
  const std::string dstPath = basePath + "_fw_dst.trx";
  Cleanup(srcPath);
  Cleanup(dstPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { srcPath, dstPath } };

  // Write a source TRX via TrxStreamWriter.
  {
    auto w = itk::TrxStreamWriter::New();
    w->SetFileName(srcPath);
    w->SetUseCompression(true);
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->PushStreamline(MakeSL(0, 3));
    w->PushStreamline(MakeSL(10, 2));
    w->Finalize();
  }

  // Read it back.
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(srcPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[FileWriter] failed to read source\n";
    return false;
  }

  // Re-write using TrxFileWriter.
  auto fileWriter = itk::TrxFileWriter::New();
  fileWriter->SetFileName(dstPath);
  fileWriter->SetInput(data);
  fileWriter->SetUseCompression(true);
  fileWriter->Update();

  // Read back the re-written file.
  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(dstPath);
  reader2->Update();
  auto data2 = reader2->GetOutput();
  if (!data2)
  {
    std::cerr << "[FileWriter] failed to read destination\n";
    return false;
  }
  if (data2->GetNumberOfStreamlines() != 2 || data2->GetNumberOfVertices() != 5)
  {
    std::cerr << "[FileWriter] count mismatch: sl=" << data2->GetNumberOfStreamlines()
              << " v=" << data2->GetNumberOfVertices() << "\n";
    return false;
  }

  // Error path: empty filename
  {
    auto w2 = itk::TrxFileWriter::New();
    w2->SetInput(data);
    bool threw = false;
    try
    {
      w2->Update();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "[FileWriter] expected exception for empty filename\n";
      return false;
    }
  }

  // Error path: null input
  {
    auto w3 = itk::TrxFileWriter::New();
    w3->SetFileName(dstPath);
    bool threw = false;
    try
    {
      w3->Update();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "[FileWriter] expected exception for null input\n";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// 6. TrxStreamlineData accessor methods
// ---------------------------------------------------------------------------

bool
TestStreamlineDataMethods(const std::string & basePath)
{
  // Write a TRX with 2 groups, 4 streamlines.
  const std::string srcPath = basePath + "_sdm_src.trx";
  const std::string savePath = basePath + "_sdm_save.trx";
  Cleanup(srcPath);
  Cleanup(savePath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { srcPath, savePath } };

  {
    auto w = itk::TrxStreamWriter::New();
    w->SetFileName(srcPath);
    w->SetUseCompression(true);
    w->SetVoxelToRasMatrix(MakeIdentityRas());
    w->SetDimensions(MakeDims());
    w->PushStreamline(MakeSL(0, 3), {}, {}, { "GroupA" });
    w->PushStreamline(MakeSL(10, 2), {}, {}, { "GroupB" });
    w->PushStreamline(MakeSL(20, 4), {}, {}, { "GroupA" });
    w->PushStreamline(MakeSL(30, 2), {}, {}, { "GroupB" });
    w->Finalize();
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(srcPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[SDMethods] failed to read\n";
    return false;
  }

  // GetCoordinateType — always Float32 for our writer.
  if (data->GetCoordinateType() != itk::TrxStreamlineData::CoordinateType::Float32)
  {
    std::cerr << "[SDMethods] unexpected coordinate type\n";
    return false;
  }

  // HasFloat16Positions / GetFloat16Positions — always false/null for Float32 data.
  if (data->HasFloat16Positions())
  {
    std::cerr << "[SDMethods] HasFloat16Positions should be false\n";
    return false;
  }
  if (data->GetFloat16Positions() != nullptr)
  {
    std::cerr << "[SDMethods] GetFloat16Positions should return null\n";
    return false;
  }

  // HasFloat64Positions / GetFloat64Positions — always false/null for Float32 data.
  if (data->HasFloat64Positions())
  {
    std::cerr << "[SDMethods] HasFloat64Positions should be false\n";
    return false;
  }
  if (data->GetFloat64Positions() != nullptr)
  {
    std::cerr << "[SDMethods] GetFloat64Positions should return null\n";
    return false;
  }

  // GetStreamline() — compare against GetStreamlineRange().
  for (itk::SizeValueType i = 0; i < data->GetNumberOfStreamlines(); ++i)
  {
    const auto vec = data->GetStreamline(i);
    std::vector<itk::Point<double, 3>> rangeVec;
    for (const auto & p : data->GetStreamlineRange(i))
      rangeVec.push_back(p);

    if (vec.size() != rangeVec.size())
    {
      std::cerr << "[SDMethods] GetStreamline/Range size mismatch at sl " << i << "\n";
      return false;
    }
    for (size_t k = 0; k < vec.size(); ++k)
    {
      for (unsigned int d = 0; d < 3; ++d)
      {
        if (std::abs(vec[k][d] - rangeVec[k][d]) > 1e-6)
        {
          std::cerr << "[SDMethods] GetStreamline coord mismatch at sl " << i << " pt " << k << "\n";
          return false;
        }
      }
    }
  }

  // GetStreamlineView() — verify pointCount.
  {
    itk::TrxStreamlineData::StreamlineView view;
    const bool ok = data->GetStreamlineView(0, view);
    if (!ok)
    {
      std::cerr << "[SDMethods] GetStreamlineView returned false\n";
      return false;
    }
    if (view.pointCount != 3)
    {
      std::cerr << "[SDMethods] GetStreamlineView pointCount wrong: " << view.pointCount << "\n";
      return false;
    }
    if (view.xyz == nullptr)
    {
      std::cerr << "[SDMethods] GetStreamlineView xyz is null\n";
      return false;
    }
  }

  // GetGroupStreamlineCount()
  if (data->GetGroupStreamlineCount("GroupA") != 2)
  {
    std::cerr << "[SDMethods] GroupA count wrong: " << data->GetGroupStreamlineCount("GroupA") << "\n";
    return false;
  }
  if (data->GetGroupStreamlineCount("GroupB") != 2)
  {
    std::cerr << "[SDMethods] GroupB count wrong: " << data->GetGroupStreamlineCount("GroupB") << "\n";
    return false;
  }
  if (data->GetGroupStreamlineCount("NoSuchGroup") != 0)
  {
    std::cerr << "[SDMethods] non-existent group count should be 0\n";
    return false;
  }

  // SetCoordinateSystem / GetCoordinateSystem
  data->SetCoordinateSystem(itk::TrxStreamlineData::CoordinateSystem::LPS);
  if (data->GetCoordinateSystem() != itk::TrxStreamlineData::CoordinateSystem::LPS)
  {
    std::cerr << "[SDMethods] GetCoordinateSystem mismatch\n";
    return false;
  }
  data->SetCoordinateSystem(itk::TrxStreamlineData::CoordinateSystem::RAS);

  // SubsetStreamlinesLazy() — result should match SubsetStreamlines() in count.
  {
    auto lazy = data->SubsetStreamlinesLazy({ 1, 3 });
    if (!lazy || lazy->GetNumberOfStreamlines() != 2)
    {
      std::cerr << "[SDMethods] SubsetStreamlinesLazy count wrong\n";
      return false;
    }
    auto eager = data->SubsetStreamlines({ 1, 3 });
    if (!eager || eager->GetNumberOfStreamlines() != 2)
    {
      std::cerr << "[SDMethods] SubsetStreamlines count wrong\n";
      return false;
    }
    if (lazy->GetNumberOfVertices() != eager->GetNumberOfVertices())
    {
      std::cerr << "[SDMethods] lazy vs eager vertex count mismatch\n";
      return false;
    }
  }

  // ForEachStreamlineChunked() — count total vertices directly.
  {
    itk::SizeValueType totalVerts = 0;
    data->ForEachStreamlineChunked(
      [&](itk::SizeValueType,
          const void *,
          itk::SizeValueType count,
          itk::TrxStreamlineData::CoordinateType,
          itk::TrxStreamlineData::CoordinateSystem) { totalVerts += count; },
      itk::TrxStreamlineData::CoordinateSystem::LPS);
    if (totalVerts != data->GetNumberOfVertices())
    {
      std::cerr << "[SDMethods] ForEachStreamlineChunked vertex sum mismatch: "
                << totalVerts << " vs " << data->GetNumberOfVertices() << "\n";
      return false;
    }

    // Also exercise the RAS code path.
    itk::SizeValueType totalRas = 0;
    data->ForEachStreamlineChunked(
      [&](itk::SizeValueType,
          const void *,
          itk::SizeValueType count,
          itk::TrxStreamlineData::CoordinateType,
          itk::TrxStreamlineData::CoordinateSystem) { totalRas += count; },
      itk::TrxStreamlineData::CoordinateSystem::RAS);
    if (totalRas != data->GetNumberOfVertices())
    {
      std::cerr << "[SDMethods] ForEachStreamlineChunked RAS vertex sum mismatch\n";
      return false;
    }
  }

  // InvalidateAabbCache() — build, invalidate, rebuild.
  {
    const auto & aabbs1 = data->GetOrBuildStreamlineAabbs();
    if (aabbs1.empty())
    {
      std::cerr << "[SDMethods] AABB cache empty\n";
      return false;
    }
    data->InvalidateAabbCache();
    const auto & aabbs2 = data->GetOrBuildStreamlineAabbs();
    if (aabbs2.size() != aabbs1.size())
    {
      std::cerr << "[SDMethods] AABB size changed after invalidate/rebuild\n";
      return false;
    }
  }

  // FlipXYInPlace() — two flips should restore original positions.
  {
    const auto pts_before = data->GetStreamline(0);
    data->FlipXYInPlace();
    data->FlipXYInPlace();
    const auto pts_after = data->GetStreamline(0);
    for (size_t k = 0; k < pts_before.size(); ++k)
    {
      for (unsigned int d = 0; d < 3; ++d)
      {
        if (std::abs(pts_before[k][d] - pts_after[k][d]) > 1e-4)
        {
          std::cerr << "[SDMethods] FlipXYInPlace double-flip changed coords at k=" << k << " d=" << d << "\n";
          return false;
        }
      }
    }
  }

  // Save() — write to new path, read back, verify counts.
  {
    data->Save(savePath, true);
    auto reader2 = itk::TrxFileReader::New();
    reader2->SetFileName(savePath);
    reader2->Update();
    auto data2 = reader2->GetOutput();
    if (!data2 || data2->GetNumberOfStreamlines() != 4 || data2->GetNumberOfVertices() != 11)
    {
      std::cerr << "[SDMethods] Save/reload count mismatch\n";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// 7. TrxGroupTdiMapper: DPS weight field and static Compute()
// ---------------------------------------------------------------------------

/** Write a TRX with 3 streamlines, DPS "weight" field, and "WeightGroup"
 *  containing streamlines 0 and 2. Coordinates match a 20x20x20 1mm
 *  identity LPS reference. */
bool
WriteWeightedTrx(const std::string & trxPath)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;

  itk::TrxStreamWriter::MatrixType voxelToLps;
  voxelToLps.SetIdentity();
  itk::TrxStreamWriter::DimensionsType dims;
  dims[0] = dims[1] = dims[2] = 20;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(trxPath);
  writer->SetVoxelToLpsMatrix(voxelToLps);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("weight", "float32");

  // SL 0 (in group): voxels (3,3,3) twice, voxel (4,4,4) once. Weight = 2.0
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.1, 3.1, 3.1 } });
    sl.push_back(PointType{ { 3.4, 3.4, 3.4 } });
    sl.push_back(PointType{ { 4.0, 4.0, 4.0 } });
    writer->PushStreamline(sl, { { "weight", 2.0 } }, {}, { "WeightGroup" });
  }
  // SL 1 (not in group): voxel (3,3,3). Weight = 5.0 (should be ignored).
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.2, 3.2, 3.2 } });
    writer->PushStreamline(sl, { { "weight", 5.0 } }, {}, {});
  }
  // SL 2 (in group): voxel (3,3,3), then out-of-bounds. Weight = 3.0
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    sl.push_back(PointType{ { 1000.0, 1000.0, 1000.0 } });
    writer->PushStreamline(sl, { { "weight", 3.0 } }, {}, { "WeightGroup" });
  }
  try
  {
    writer->Finalize();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteWeightedTrx: " << e << "\n";
    return false;
  }
  return true;
}

bool
TestTdiWeightedField(const std::string & basePath)
{
  const std::string trxPath = basePath + "_tdi_wt.trx";
  const std::string refPath = basePath + "_tdi_ref.nii.gz";
  Cleanup(trxPath);
  Cleanup(refPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { trxPath, refPath } };

  if (!WriteWeightedTrx(trxPath))
    return false;
  if (!WriteReferenceNifti(refPath))
    return false;

  // Sum mode with weight field.
  {
    auto mapper = itk::TrxGroupTdiMapper::New();
    mapper->SetInputFileName(trxPath);
    mapper->SetGroupName("WeightGroup");
    mapper->SetReferenceImageFileName(refPath);
    itk::TrxGroupTdiMapper::Options opts;
    opts.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
    opts.weightField = "weight";
    mapper->SetOptions(opts);
    mapper->Update();

    const auto * out = mapper->GetOutput();
    if (!out || !out->GetBufferPointer())
    {
      std::cerr << "[TdiWeighted] null output\n";
      return false;
    }

    const auto   sz = out->GetLargestPossibleRegion().GetSize();
    const size_t nx = sz[0];
    const size_t ny = sz[1];
    const auto * buf = out->GetBufferPointer();

    auto flat = [&](size_t i, size_t j, size_t k) { return i + nx * (j + ny * k); };

    constexpr float tol = 1e-5f;
    const float     v333 = buf[flat(3, 3, 3)];
    const float     v444 = buf[flat(4, 4, 4)];
    // SL0 (w=2) and SL2 (w=3) both touch voxel (3,3,3) → sum = 5.0
    if (std::abs(v333 - 5.0f) > tol)
    {
      std::cerr << "[TdiWeighted] v333 mismatch: " << v333 << " (expected 5.0)\n";
      return false;
    }
    // Only SL0 (w=2) touches voxel (4,4,4) → sum = 2.0
    if (std::abs(v444 - 2.0f) > tol)
    {
      std::cerr << "[TdiWeighted] v444 mismatch: " << v444 << " (expected 2.0)\n";
      return false;
    }
  }

  return true;
}

bool
TestTdiStaticCompute(const std::string & basePath)
{
  const std::string trxPath = basePath + "_tdi_sc.trx";
  const std::string refPath = basePath + "_tdi_sc_ref.nii.gz";
  Cleanup(trxPath);
  Cleanup(refPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { trxPath, refPath } };

  if (!WriteWeightedTrx(trxPath))
    return false;
  if (!WriteReferenceNifti(refPath))
    return false;

  // Exercise both static Compute() overloads.
  {
    auto img1 = itk::TrxGroupTdiMapper::Compute(trxPath, "WeightGroup", refPath);
    if (!img1)
    {
      std::cerr << "[TdiStaticCompute] Compute() (no options) returned null\n";
      return false;
    }
  }
  {
    itk::TrxGroupTdiMapper::Options opts;
    opts.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Mean;
    auto img2 = itk::TrxGroupTdiMapper::Compute(trxPath, "WeightGroup", refPath, opts);
    if (!img2)
    {
      std::cerr << "[TdiStaticCompute] Compute() (with options) returned null\n";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// 8. TrxParcellationLabeler: dilation
// ---------------------------------------------------------------------------

/** Write a TRX with 4 streamlines for parcellation tests.
 *  SL 0: touches region A only (point at LPS [3,3,3])
 *  SL 1: touches region B only (point at LPS [13,13,13])
 *  SL 2: touches both regions
 *  SL 3: touches neither region, but point at LPS [6,3,3] is 1 voxel
 *         outside region A boundary (voxel 6,3,3 adjacent to region A
 *         voxel 5,3,3). Should be captured with dilation radius=1.
 */
bool
WriteTrxForParcellation(const std::string & trxPath)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;

  itk::TrxStreamWriter::MatrixType voxelToLps;
  voxelToLps.SetIdentity();
  itk::TrxStreamWriter::DimensionsType dims;
  dims[0] = dims[1] = dims[2] = 20;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(trxPath);
  writer->SetVoxelToLpsMatrix(voxelToLps);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("weight", "float32");

  // SL 0 — region A
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 0.5, 0.5, 0.5 } });
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    sl.push_back(PointType{ { 6.5, 6.5, 6.5 } });
    writer->PushStreamline(sl, { { "weight", 0.1 } }, {}, {});
  }
  // SL 1 — region B
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 9.0, 9.0, 9.0 } });
    sl.push_back(PointType{ { 13.0, 13.0, 13.0 } });
    sl.push_back(PointType{ { 17.0, 17.0, 17.0 } });
    writer->PushStreamline(sl, { { "weight", 0.2 } }, {}, {});
  }
  // SL 2 — both regions
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    sl.push_back(PointType{ { 9.0, 9.0, 9.0 } });
    sl.push_back(PointType{ { 13.0, 13.0, 13.0 } });
    writer->PushStreamline(sl, { { "weight", 0.3 } }, {}, {});
  }
  // SL 3 — just outside region A (voxel 6,3,3), captured only with dilation=1
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 6.0, 3.0, 3.0 } });
    writer->PushStreamline(sl, { { "weight", 0.4 } }, {}, {});
  }
  try
  {
    writer->Finalize();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteTrxForParcellation: " << e << "\n";
    return false;
  }
  return true;
}

bool
TestParcellationDilation(const std::string & basePath)
{
  const std::string niftiPath = basePath + "_dil_parc.nii.gz";
  const std::string labelPath = basePath + "_dil_labels.txt";
  const std::string trxPath = basePath + "_dil_input.trx";
  const std::string outPath = basePath + "_dil_output.trx";
  Cleanup(niftiPath);
  Cleanup(labelPath);
  Cleanup(trxPath);
  Cleanup(outPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { niftiPath, labelPath, trxPath, outPath } };

  if (!WriteParcellationNifti(niftiPath))
    return false;
  if (!WriteLabelFileTwoRegions(labelPath))
    return false;
  if (!WriteTrxForParcellation(trxPath))
    return false;

  // Read input TRX.
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxPath);
  reader->Update();
  auto inputData = reader->GetOutput();

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "Dil";

  auto labeler = itk::TrxParcellationLabeler::New();
  labeler->SetInput(inputData);
  labeler->SetOutputFileName(outPath);
  labeler->AddParcellation(spec);
  labeler->SetDilationRadius(1); // exercise the dilation code path

  try
  {
    labeler->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "[ParcellationDilation] Update threw: " << e << "\n";
    return false;
  }

  // Read result and verify expected groups are present.
  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(outPath);
  reader2->Update();
  auto result = reader2->GetOutput();
  if (!result)
  {
    std::cerr << "[ParcellationDilation] null output\n";
    return false;
  }
  const auto names = result->GetGroupNames();
  const bool hasA =
    std::find(names.begin(), names.end(), "Dil_Region_A") != names.end();
  const bool hasB =
    std::find(names.begin(), names.end(), "Dil_Region_B") != names.end();
  if (!hasA || !hasB)
  {
    std::cerr << "[ParcellationDilation] expected groups Dil_Region_A and Dil_Region_B\n";
    return false;
  }

  // With dilation=1, SL 3 (point at voxel 6,3,3) should be in Dil_Region_A.
  auto groupA = result->GetGroup("Dil_Region_A");
  if (!groupA)
  {
    std::cerr << "[ParcellationDilation] Dil_Region_A group is null\n";
    return false;
  }
  const auto & idxA = groupA->GetStreamlineIndices();
  // SL 0 (region A) and SL 2 (both) must be present.
  const bool hasSl0 = std::find(idxA.begin(), idxA.end(), 0u) != idxA.end();
  const bool hasSl2 = std::find(idxA.begin(), idxA.end(), 2u) != idxA.end();
  if (!hasSl0 || !hasSl2)
  {
    std::cerr << "[ParcellationDilation] Dil_Region_A missing expected base streamlines\n";
    return false;
  }

  // With dilation SL 3 should also be in region A.
  const bool hasSl3 = std::find(idxA.begin(), idxA.end(), 3u) != idxA.end();
  if (!hasSl3)
  {
    std::cerr << "[ParcellationDilation] SL3 not captured by dilation into Dil_Region_A\n";
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// 9. TrxParcellationLabeler: multiple atlases
// ---------------------------------------------------------------------------

bool
TestParcellationMultiAtlas(const std::string & basePath)
{
  const std::string niftiPath = basePath + "_ma_parc.nii.gz";
  const std::string labelPath = basePath + "_ma_labels.txt";
  const std::string trxPath = basePath + "_ma_input.trx";
  const std::string outPath = basePath + "_ma_output.trx";
  Cleanup(niftiPath);
  Cleanup(labelPath);
  Cleanup(trxPath);
  Cleanup(outPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { niftiPath, labelPath, trxPath, outPath } };

  if (!WriteParcellationNifti(niftiPath))
    return false;
  if (!WriteLabelFileTwoRegions(labelPath))
    return false;

  // Write a simple 3-streamline TRX (no DPS).
  {
    using PointType = itk::TrxStreamWriter::PointType;
    using StreamlineType = itk::TrxStreamWriter::StreamlineType;
    itk::TrxStreamWriter::MatrixType voxelToLps;
    voxelToLps.SetIdentity();
    itk::TrxStreamWriter::DimensionsType dims;
    dims[0] = dims[1] = dims[2] = 20;
    auto w = itk::TrxStreamWriter::New();
    w->SetFileName(trxPath);
    w->SetVoxelToLpsMatrix(voxelToLps);
    w->SetDimensions(dims);
    StreamlineType sl0;
    sl0.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    w->PushStreamline(sl0);
    StreamlineType sl1;
    sl1.push_back(PointType{ { 13.0, 13.0, 13.0 } });
    w->PushStreamline(sl1);
    StreamlineType sl2;
    sl2.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    sl2.push_back(PointType{ { 13.0, 13.0, 13.0 } });
    w->PushStreamline(sl2);
    w->Finalize();
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxPath);
  reader->Update();
  auto inputData = reader->GetOutput();

  // Two atlases with different prefixes but the same image and labels.
  itk::TrxParcellationLabeler::ParcellationSpec specP;
  specP.niftiPath = niftiPath;
  specP.labelFilePath = labelPath;
  specP.groupPrefix = "AtlasP";

  itk::TrxParcellationLabeler::ParcellationSpec specQ;
  specQ.niftiPath = niftiPath;
  specQ.labelFilePath = labelPath;
  specQ.groupPrefix = "AtlasQ";

  auto labeler = itk::TrxParcellationLabeler::New();
  labeler->SetInput(inputData);
  labeler->SetOutputFileName(outPath);
  labeler->AddParcellation(specP);
  labeler->AddParcellation(specQ);

  try
  {
    labeler->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "[ParcellationMultiAtlas] Update threw: " << e << "\n";
    return false;
  }

  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(outPath);
  reader2->Update();
  auto result = reader2->GetOutput();
  if (!result)
  {
    std::cerr << "[ParcellationMultiAtlas] null output\n";
    return false;
  }

  const auto names = result->GetGroupNames();
  for (const auto & expected : { std::string("AtlasP_Region_A"),
                                  std::string("AtlasP_Region_B"),
                                  std::string("AtlasQ_Region_A"),
                                  std::string("AtlasQ_Region_B") })
  {
    if (std::find(names.begin(), names.end(), expected) == names.end())
    {
      std::cerr << "[ParcellationMultiAtlas] group '" << expected << "' not found\n";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// 10. TrxParcellationLabeler: SetInputFileName copy-through mode
// ---------------------------------------------------------------------------

bool
TestParcellationCopyThrough(const std::string & basePath)
{
  const std::string niftiPath = basePath + "_ct_parc.nii.gz";
  const std::string labelPath = basePath + "_ct_labels.txt";
  const std::string trxPath = basePath + "_ct_input.trx";
  const std::string outPath = basePath + "_ct_output.trx";
  Cleanup(niftiPath);
  Cleanup(labelPath);
  Cleanup(trxPath);
  Cleanup(outPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        Cleanup(p);
    }
  } g{ { niftiPath, labelPath, trxPath, outPath } };

  if (!WriteParcellationNifti(niftiPath))
    return false;
  if (!WriteLabelFileTwoRegions(labelPath))
    return false;
  if (!WriteTrxForParcellation(trxPath))
    return false;

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxPath);
  reader->Update();
  auto inputData = reader->GetOutput();

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "CopyThru";

  auto labeler = itk::TrxParcellationLabeler::New();
  labeler->SetInput(inputData);
  labeler->SetInputFileName(trxPath); // enable copy-through mode
  labeler->SetOutputFileName(outPath);
  labeler->AddParcellation(spec);

  try
  {
    labeler->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "[ParcellationCopyThrough] Update threw: " << e << "\n";
    return false;
  }

  // Verify file sizes are accessible.
  const size_t pre = labeler->GetPreGroupFileBytes();
  const size_t fin = labeler->GetFinalFileBytes();
  if (fin < pre)
  {
    std::cerr << "[ParcellationCopyThrough] final file smaller than pre-group file\n";
    return false;
  }

  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(outPath);
  reader2->Update();
  auto result = reader2->GetOutput();
  if (!result)
  {
    std::cerr << "[ParcellationCopyThrough] null output\n";
    return false;
  }
  const auto names = result->GetGroupNames();
  const bool hasA = std::find(names.begin(), names.end(), "CopyThru_Region_A") != names.end();
  const bool hasB = std::find(names.begin(), names.end(), "CopyThru_Region_B") != names.end();
  if (!hasA || !hasB)
  {
    std::cerr << "[ParcellationCopyThrough] expected groups not found\n";
    return false;
  }

  return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test entry point
// ---------------------------------------------------------------------------

int
itkTrxCoverageTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <output_base_path>\n";
    return EXIT_FAILURE;
  }

  const std::string base = argv[1];
  const std::string dir = itksys::SystemTools::GetFilenamePath(base);
  if (!dir.empty() && !itksys::SystemTools::MakeDirectory(dir).IsSuccess())
  {
    std::cerr << "Cannot create output directory: " << dir << "\n";
    return EXIT_FAILURE;
  }

  struct Test
  {
    const char * name;
    bool         result;
  };

  std::vector<Test> tests = {
    { "VnlMatrixWriter", TestVnlMatrixWriter(base) },
    { "TransformVnlBuffer", TestTransformVnlBuffer(base) },
    { "WriterErrorPaths", TestWriterErrorPaths(base) },
    { "ReaderErrorPaths", TestReaderErrorPaths() },
    { "FileWriter", TestFileWriter(base) },
    { "StreamlineDataMethods", TestStreamlineDataMethods(base) },
    { "TdiWeightedField", TestTdiWeightedField(base) },
    { "TdiStaticCompute", TestTdiStaticCompute(base) },
    { "ParcellationDilation", TestParcellationDilation(base) },
    { "ParcellationMultiAtlas", TestParcellationMultiAtlas(base) },
    { "ParcellationCopyThrough", TestParcellationCopyThrough(base) },
  };

  bool allPassed = true;
  for (const auto & t : tests)
  {
    if (t.result)
    {
      std::cerr << "[CoverageTest] PASS: " << t.name << "\n";
    }
    else
    {
      std::cerr << "[CoverageTest] FAIL: " << t.name << "\n";
      allPassed = false;
    }
  }

  return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
