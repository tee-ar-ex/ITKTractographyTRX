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

#include "itkImage.h"
#include "itkImageFileWriter.h"
#include "itkNiftiImageIO.h"
#include "itkMath.h"
#include "itkTrxGroupTdiMapper.h"
#include "itkTrxStreamWriter.h"
#include "itkContinuousIndex.h"

#include "itksys/SystemTools.hxx"

#include <trx/trx.h>

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <array>

namespace
{
using ImageType = itk::Image<float, 3>;

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

bool
WriteReferenceImage(const std::string & niftiPath)
{
  ImageType::IndexType start;
  start.Fill(0);
  ImageType::SizeType size;
  size.Fill(20);
  ImageType::RegionType region(start, size);

  auto image = ImageType::New();
  image->SetRegions(region);
  image->Allocate(true);
  ImageType::SpacingType spacing;
  spacing.Fill(1.0);
  image->SetSpacing(spacing);
  ImageType::PointType origin;
  origin.Fill(0.0);
  image->SetOrigin(origin);
  ImageType::DirectionType direction;
  direction.SetIdentity();
  image->SetDirection(direction);

  using WriterType = itk::ImageFileWriter<ImageType>;
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
    std::cerr << "Failed to write reference image: " << e << '\n';
    return false;
  }
  return true;
}

bool
WriteSyntheticTrxWithMismatchedHeader(const std::string & trxPath)
{
  using WriterType = itk::TrxStreamWriter;
  using PointType = WriterType::PointType;
  using StreamlineType = WriterType::StreamlineType;

  auto writer = WriterType::New();
  writer->SetFileName(trxPath);
  WriterType::DimensionsType dims;
  dims[0] = 20;
  dims[1] = 20;
  dims[2] = 20;
  writer->SetDimensions(dims);

  // Intentionally mismatched header geometry. The mapper must ignore this and
  // only use the reference NIfTI geometry for voxel assignment.
  WriterType::MatrixType voxelToLps;
  voxelToLps.SetIdentity();
  voxelToLps[0][3] = 100.0;
  voxelToLps[1][3] = -200.0;
  voxelToLps[2][3] = 50.0;
  writer->SetVoxelToLpsMatrix(voxelToLps);

  // Streamline 0 (in group): visits voxel (3,3,3) twice, and voxel (4,4,4) once.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.1, 3.1, 3.1 } });
    sl.push_back(PointType{ { 3.4, 3.4, 3.4 } });
    sl.push_back(PointType{ { 4.0, 4.0, 4.0 } });
    writer->PushStreamline(sl, {}, {}, {});
  }
  // Streamline 1 (not in group): visits voxel (3,3,3), should be ignored.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.2, 3.2, 3.2 } });
    writer->PushStreamline(sl, {}, {}, {});
  }
  // Streamline 2 (in group): visits voxel (3,3,3), and an out-of-bounds point.
  {
    StreamlineType sl;
    sl.push_back(PointType{ { 3.0, 3.0, 3.0 } });
    sl.push_back(PointType{ { 1000.0, 1000.0, 1000.0 } });
    writer->PushStreamline(sl, {}, {}, {});
  }

  try
  {
    writer->Finalize();
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "Failed to write synthetic TRX: " << e << '\n';
    return false;
  }

  std::map<std::string, std::vector<uint32_t>> groups;
  groups["TargetGroup"] = { 0, 2 };
  try
  {
    trx::append_groups_to_zip(trxPath, groups);
  }
  catch (const std::exception & e)
  {
    std::cerr << "Failed to append groups: " << e.what() << '\n';
    return false;
  }
  return true;
}

inline size_t
Flatten(size_t i, size_t j, size_t k, size_t nx, size_t ny)
{
  return i + nx * (j + ny * k);
}

struct LpsToVoxelState
{
  std::array<double, 9> M{};
  std::array<double, 3> origin{};
  itk::Index<3>         bufferStart{};
  itk::Size<3>          bufferSize{};
};

