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
 * \file itkTrxParcellationLabelingTest.cxx
 *
 * Synthetic unit test for TrxParcellationLabeler.
 *
 * Sets up a 20×20×20 LPS image with two labeled regions, writes three
 * streamlines that intersect those regions in known patterns, runs the
 * labeler, and verifies that the output TRX contains the expected groups.
 */

#include "itkTrxFileReader.h"
#include "itkTrxParcellationLabeler.h"
#include "itkTrxStreamWriter.h"

#include "itkImage.h"
#include "itkImageFileWriter.h"
#include "itkNiftiImageIO.h"

#include "itksys/SystemTools.hxx"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

bool
MakeDir(const std::string & path)
{
  return itksys::SystemTools::MakeDirectory(path).IsSuccess();
}

void
Cleanup(const std::string & path)
{
  if (itksys::SystemTools::FileIsDirectory(path))
  {
    itksys::SystemTools::RemoveADirectory(path);
  }
  else if (itksys::SystemTools::FileExists(path, true))
  {
    itksys::SystemTools::RemoveFile(path);
  }
}

/** Write a simple NIfTI-1 label image programmatically. */
bool
WriteSyntheticParcellation(const std::string & niftiPath)
{
  using LabelImage = itk::Image<int32_t, 3>;

  LabelImage::IndexType start;
  start.Fill(0);
  LabelImage::SizeType size;
  size.Fill(20);
  LabelImage::RegionType region(start, size);

  auto image = LabelImage::New();
  image->SetRegions(region);
  image->Allocate(true); // zero-initialised

  // Isotropic 1 mm, identity direction, origin at [0,0,0] in LPS.
  // With this setup: LPS physical point (i,j,k) mm → voxel index (i,j,k).
  LabelImage::SpacingType spacing;
  spacing.Fill(1.0);
  image->SetSpacing(spacing);

  LabelImage::PointType origin;
  origin.Fill(0.0);
  image->SetOrigin(origin);

  // Direction = identity (LPS aligned).
  LabelImage::DirectionType dir;
  dir.SetIdentity();
  image->SetDirection(dir);

  // Region 1: voxels (2,2,2) to (5,5,5) inclusive.
  for (int k = 2; k <= 5; ++k)
  {
    for (int j = 2; j <= 5; ++j)
    {
      for (int i = 2; i <= 5; ++i)
      {
        LabelImage::IndexType idx = { { i, j, k } };
        image->SetPixel(idx, 1);
      }
    }
  }

  // Region 2: voxels (12,12,12) to (15,15,15) inclusive.
  for (int k = 12; k <= 15; ++k)
  {
    for (int j = 12; j <= 15; ++j)
    {
      for (int i = 12; i <= 15; ++i)
      {
        LabelImage::IndexType idx = { { i, j, k } };
        image->SetPixel(idx, 2);
      }
    }
  }

  using WriterType = itk::ImageFileWriter<LabelImage>;
  auto io = itk::NiftiImageIO::New();
  auto writer = WriterType::New();
  writer->SetImageIO(io);
  writer->SetFileName(niftiPath);
  writer->SetInput(image);
  try
  {
    writer->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "Failed to write parcellation: " << e << "\n";
    return false;
  }
  return true;
}

/** Write the label lookup text file. */
bool
WriteLabelFile(const std::string & txtPath)
{
  std::ofstream out(txtPath);
  if (!out.is_open())
  {
    return false;
  }
  out << "1\tRegion_A\n";
  out << "2\tRegion_B\n";
  return true;
}

/**
 * Build a synthetic TRX with three streamlines and one DPS field.
 *
 * Streamline 0: passes through Region A  (point at LPS [3,3,3])
 * Streamline 1: passes through Region B  (point at LPS [13,13,13])
 * Streamline 2: passes through both      (points at [3,3,3] and [13,13,13])
 *
 * DPS field "weight": values 0.1, 0.2, 0.3.
 */
