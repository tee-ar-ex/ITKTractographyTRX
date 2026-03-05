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

  std::cout << "All parcellation labeling tests passed.\n";
  return EXIT_SUCCESS;
}