struct RasToVoxelState
{
  std::array<double, 9> M{};
  std::array<double, 3> b{};
  itk::Index<3>         bufferStart{};
  itk::Size<3>          bufferSize{};
};

LpsToVoxelState
BuildLpsToVoxelState(const ImageType * image)
{
  LpsToVoxelState state;
  const auto buffered = image->GetBufferedRegion();
  state.bufferStart = buffered.GetIndex();
  state.bufferSize = buffered.GetSize();
  const auto & invDir = image->GetInverseDirection();
  const auto & spacing = image->GetSpacing();
  const auto & origin = image->GetOrigin();
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 3; ++c)
    {
      state.M[static_cast<size_t>(r * 3 + c)] = invDir(r, c) / spacing[r];
    }
    state.origin[static_cast<size_t>(r)] = origin[r];
  }
  return state;
}

RasToVoxelState
BuildRasToVoxelState(const ImageType * image)
{
  RasToVoxelState state;
  const auto buffered = image->GetBufferedRegion();
  state.bufferStart = buffered.GetIndex();
  state.bufferSize = buffered.GetSize();

  const auto & invDir = image->GetInverseDirection();
  const auto & spacing = image->GetSpacing();
  const auto & origin = image->GetOrigin();
  const double lpsToVoxel[9] = { invDir(0, 0) / spacing[0],
                                 invDir(0, 1) / spacing[0],
                                 invDir(0, 2) / spacing[0],
                                 invDir(1, 0) / spacing[1],
                                 invDir(1, 1) / spacing[1],
                                 invDir(1, 2) / spacing[1],
                                 invDir(2, 0) / spacing[2],
                                 invDir(2, 1) / spacing[2],
                                 invDir(2, 2) / spacing[2] };
  const double rasToLps[9] = { -1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0 };
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 3; ++c)
    {
      double sum = 0.0;
      for (int k = 0; k < 3; ++k)
      {
        sum += lpsToVoxel[r * 3 + k] * rasToLps[k * 3 + c];
      }
      state.M[static_cast<size_t>(r * 3 + c)] = sum;
    }
    state.b[static_cast<size_t>(r)] = -(lpsToVoxel[r * 3 + 0] * origin[0] + lpsToVoxel[r * 3 + 1] * origin[1] +
                                        lpsToVoxel[r * 3 + 2] * origin[2]);
  }
  return state;
}

bool
PhysicalPointToIndexLikeParcellation(const LpsToVoxelState & state,
                                     const ImageType::PointType & pointLps,
                                     ImageType::IndexType & index)
{
  using IndexValueType = ImageType::IndexType::IndexValueType;
  const double dx = pointLps[0] - state.origin[0];
  const double dy = pointLps[1] - state.origin[1];
  const double dz = pointLps[2] - state.origin[2];
  const int i = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(state.M[0] * dx + state.M[1] * dy + state.M[2] * dz));
  const int j = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(state.M[3] * dx + state.M[4] * dy + state.M[5] * dz));
  const int k = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(state.M[6] * dx + state.M[7] * dy + state.M[8] * dz));
  index[0] = i;
  index[1] = j;
  index[2] = k;
  const int startI = state.bufferStart[0];
  const int startJ = state.bufferStart[1];
  const int startK = state.bufferStart[2];
  const int endI = startI + static_cast<int>(state.bufferSize[0]);
  const int endJ = startJ + static_cast<int>(state.bufferSize[1]);
  const int endK = startK + static_cast<int>(state.bufferSize[2]);
  return (i >= startI && i < endI && j >= startJ && j < endJ && k >= startK && k < endK);
}