bool
WriteSyntheticTrx(const std::string & trxPath)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;

  // Voxel-to-LPS matrix for a 20×20×20 1mm image at the origin.
  // TRX stores RAS internally, so voxelToRas flips X and Y of voxelToLps.
  itk::TrxStreamWriter::MatrixType voxelToLps;
  voxelToLps.SetIdentity();
  itk::TrxStreamWriter::DimensionsType dims;
  dims[0] = 20;
  dims[1] = 20;
  dims[2] = 20;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(trxPath);
  writer->SetVoxelToLpsMatrix(voxelToLps);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("weight", "float32");

  // Streamline 0 — region A only.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 0.5, 0.5, 0.5 } });
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } }); // inside region A
    sl.push_back(PointType{ { 6.0, 6.0, 6.0 } });
    writer->PushStreamline(sl, { { "weight", 0.1 } }, {}, {});
  }

  // Streamline 1 — region B only.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 9.0, 9.0, 9.0 } });
    sl.push_back(PointType{ { 13.0, 13.0, 13.0 } }); // inside region B
    sl.push_back(PointType{ { 17.0, 17.0, 17.0 } });
    writer->PushStreamline(sl, { { "weight", 0.2 } }, {}, {});
  }

  // Streamline 2 — both regions.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } });  // region A
    sl.push_back(PointType{ { 9.0, 9.0, 9.0 } });
    sl.push_back(PointType{ { 13.0, 13.0, 13.0 } }); // region B
    writer->PushStreamline(sl, { { "weight", 0.3 } }, {}, {});
  }

  try
  {
    writer->Finalize();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "Failed to write TRX: " << e << "\n";
    return false;
  }
  return true;
}

bool
CheckGroups(const std::string & trxPath)
{
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxPath);
  reader->Update();
  auto data = reader->GetOutput();

  const auto groupNames = data->GetGroupNames();

  // Expected groups and their memberships.
  const std::map<std::string, std::vector<uint32_t>> expected = {
    { "Test_Region_A", { 0, 2 } }, // streamlines 0 and 2
    { "Test_Region_B", { 1, 2 } }, // streamlines 1 and 2
  };

  bool ok = true;
  for (const auto & kv : expected)
  {
    const std::string & name = kv.first;
    const auto &        expIdx = kv.second;

    auto it = std::find(groupNames.begin(), groupNames.end(), name);
    if (it == groupNames.end())
    {
      std::cerr << "FAIL: group '" << name << "' not found in output.\n";
      ok = false;
      continue;
    }

    auto group = data->GetGroup(name);
    if (!group)
    {
      std::cerr << "FAIL: GetGroup('" << name << "') returned nullptr.\n";
      ok = false;
      continue;
    }

    auto gotIdx = group->GetStreamlineIndices();
    std::sort(gotIdx.begin(), gotIdx.end());

    if (gotIdx != expIdx)
    {
      std::cerr << "FAIL: group '" << name << "' has wrong indices. Got:";
      for (auto v : gotIdx)
      {
        std::cerr << " " << v;
      }
      std::cerr << "  Expected:";
      for (auto v : expIdx)
      {
        std::cerr << " " << v;
      }
      std::cerr << "\n";
      ok = false;
    }
    else
    {
      std::cout << "PASS: group '" << name << "' has correct indices.\n";
    }
  }

  // Check DPS passthrough.
  const auto dpsNames = data->GetDpsFieldNames();
  if (std::find(dpsNames.begin(), dpsNames.end(), "weight") == dpsNames.end())
  {
    std::cerr << "FAIL: DPS field 'weight' not found in output.\n";
    ok = false;
  }
  else
  {
    const auto weights = data->GetDpsField("weight");
    constexpr float tol = 1e-4f;
    if (weights.size() != 3 || std::abs(weights[0] - 0.1f) > tol ||
        std::abs(weights[1] - 0.2f) > tol || std::abs(weights[2] - 0.3f) > tol)
    {
      std::cerr << "FAIL: DPS 'weight' values are incorrect.\n";
      ok = false;
    }
    else
    {
      std::cout << "PASS: DPS field 'weight' preserved correctly.\n";
    }
  }

  return ok;
}

/** Write a label file with blank lines, comments, and CR+LF line endings. */
bool
WriteLabelFileEdgeCases(const std::string & txtPath)
{
  std::ofstream out(txtPath);
  if (!out.is_open())
    return false;
  out << "# comment line\n";
  out << "\n";
  out << "   \n";             // whitespace-only line
  out << "1\tRegion_A\r\n";  // Windows CR+LF
  out << "2\tRegion_B\n";
  return true;
}

/** Write a label file mapping only label 1 (so label 2 in the image is unlisted). */
bool
WriteLabelFilePartial(const std::string & txtPath)
{
  std::ofstream out(txtPath);
  if (!out.is_open())
    return false;
  out << "1\tOnly_Region\n";
  return true;
}

/** Write a second 20×20×20 NIfTI with a region at voxels (8,8,8)–(10,10,10), label 1. */
bool
WriteSyntheticParcellation2(const std::string & niftiPath)
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
  LabelImage::PointType orig;
  orig.Fill(0.0);
  image->SetOrigin(orig);
  LabelImage::DirectionType dir;
  dir.SetIdentity();
  image->SetDirection(dir);
  for (int k = 8; k <= 10; ++k)
    for (int j = 8; j <= 10; ++j)
      for (int i = 8; i <= 10; ++i)
      {
        LabelImage::IndexType idx = { { i, j, k } };
        image->SetPixel(idx, 1);
      }
  using WriterType = itk::ImageFileWriter<LabelImage>;
  auto io = itk::NiftiImageIO::New();
  auto wr = WriterType::New();
  wr->SetImageIO(io);
  wr->SetFileName(niftiPath);
  wr->SetInput(image);
  try
  {
    wr->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteSyntheticParcellation2 failed: " << e << "\n";
    return false;
  }
  return true;
}