bool
RasPointToIndexLikeGroupTdi(const RasToVoxelState & state,
                            const std::array<double, 3> & pointRas,
                            ImageType::IndexType & index)
{
  using IndexValueType = ImageType::IndexType::IndexValueType;
  const double fi = state.M[0] * pointRas[0] + state.M[1] * pointRas[1] + state.M[2] * pointRas[2] + state.b[0];
  const double fj = state.M[3] * pointRas[0] + state.M[4] * pointRas[1] + state.M[5] * pointRas[2] + state.b[1];
  const double fk = state.M[6] * pointRas[0] + state.M[7] * pointRas[1] + state.M[8] * pointRas[2] + state.b[2];
  const int i = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fi));
  const int j = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fj));
  const int k = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fk));
  index[0] = i;
  index[1] = j;
  index[2] = k;
  const int startI = state.bufferStart[0];
  const int startJ = state.bufferStart[1];
  const int startK = state.bufferStart[2];
  const int endI = startI + static_cast<int>(state.bufferSize[0]);
  const int endJ = startJ + static_cast<int>(state.bufferSize[1]);
  const int endK = startK + static_cast<int>(state.bufferSize[2]);
  return (i >= startI && i < endI && j >= startJ && j < endJ && k >= startK && k < endK);
}

bool
RunPointToIndexParityAssertions()
{
  using ContinuousIndexType = itk::ContinuousIndex<double, 3>;
  auto MakeContinuousIndex = [](double x, double y, double z) {
    ContinuousIndexType ci;
    ci[0] = x;
    ci[1] = y;
    ci[2] = z;
    return ci;
  };

  ImageType::IndexType start;
  start[0] = -4;
  start[1] = 3;
  start[2] = -2;
  ImageType::SizeType size;
  size[0] = 17;
  size[1] = 19;
  size[2] = 23;
  ImageType::RegionType region(start, size);

  auto image = ImageType::New();
  image->SetRegions(region);
  image->Allocate(true);

  ImageType::SpacingType spacing;
  spacing[0] = 1.3;
  spacing[1] = 0.9;
  spacing[2] = 2.1;
  image->SetSpacing(spacing);

  ImageType::PointType origin;
  origin[0] = -24.0;
  origin[1] = 7.5;
  origin[2] = 11.0;
  image->SetOrigin(origin);

  const double ax = 0.35;
  const double ay = -0.40;
  const double az = 0.25;
  const double cx = std::cos(ax), sx = std::sin(ax);
  const double cy = std::cos(ay), sy = std::sin(ay);
  const double cz = std::cos(az), sz = std::sin(az);

  ImageType::DirectionType dir;
  dir(0, 0) = cz * cy;
  dir(0, 1) = cz * sy * sx - sz * cx;
  dir(0, 2) = cz * sy * cx + sz * sx;
  dir(1, 0) = sz * cy;
  dir(1, 1) = sz * sy * sx + cz * cx;
  dir(1, 2) = sz * sy * cx - cz * sx;
  dir(2, 0) = -sy;
  dir(2, 1) = cy * sx;
  dir(2, 2) = cy * cx;
  image->SetDirection(dir);

  const auto lpsState = BuildLpsToVoxelState(image);
  const auto rasState = BuildRasToVoxelState(image);

  // Keep tilted-FOV parity points away from exact half-integer boundaries
  // to avoid fragile compiler-dependent ties after round-trip transforms.
  std::vector<ContinuousIndexType> cis = {
    MakeContinuousIndex(-0.5001, 3.25, 4.75),
    MakeContinuousIndex(-0.4999, 3.25, 4.75),
    MakeContinuousIndex(2.5, 5.5, 7.5),
    MakeContinuousIndex(6.2, 8.8, 1.1),
    MakeContinuousIndex(16.49, 18.49, 22.49),
    MakeContinuousIndex(16.499, 18.499, 22.499),
    MakeContinuousIndex(-1.49, 4.2, 6.9),
    MakeContinuousIndex(7.5, -0.5001, 10.5)
  };

  for (const auto & ci : cis)
  {
    ImageType::PointType pLps;
    image->TransformContinuousIndexToPhysicalPoint(ci, pLps);

    ImageType::IndexType itkIndex;
    const bool itkInside = image->TransformPhysicalPointToIndex(pLps, itkIndex);

    ImageType::IndexType parIndex;
    const bool parInside = PhysicalPointToIndexLikeParcellation(lpsState, pLps, parIndex);
    const bool itkInBuffered = itkInside && image->GetBufferedRegion().IsInside(itkIndex);
    if (itkInBuffered != parInside || (itkInBuffered && parIndex != itkIndex))
    {
      std::cerr << "FAIL: parcellation-style mapping mismatch for CI=(" << ci[0] << ", " << ci[1] << ", " << ci[2]
                << ").\n";
      return false;
    }

    std::array<double, 3> pRas = { -pLps[0], -pLps[1], pLps[2] };
    ImageType::IndexType rasIndex;
    const bool rasInside = RasPointToIndexLikeGroupTdi(rasState, pRas, rasIndex);
    if (itkInBuffered != rasInside || (itkInBuffered && rasIndex != itkIndex))
    {
      std::cerr << "FAIL: RAS-style mapping mismatch for CI=(" << ci[0] << ", " << ci[1] << ", " << ci[2] << ").\n";
      return false;
    }
  }

  // Separate tie-boundary check on axis-aligned geometry where -0.5 maps
  // exactly and deterministically to validate ITK half-up behavior.
  auto axisImage = ImageType::New();
  ImageType::IndexType axisStart;
  axisStart.Fill(0);
  ImageType::SizeType axisSize;
  axisSize.Fill(16);
  axisImage->SetRegions(ImageType::RegionType(axisStart, axisSize));
  axisImage->Allocate(true);
  ImageType::SpacingType axisSpacing;
  axisSpacing.Fill(1.0);
  axisImage->SetSpacing(axisSpacing);
  ImageType::PointType axisOrigin;
  axisOrigin.Fill(0.0);
  axisImage->SetOrigin(axisOrigin);
  ImageType::DirectionType axisDir;
  axisDir.SetIdentity();
  axisImage->SetDirection(axisDir);
  const auto axisLpsState = BuildLpsToVoxelState(axisImage);
  const auto axisRasState = BuildRasToVoxelState(axisImage);

  std::vector<ContinuousIndexType> tieCis = {
    MakeContinuousIndex(-0.5, 3.25, 4.75),
    MakeContinuousIndex(-0.5000001, 3.25, 4.75),
    MakeContinuousIndex(-0.4999999, 3.25, 4.75),
    MakeContinuousIndex(7.5, 8.5, 9.5)
  };

  for (const auto & ci : tieCis)
  {
    ImageType::PointType pLps;
    axisImage->TransformContinuousIndexToPhysicalPoint(ci, pLps);

    const ImageType::IndexType itkIndex = axisImage->TransformPhysicalPointToIndex(pLps);
    const bool itkInBuffered = axisImage->GetBufferedRegion().IsInside(itkIndex);

    ImageType::IndexType parIndex;
    const bool parInside = PhysicalPointToIndexLikeParcellation(axisLpsState, pLps, parIndex);
    if (itkInBuffered != parInside || (itkInBuffered && parIndex != itkIndex))
    {
      std::cerr << "FAIL: axis parcellation-style mismatch for CI=(" << ci[0] << ", " << ci[1] << ", " << ci[2]
                << ").\n";
      return false;
    }

    std::array<double, 3> pRas = { -pLps[0], -pLps[1], pLps[2] };
    ImageType::IndexType rasIndex;
    const bool rasInside = RasPointToIndexLikeGroupTdi(axisRasState, pRas, rasIndex);
    if (itkInBuffered != rasInside || (itkInBuffered && rasIndex != itkIndex))
    {
      std::cerr << "FAIL: axis RAS-style mismatch for CI=(" << ci[0] << ", " << ci[1] << ", " << ci[2] << ").\n";
      return false;
    }
  }

  return true;
}