/** Write a TRX with one streamline that has both a DPS field and a DPV field. */
bool
WriteSyntheticTrxWithDpv(const std::string & trxPath)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;
  itk::TrxStreamWriter::MatrixType voxelToLps;
  voxelToLps.SetIdentity();
  itk::TrxStreamWriter::DimensionsType dims;
  dims[0] = 20;
  dims[1] = 20;
  dims[2] = 20;
  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(trxPath);
  writer->SetVoxelToLpsMatrix(voxelToLps);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("weight", "float32");
  writer->RegisterDpvField("curvature", "float32");
  // One streamline through region A (3 points → 3 per-vertex curvature values).
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 1.0, 1.0, 1.0 } });
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } }); // inside region A
    sl.push_back(PointType{ { 6.0, 6.0, 6.0 } });
    writer->PushStreamline(sl, { { "weight", 0.5 } }, { { "curvature", { 0.1, 0.2, 0.3 } } }, {});
  }
  try
  {
    writer->Finalize();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "WriteSyntheticTrxWithDpv failed: " << e << "\n";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Sub-tests
// ---------------------------------------------------------------------------

/** Verify that Update() throws when required inputs are missing. */
bool
TestErrorPaths(const std::string &                      niftiPath,
               const std::string &                      labelPath,
               const std::string &                      outDir,
               itk::TrxStreamlineData::ConstPointer     inputData)
{
  bool ok = true;
  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "E";

  // 1. No input set.
  {
    auto lab = itk::TrxParcellationLabeler::New();
    lab->SetOutputFileName(outDir + "/err1.trx");
    lab->AddParcellation(spec);
    bool threw = false;
    try
    {
      lab->Update();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "FAIL: expected throw for no input\n";
      ok = false;
    }
    else
    {
      std::cout << "PASS: throws with no input\n";
    }
  }

  // 2. No output filename.
  {
    auto lab = itk::TrxParcellationLabeler::New();
    lab->SetInput(inputData);
    lab->AddParcellation(spec);
    bool threw = false;
    try
    {
      lab->Update();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "FAIL: expected throw for no output filename\n";
      ok = false;
    }
    else
    {
      std::cout << "PASS: throws with no output filename\n";
    }
  }

  // 3. No parcellations.
  {
    auto lab = itk::TrxParcellationLabeler::New();
    lab->SetInput(inputData);
    lab->SetOutputFileName(outDir + "/err3.trx");
    bool threw = false;
    try
    {
      lab->Update();
    }
    catch (const itk::ExceptionObject &)
    {
      threw = true;
    }
    if (!threw)
    {
      std::cerr << "FAIL: expected throw for no parcellations\n";
      ok = false;
    }
    else
    {
      std::cout << "PASS: throws with no parcellations\n";
    }
  }

  return ok;
}

/** Verify getters/setters on TrxParcellationLabeler. */
bool
TestAccessors()
{
  auto lab = itk::TrxParcellationLabeler::New();

  lab->SetInputFileName("/tmp/in.trx");
  if (lab->GetInputFileName() != "/tmp/in.trx")
  {
    std::cerr << "FAIL: GetInputFileName mismatch\n";
    return false;
  }

  lab->SetOutputFileName("/tmp/out.trx");
  if (lab->GetOutputFileName() != "/tmp/out.trx")
  {
    std::cerr << "FAIL: GetOutputFileName mismatch\n";
    return false;
  }

  lab->SetDilationRadius(3);
  if (lab->GetDilationRadius() != 3)
  {
    std::cerr << "FAIL: GetDilationRadius mismatch\n";
    return false;
  }

  lab->SetMaxDpvBytes(1024ULL * 1024ULL);
  if (lab->GetMaxDpvBytes() != 1024ULL * 1024ULL)
  {
    std::cerr << "FAIL: GetMaxDpvBytes mismatch\n";
    return false;
  }

  if (lab->GetPreGroupFileBytes() != 0 || lab->GetFinalFileBytes() != 0)
  {
    std::cerr << "FAIL: byte counts should be zero before Update\n";
    return false;
  }

  std::cout << "PASS: accessors work correctly\n";
  return true;
}

/** Verify PrintSelf does not crash and produces non-empty output. */
bool
TestPrintSelf(const std::string & niftiPath, const std::string & labelPath)
{
  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "PS";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInputFileName("/tmp/in.trx");
  lab->SetOutputFileName("/tmp/out.trx");
  lab->SetDilationRadius(2);
  lab->AddParcellation(spec);

  std::ostringstream oss;
  lab->Print(oss);
  if (oss.str().empty())
  {
    std::cerr << "FAIL: PrintSelf produced empty output\n";
    return false;
  }
  std::cout << "PASS: PrintSelf ran without error\n";
  return true;
}

/** Verify copy-through mode (InputFileName set): artifact is copied then groups appended. */
bool
TestCopyThroughMode(const std::string &                  niftiPath,
                    const std::string &                  labelPath,
                    const std::string &                  inputTrx,
                    const std::string &                  outDir,
                    itk::TrxStreamlineData::ConstPointer inputData)
{
  const std::string outputTrx = outDir + "/ct_out.trx";
  Cleanup(outputTrx);
  struct Guard
  {
    std::string p;
    ~Guard() { Cleanup(p); }
  } g{ outputTrx };

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "CT";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetInputFileName(inputTrx);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestCopyThroughMode threw: " << e << "\n";
    return false;
  }

  if (lab->GetPreGroupFileBytes() == 0)
  {
    std::cerr << "FAIL: GetPreGroupFileBytes is 0 after copy-through Update\n";
    return false;
  }
  if (lab->GetFinalFileBytes() < lab->GetPreGroupFileBytes())
  {
    std::cerr << "FAIL: FinalFileBytes < PreGroupFileBytes after group append\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  const auto names = rd->GetOutput()->GetGroupNames();
  if (std::find(names.begin(), names.end(), "CT_Region_A") == names.end() ||
      std::find(names.begin(), names.end(), "CT_Region_B") == names.end())
  {
    std::cerr << "FAIL: copy-through output missing expected groups\n";
    return false;
  }
  std::cout << "PASS: copy-through mode produces groups with correct byte counts\n";
  return true;
}

/**
 * Verify that dilation=1 captures a streamline one voxel beyond the label boundary,
 * and that dilation=0 does not.
 *
 * Region A covers voxels (2..5, 2..5, 2..5).  A streamline at LPS (6,3,3) maps to
 * voxel (6,3,3), which is 1 voxel away from boundary voxel (5,3,3) and thus falls
 * within a radius-1 ball dilation.
 */
bool
TestDilation(const std::string & niftiPath, const std::string & labelPath, const std::string & outDir)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;

  const std::string trxIn = outDir + "/dil_in.trx";
  const std::string outNoDil = outDir + "/dil_no.trx";
  const std::string outDil = outDir + "/dil_yes.trx";
  Cleanup(trxIn);
  Cleanup(outNoDil);
  Cleanup(outDil);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { trxIn, outNoDil, outDil } };

  {
    itk::TrxStreamWriter::MatrixType voxelToLps;
    voxelToLps.SetIdentity();
    itk::TrxStreamWriter::DimensionsType dims;
    dims[0] = 20;
    dims[1] = 20;
    dims[2] = 20;
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(trxIn);
    writer->SetVoxelToLpsMatrix(voxelToLps);
    writer->SetDimensions(dims);
    StreamlineType sl;
    sl.push_back(PointType{ { 7.0, 3.0, 3.0 } }); // outside region A
    sl.push_back(PointType{ { 6.0, 3.0, 3.0 } }); // 1 voxel beyond boundary
    try
    {
      writer->PushStreamline(sl, {}, {}, {});
      writer->Finalize();
    }
    catch (const itk::ExceptionObject & e)
    {
      std::cerr << "TestDilation: failed to write input TRX: " << e << "\n";
      return false;
    }
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxIn);
  reader->Update();
  auto inputData = reader->GetOutput();

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "D";

  // Without dilation: no group.
  {
    auto lab = itk::TrxParcellationLabeler::New();
    lab->SetInput(inputData);
    lab->SetOutputFileName(outNoDil);
    lab->AddParcellation(spec);
    lab->SetDilationRadius(0);
    lab->Update();
    auto rd = itk::TrxFileReader::New();
    rd->SetFileName(outNoDil);
    rd->Update();
    if (!rd->GetOutput()->GetGroupNames().empty())
    {
      std::cerr << "FAIL: dilation=0 should produce no groups for out-of-region streamline\n";
      return false;
    }
    std::cout << "PASS: dilation=0 does not capture adjacent streamline\n";
  }

  // With dilation=1: streamline at voxel (6,3,3) should be in Region_A.
  {
    auto lab = itk::TrxParcellationLabeler::New();
    lab->SetInput(inputData);
    lab->SetOutputFileName(outDil);
    lab->AddParcellation(spec);
    lab->SetDilationRadius(1);
    lab->Update();
    auto rd = itk::TrxFileReader::New();
    rd->SetFileName(outDil);
    rd->Update();
    const auto names = rd->GetOutput()->GetGroupNames();
    if (std::find(names.begin(), names.end(), "D_Region_A") == names.end())
    {
      std::cerr << "FAIL: dilation=1 should capture adjacent streamline into Region_A\n";
      return false;
    }
    std::cout << "PASS: dilation=1 captures adjacent streamline\n";
  }

  return true;
}

/**
 * Verify that two atlases are processed simultaneously and each contributes its own groups.
 *
 * Atlas 1: original parcellation (Region_A at 2-5, Region_B at 12-15).
 * Atlas 2: new parcellation (Region_C at 8-10).
 * The existing 3-streamline TRX has a streamline at (9,9,9) → should land in Region_C.
 */
bool
TestMultipleAtlases(const std::string &                  niftiPath,
                    const std::string &                  labelPath,
                    const std::string &                  outDir,
                    itk::TrxStreamlineData::ConstPointer inputData)
{
  const std::string nifti2 = outDir + "/parc2.nii.gz";
  const std::string label2 = outDir + "/labels2.txt";
  const std::string outputTrx = outDir + "/multi_atlas_out.trx";
  Cleanup(nifti2);
  Cleanup(label2);
  Cleanup(outputTrx);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { nifti2, label2, outputTrx } };

  if (!WriteSyntheticParcellation2(nifti2))
    return false;
  {
    std::ofstream out(label2);
    out << "1\tRegion_C\n";
  }

  itk::TrxParcellationLabeler::ParcellationSpec spec1;
  spec1.niftiPath = niftiPath;
  spec1.labelFilePath = labelPath;
  spec1.groupPrefix = "MA1";

  itk::TrxParcellationLabeler::ParcellationSpec spec2;
  spec2.niftiPath = nifti2;
  spec2.labelFilePath = label2;
  spec2.groupPrefix = "MA2";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec1);
  lab->AddParcellation(spec2);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestMultipleAtlases threw: " << e << "\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  auto d = rd->GetOutput();
  const auto names = d->GetGroupNames();

  bool ok = true;
  for (const auto & expected : { "MA1_Region_A", "MA1_Region_B", "MA2_Region_C" })
  {
    if (std::find(names.begin(), names.end(), expected) == names.end())
    {
      std::cerr << "FAIL: multi-atlas output missing group '" << expected << "'\n";
      ok = false;
    }
  }

  // Atlas 2 region covers (8-10, 8-10, 8-10).
  // Streamline 1 passes through (9,9,9) and streamline 2 also passes through (9,9,9),
  // so both should be in MA2_Region_C.  Streamline 0 stays in (0.5-6) range → not in it.
  if (ok)
  {
    auto grp = d->GetGroup("MA2_Region_C");
    if (!grp)
    {
      std::cerr << "FAIL: MA2_Region_C group not found\n";
      return false;
    }
    auto idx = grp->GetStreamlineIndices();
    std::sort(idx.begin(), idx.end());
    if (idx != (std::vector<uint32_t>{ 1, 2 }))
    {
      std::cerr << "FAIL: MA2_Region_C should contain streamlines 1 and 2\n";
      ok = false;
    }
  }

  if (ok)
    std::cout << "PASS: multiple atlases produce correct independent groups\n";
  return ok;
}