bool
RunSumAssertions(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetGroupName("TargetGroup");
  mapper->SetReferenceImageFileName(referencePath);
  itk::TrxGroupTdiMapper::Options options;
  options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
  mapper->SetOptions(options);
  mapper->Update();
  const auto * out = mapper->GetOutput();
  if (!out)
  {
    std::cerr << "FAIL: null output image in sum mode.\n";
    return false;
  }

  const auto size = out->GetLargestPossibleRegion().GetSize();
  const size_t nx = size[0];
  const size_t ny = size[1];
  const size_t nz = size[2];
  const auto * buffer = out->GetBufferPointer();
  if (!buffer)
  {
    std::cerr << "FAIL: null output buffer in sum mode.\n";
    return false;
  }
  const float v333 = buffer[Flatten(3, 3, 3, nx, ny)];
  const float v444 = buffer[Flatten(4, 4, 4, nx, ny)];
  const float v222 = buffer[Flatten(2, 2, 2, nx, ny)];

  constexpr float tol = 1e-6f;
  if (std::abs(v333 - 2.0f) > tol || std::abs(v444 - 1.0f) > tol || std::abs(v222) > tol)
  {
    std::cerr << "FAIL: sum mode mismatch."
              << " v333=" << v333 << " v444=" << v444 << " v222=" << v222 << '\n';
    return false;
  }

  size_t nonZero = 0;
  for (size_t idx = 0; idx < nx * ny * nz; ++idx)
  {
    if (buffer[idx] != 0.0f)
    {
      ++nonZero;
    }
  }
  if (nonZero != 2)
  {
    std::cerr << "FAIL: expected exactly 2 non-zero voxels in sum mode, got " << nonZero << '\n';
    return false;
  }
  return true;
}