/** Verify that a label present in the image but absent from the label file is silently skipped. */
bool
TestUnknownLabelSkipped(const std::string &                  niftiPath,
                        const std::string &                  outDir,
                        itk::TrxStreamlineData::ConstPointer inputData)
{
  const std::string partialLabel = outDir + "/partial_labels.txt";
  const std::string outputTrx = outDir + "/unknown_label_out.trx";
  Cleanup(partialLabel);
  Cleanup(outputTrx);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { partialLabel, outputTrx } };

  if (!WriteLabelFilePartial(partialLabel))
    return false;

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = partialLabel;
  spec.groupPrefix = "UL";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestUnknownLabelSkipped threw: " << e << "\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  const auto names = rd->GetOutput()->GetGroupNames();

  bool ok = true;
  if (std::find(names.begin(), names.end(), "UL_Only_Region") == names.end())
  {
    std::cerr << "FAIL: UL_Only_Region should exist\n";
    ok = false;
  }
  for (const auto & n : names)
  {
    if (n.find("UL_") == 0 && n != "UL_Only_Region")
    {
      std::cerr << "FAIL: unexpected group '" << n << "' for unlisted label\n";
      ok = false;
    }
  }
  if (ok)
    std::cout << "PASS: unlisted labels produce no group\n";
  return ok;
}

/** Verify that blank lines, comments, and CR+LF in the label file are handled correctly. */
bool
TestLabelFileEdgeCases(const std::string &                  niftiPath,
                       const std::string &                  outDir,
                       itk::TrxStreamlineData::ConstPointer inputData)
{
  const std::string edgeLabelPath = outDir + "/edge_labels.txt";
  const std::string outputTrx = outDir + "/edge_label_out.trx";
  Cleanup(edgeLabelPath);
  Cleanup(outputTrx);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { edgeLabelPath, outputTrx } };

  if (!WriteLabelFileEdgeCases(edgeLabelPath))
    return false;

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = edgeLabelPath;
  spec.groupPrefix = "EL";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestLabelFileEdgeCases threw: " << e << "\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  const auto names = rd->GetOutput()->GetGroupNames();
  if (std::find(names.begin(), names.end(), "EL_Region_A") == names.end() ||
      std::find(names.begin(), names.end(), "EL_Region_B") == names.end())
  {
    std::cerr << "FAIL: edge-case label file did not produce expected groups\n";
    return false;
  }
  std::cout << "PASS: label file with blank lines/comments/CR+LF parsed correctly\n";
  return true;
}

/** Verify that a DPV field is preserved through the full-rewrite path (no InputFileName). */
bool
TestDpvPassthrough(const std::string & niftiPath,
                   const std::string & labelPath,
                   const std::string & outDir)
{
  const std::string dpvTrx = outDir + "/dpv_in.trx";
  const std::string outputTrx = outDir + "/dpv_out.trx";
  Cleanup(dpvTrx);
  Cleanup(outputTrx);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { dpvTrx, outputTrx } };

  if (!WriteSyntheticTrxWithDpv(dpvTrx))
    return false;

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(dpvTrx);
  reader->Update();
  auto inputData = reader->GetOutput();

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "DPV";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestDpvPassthrough threw: " << e << "\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  auto d = rd->GetOutput();

  bool ok = true;
  const auto dpsNames = d->GetDpsFieldNames();
  if (std::find(dpsNames.begin(), dpsNames.end(), "weight") == dpsNames.end())
  {
    std::cerr << "FAIL: DPS 'weight' not preserved through rewrite\n";
    ok = false;
  }
  const auto dpvNames = d->GetDpvFieldNames();
  if (std::find(dpvNames.begin(), dpvNames.end(), "curvature") == dpvNames.end())
  {
    std::cerr << "FAIL: DPV 'curvature' not preserved through rewrite\n";
    ok = false;
  }
  if (ok)
    std::cout << "PASS: DPV field preserved through rewrite path\n";
  return ok;
}