bool
RunMeanAssertions(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetGroupName("TargetGroup");
  mapper->SetReferenceImageFileName(referencePath);
  itk::TrxGroupTdiMapper::Options options;
  options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Mean;
  mapper->SetOptions(options);
  mapper->Update();
  const auto * out = mapper->GetOutput();
  if (!out)
  {
    std::cerr << "FAIL: null output image in mean mode.\n";
    return false;
  }

  const auto size = out->GetLargestPossibleRegion().GetSize();
  const size_t nx = size[0];
  const size_t ny = size[1];
  const auto * buffer = out->GetBufferPointer();
  if (!buffer)
  {
    std::cerr << "FAIL: null output buffer in mean mode.\n";
    return false;
  }
  const float v333 = buffer[Flatten(3, 3, 3, nx, ny)];
  const float v444 = buffer[Flatten(4, 4, 4, nx, ny)];

  constexpr float tol = 1e-6f;
  if (std::abs(v333 - 1.0f) > tol || std::abs(v444 - 1.0f) > tol)
  {
    std::cerr << "FAIL: mean mode mismatch."
              << " v333=" << v333 << " v444=" << v444 << '\n';
    return false;
  }
  return true;
}

bool
RunMissingGroupFailureCheck(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetGroupName("DoesNotExist");
  mapper->SetReferenceImageFileName(referencePath);
  try
  {
    mapper->Update();
  }
  catch (const itk::ExceptionObject &)
  {
    return true;
  }
  std::cerr << "FAIL: expected missing-group exception was not thrown.\n";
  return false;
}

bool
RunSelectedIdsAssertions(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetReferenceImageFileName(referencePath);
  mapper->SetSelectedStreamlineIds({ 0, 2 });
  itk::TrxGroupTdiMapper::Options options;
  options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
  mapper->SetOptions(options);
  mapper->Update();
  const auto * out = mapper->GetOutput();
  if (!out || !out->GetBufferPointer())
  {
    std::cerr << "FAIL: null output for selected-id path.\n";
    return false;
  }

  const auto size = out->GetLargestPossibleRegion().GetSize();
  const size_t nx = size[0];
  const size_t ny = size[1];
  const auto * buffer = out->GetBufferPointer();
  constexpr float tol = 1e-6f;
  const float v333 = buffer[Flatten(3, 3, 3, nx, ny)];
  const float v444 = buffer[Flatten(4, 4, 4, nx, ny)];
  if (std::abs(v333 - 2.0f) > tol || std::abs(v444 - 1.0f) > tol)
  {
    std::cerr << "FAIL: selected-id path mismatch. v333=" << v333 << " v444=" << v444 << '\n';
    return false;
  }
  return true;
}

bool
RunSelectedIdsGroupIntersectionAssertions(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetReferenceImageFileName(referencePath);
  mapper->SetGroupName("TargetGroup");
  mapper->SetSelectedStreamlineIds({ 0 });
  itk::TrxGroupTdiMapper::Options options;
  options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
  mapper->SetOptions(options);
  mapper->Update();
  const auto * out = mapper->GetOutput();
  if (!out || !out->GetBufferPointer())
  {
    std::cerr << "FAIL: null output for selected-id/group intersection path.\n";
    return false;
  }

  const auto size = out->GetLargestPossibleRegion().GetSize();
  const size_t nx = size[0];
  const size_t ny = size[1];
  const auto * buffer = out->GetBufferPointer();
  constexpr float tol = 1e-6f;
  const float v333 = buffer[Flatten(3, 3, 3, nx, ny)];
  const float v444 = buffer[Flatten(4, 4, 4, nx, ny)];
  if (std::abs(v333 - 1.0f) > tol || std::abs(v444 - 1.0f) > tol)
  {
    std::cerr << "FAIL: selected/group intersection mismatch. v333=" << v333 << " v444=" << v444 << '\n';
    return false;
  }
  return true;
}

bool
RunEmptySelectedIdsAssertions(const std::string & trxPath, const std::string & referencePath)
{
  auto mapper = itk::TrxGroupTdiMapper::New();
  mapper->SetInputFileName(trxPath);
  mapper->SetReferenceImageFileName(referencePath);
  mapper->SetSelectedStreamlineIds({});
  mapper->Update();
  const auto * out = mapper->GetOutput();
  if (!out || !out->GetBufferPointer())
  {
    std::cerr << "FAIL: null output for empty selected-id path.\n";
    return false;
  }
  const auto size = out->GetLargestPossibleRegion().GetSize();
  const size_t nx = size[0];
  const size_t ny = size[1];
  const size_t nz = size[2];
  const auto * buffer = out->GetBufferPointer();
  for (size_t idx = 0; idx < nx * ny * nz; ++idx)
  {
    if (buffer[idx] != 0.0f)
    {
      std::cerr << "FAIL: expected all-zero output for empty selected-id set.\n";
      return false;
    }
  }
  return true;
}
} // namespace

int
itkTrxGroupTdiMapperTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <output_dir>\n";
    return EXIT_FAILURE;
  }

  const std::string outDir = argv[1];
  if (!MakeDir(outDir))
  {
    std::cerr << "Cannot create output directory: " << outDir << '\n';
    return EXIT_FAILURE;
  }

  const std::string referencePath = outDir + "/reference.nii.gz";
  const std::string trxPath = outDir + "/group_tdi.trx";

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
  guard.paths = { referencePath, trxPath };

  if (!WriteReferenceImage(referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!WriteSyntheticTrxWithMismatchedHeader(trxPath))
  {
    return EXIT_FAILURE;
  }
  if (!RunSumAssertions(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunMeanAssertions(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunMissingGroupFailureCheck(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunSelectedIdsAssertions(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunSelectedIdsGroupIntersectionAssertions(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunEmptySelectedIdsAssertions(trxPath, referencePath))
  {
    return EXIT_FAILURE;
  }
  if (!RunPointToIndexParityAssertions())
  {
    return EXIT_FAILURE;
  }

  std::cout << "All TrxGroupTdiMapper tests passed.\n";
  return EXIT_SUCCESS;
}