/** Verify that a streamline entirely outside all labeled regions ends up in no group. */
bool
TestNoMatchingRegion(const std::string & niftiPath,
                     const std::string & labelPath,
                     const std::string & outDir)
{
  using PointType = itk::TrxStreamWriter::PointType;
  using StreamlineType = itk::TrxStreamWriter::StreamlineType;

  const std::string trxIn = outDir + "/nomatch_in.trx";
  const std::string outputTrx = outDir + "/nomatch_out.trx";
  Cleanup(trxIn);
  Cleanup(outputTrx);
  struct Guard
  {
    std::vector<std::string> ps;
    ~Guard()
    {
      for (auto & p : ps)
        Cleanup(p);
    }
  } g{ { trxIn, outputTrx } };

  {
    itk::TrxStreamWriter::MatrixType voxelToLps;
    voxelToLps.SetIdentity();
    itk::TrxStreamWriter::DimensionsType dims;
    dims[0] = 20;
    dims[1] = 20;
    dims[2] = 20;
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(trxIn);
    writer->SetVoxelToLpsMatrix(voxelToLps);
    writer->SetDimensions(dims);
    // Streamline stays in background (label 0) the whole time.
    StreamlineType sl;
    sl.push_back(PointType{ { 7.0, 7.0, 7.0 } });
    sl.push_back(PointType{ { 8.0, 8.0, 8.0 } });
    try
    {
      writer->PushStreamline(sl, {}, {}, {});
      writer->Finalize();
    }
    catch (const itk::ExceptionObject & e)
    {
      std::cerr << "TestNoMatchingRegion: failed to write TRX: " << e << "\n";
      return false;
    }
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(trxIn);
  reader->Update();
  auto inputData = reader->GetOutput();

  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "NM";

  auto lab = itk::TrxParcellationLabeler::New();
  lab->SetInput(inputData);
  lab->SetOutputFileName(outputTrx);
  lab->AddParcellation(spec);

  try
  {
    lab->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "TestNoMatchingRegion threw: " << e << "\n";
    return false;
  }

  auto rd = itk::TrxFileReader::New();
  rd->SetFileName(outputTrx);
  rd->Update();
  if (!rd->GetOutput()->GetGroupNames().empty())
  {
    std::cerr << "FAIL: no groups expected when streamline misses all regions\n";
    return false;
  }
  std::cout << "PASS: streamline outside all regions produces no groups\n";
  return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test entry point
// ---------------------------------------------------------------------------

int
itkTrxParcellationLabelingTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <output_dir>\n";
    return EXIT_FAILURE;
  }

  const std::string outDir = argv[1];
  if (!MakeDir(outDir))
  {
    std::cerr << "Cannot create output directory: " << outDir << "\n";
    return EXIT_FAILURE;
  }

  const std::string niftiPath = outDir + "/test_parcellation.nii.gz";
  const std::string labelPath = outDir + "/test_labels.txt";
  const std::string inputTrx = outDir + "/input.trx";
  const std::string outputTrx = outDir + "/labeled.trx";

  // Cleanup on exit.
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
      {
        Cleanup(p);
      }
    }
  } guard;
  guard.paths = { niftiPath, labelPath, inputTrx, outputTrx };

  if (!WriteSyntheticParcellation(niftiPath))
  {
    return EXIT_FAILURE;
  }
  if (!WriteLabelFile(labelPath))
  {
    std::cerr << "Cannot write label file.\n";
    return EXIT_FAILURE;
  }
  if (!WriteSyntheticTrx(inputTrx))
  {
    return EXIT_FAILURE;
  }

  // Load input TRX.
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(inputTrx);
  reader->Update();
  auto inputData = reader->GetOutput();

  // Run the labeler.
  itk::TrxParcellationLabeler::ParcellationSpec spec;
  spec.niftiPath = niftiPath;
  spec.labelFilePath = labelPath;
  spec.groupPrefix = "Test";

  auto labeler = itk::TrxParcellationLabeler::New();
  labeler->SetInput(inputData);
  labeler->SetOutputFileName(outputTrx);
  labeler->AddParcellation(spec);

  try
  {
    labeler->Update();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "Labeler threw: " << e << "\n";
    return EXIT_FAILURE;
  }

  if (!CheckGroups(outputTrx))
  {
    return EXIT_FAILURE;
  }

  // -------------------------------------------------------------------------
  // Additional sub-tests
  // -------------------------------------------------------------------------

  bool allPassed = true;

  allPassed &= TestErrorPaths(niftiPath, labelPath, outDir, inputData);
  allPassed &= TestAccessors();
  allPassed &= TestPrintSelf(niftiPath, labelPath);
  allPassed &= TestCopyThroughMode(niftiPath, labelPath, inputTrx, outDir, inputData);
  allPassed &= TestDilation(niftiPath, labelPath, outDir);
  allPassed &= TestMultipleAtlases(niftiPath, labelPath, outDir, inputData);
  allPassed &= TestUnknownLabelSkipped(niftiPath, outDir, inputData);
  allPassed &= TestLabelFileEdgeCases(niftiPath, outDir, inputData);
  allPassed &= TestDpvPassthrough(niftiPath, labelPath, outDir);
  allPassed &= TestNoMatchingRegion(niftiPath, labelPath, outDir);

  if (!allPassed)
  {
    return EXIT_FAILURE;
  }

  std::cout << "All parcellation labeling tests passed.\n";
  return EXIT_SUCCESS;
}
