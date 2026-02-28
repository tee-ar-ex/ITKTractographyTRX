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
#include "itkTrxFileReader.h"
#include "itkTrxStreamWriter.h"
#include "itkTranslationTransform.h"

#include "itksys/SystemTools.hxx"

#include <trx/trx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <vector>

namespace
{
bool
EnsureDir(const std::string & path)
{
  if (path.empty())
  {
    return true;
  }
  return itksys::SystemTools::MakeDirectory(path).IsSuccess();
}

void
CleanupPath(const std::string & path)
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

bool
ExpectEqualMatrix(const itk::TrxStreamWriter::MatrixType & lhs, const itk::TrxStreamWriter::MatrixType & rhs)
{
  constexpr double tol = 1e-6;
  for (unsigned int row = 0; row < 4; ++row)
  {
    for (unsigned int col = 0; col < 4; ++col)
    {
      if (std::abs(lhs[row][col] - rhs[row][col]) > tol)
      {
        return false;
      }
    }
  }
  return true;
}

std::vector<itk::Point<double, 3>>
CollectPoints(const itk::TrxStreamlineData::StreamlinePointRange & range)
{
  std::vector<itk::Point<double, 3>> points;
  for (const auto & point : range)
  {
    points.push_back(point);
  }
  return points;
}

size_t
ComputeAabbIntersectionCount(const itk::TrxStreamlineData *           data,
                             const itk::TrxStreamlineData::PointType & minCorner,
                             const itk::TrxStreamlineData::PointType & maxCorner)
{
  if (!data)
  {
    return 0;
  }

  itk::TrxStreamlineData::PointType rasMinInput;
  rasMinInput[0] = -minCorner[0];
  rasMinInput[1] = -minCorner[1];
  rasMinInput[2] = minCorner[2];
  itk::TrxStreamlineData::PointType rasMaxInput;
  rasMaxInput[0] = -maxCorner[0];
  rasMaxInput[1] = -maxCorner[1];
  rasMaxInput[2] = maxCorner[2];

  itk::TrxStreamlineData::PointType rasMin;
  rasMin[0] = std::min(rasMinInput[0], rasMaxInput[0]);
  rasMin[1] = std::min(rasMinInput[1], rasMaxInput[1]);
  rasMin[2] = std::min(rasMinInput[2], rasMaxInput[2]);
  itk::TrxStreamlineData::PointType rasMax;
  rasMax[0] = std::max(rasMinInput[0], rasMaxInput[0]);
  rasMax[1] = std::max(rasMinInput[1], rasMaxInput[1]);
  rasMax[2] = std::max(rasMinInput[2], rasMaxInput[2]);

  constexpr double tol = 1e-6;
  size_t expectedCount = 0;
  const auto totalStreamlines = data->GetNumberOfStreamlines();
  for (size_t i = 0; i < totalStreamlines; ++i)
  {
    const auto range = data->GetStreamlineRange(static_cast<itk::TrxStreamlineData::SizeValueType>(i));
    bool       hasPoint = false;
    float      minX = 0.0f;
    float      minY = 0.0f;
    float      minZ = 0.0f;
    float      maxX = 0.0f;
    float      maxY = 0.0f;
    float      maxZ = 0.0f;
    for (const auto & pointLps : range)
    {
      const float x = static_cast<float>(-pointLps[0]);
      const float y = static_cast<float>(-pointLps[1]);
      const float z = static_cast<float>(pointLps[2]);
      if (!hasPoint)
      {
        minX = maxX = x;
        minY = maxY = y;
        minZ = maxZ = z;
        hasPoint = true;
      }
      else
      {
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
      }
    }
    if (hasPoint &&
        minX <= static_cast<float>(rasMax[0] + tol) && maxX >= static_cast<float>(rasMin[0] - tol) &&
        minY <= static_cast<float>(rasMax[1] + tol) && maxY >= static_cast<float>(rasMin[1] - tol) &&
        minZ <= static_cast<float>(rasMax[2] + tol) && maxZ >= static_cast<float>(rasMin[2] - tol))
    {
      ++expectedCount;
    }
  }
  return expectedCount;
}

bool
WriteAndRead(const std::string &                                     outputPath,
             const std::vector<itk::TrxStreamWriter::StreamlineType> & streamlines,
             const itk::TrxStreamWriter::MatrixType &                ras,
             const itk::TrxStreamWriter::DimensionsType &            dims,
             bool                                                   useCompression,
             bool                                                   expectDirectory,
             itk::TrxStreamlineData::Pointer &                       output)
{
  CleanupPath(outputPath);
  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outputPath);
  writer->SetUseCompression(useCompression);
  writer->SetVoxelToRasMatrix(ras);
  writer->SetDimensions(dims);

  for (const auto & streamline : streamlines)
  {
    writer->PushStreamline(streamline);
  }
  writer->Finalize();

  const bool outputExists = expectDirectory ? itksys::SystemTools::FileIsDirectory(outputPath)
                                            : itksys::SystemTools::FileExists(outputPath, true);
  if (!outputExists)
  {
    std::cerr << "Writer did not create output: " << outputPath << std::endl;
    return false;
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  output = reader->GetOutput();
  return output != nullptr;
}

bool
TestBasicRoundTrip(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 100;
  dims[1] = 101;
  dims[2] = 102;

  itk::TrxStreamWriter::StreamlineType first;
  itk::TrxStreamWriter::StreamlineType second;
  first.reserve(3);
  second.reserve(2);

  const std::array<std::array<double, 3>, 5> points = { { { { 1.0, 2.0, 3.0 } },
                                                          { { 4.0, 5.0, 6.0 } },
                                                          { { 7.0, 8.0, 9.0 } },
                                                          { { 10.0, 11.0, 12.0 } },
                                                          { { 13.0, 14.0, 15.0 } } } };

  for (size_t i = 0; i < points.size(); ++i)
  {
    itk::Point<double, 3> point;
    point[0] = points[i][0];
    point[1] = points[i][1];
    point[2] = points[i][2];
    if (i < 3)
    {
      first.push_back(point);
    }
    else
    {
      second.push_back(point);
    }
  }

  itk::TrxStreamlineData::Pointer output;
  if (!WriteAndRead(basePath, { first, second }, ras, dims, false, true, output))
  {
    return false;
  }

  if (!output->HasFloat32Positions())
  {
    std::cerr << "Expected float32 positions in output." << std::endl;
    return false;
  }
  const auto nbVertices = output->GetNumberOfVertices();
  const auto nbStreamlines = output->GetNumberOfStreamlines();
  if (nbVertices != 5 || nbStreamlines != 2)
  {
    std::cerr << "Unexpected vertex or streamline count. vertices=" << nbVertices
              << " streamlines=" << nbStreamlines << std::endl;
    return false;
  }
  const auto & offsets = output->GetOffsets();
  if (offsets.size() != 2 || offsets[0] != 0 || offsets[1] != 3)
  {
    std::cerr << "Unexpected offsets. size=" << offsets.size();
    if (!offsets.empty())
    {
      std::cerr << " first=" << offsets.front() << " last=" << offsets.back();
    }
    std::cerr << std::endl;
    return false;
  }

  if (!output->HasVoxelToRasMatrix() || !output->HasVoxelToLpsMatrix())
  {
    std::cerr << "Expected voxel to ras/lps matrices." << std::endl;
    return false;
  }
  if (!ExpectEqualMatrix(output->GetVoxelToRasMatrix(), ras))
  {
    std::cerr << "VoxelToRas matrix mismatch." << std::endl;
    return false;
  }
  if (!output->HasDimensions())
  {
    std::cerr << "Expected dimensions." << std::endl;
    return false;
  }
  const auto & outDims = output->GetDimensions();
  if (outDims[0] != dims[0] || outDims[1] != dims[1] || outDims[2] != dims[2])
  {
    std::cerr << "Dimensions mismatch." << std::endl;
    return false;
  }

  const auto * outputPositions = output->GetFloat32Positions();
  if (outputPositions == nullptr || outputPositions->size() != 15)
  {
    std::cerr << "Unexpected positions size." << std::endl;
    return false;
  }
  for (size_t index = 0; index < points.size(); ++index)
  {
    const auto base = index * 3;
    if ((*outputPositions)[base] != static_cast<float>(points[index][0]) ||
        (*outputPositions)[base + 1] != static_cast<float>(points[index][1]) ||
        (*outputPositions)[base + 2] != static_cast<float>(points[index][2]))
    {
      std::cerr << "Position mismatch at index " << index << std::endl;
      return false;
    }
  }

  return true;
}

bool
TestSubsetAndQuery(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 1;
  dims[1] = 1;
  dims[2] = 1;

  itk::TrxStreamWriter::StreamlineType first;
  itk::TrxStreamWriter::StreamlineType second;
  first.reserve(2);
  second.reserve(2);

  itk::Point<double, 3> p0;
  p0[0] = 1.0;
  p0[1] = 1.0;
  p0[2] = 1.0;
  itk::Point<double, 3> p1;
  p1[0] = 2.0;
  p1[1] = 2.0;
  p1[2] = 2.0;
  itk::Point<double, 3> p2;
  p2[0] = 10.0;
  p2[1] = 10.0;
  p2[2] = 10.0;
  itk::Point<double, 3> p3;
  p3[0] = 11.0;
  p3[1] = 11.0;
  p3[2] = 11.0;

  first.push_back(p0);
  first.push_back(p1);
  second.push_back(p2);
  second.push_back(p3);

  itk::TrxStreamlineData::Pointer output;
  if (!WriteAndRead(basePath, { first, second }, ras, dims, false, true, output))
  {
    return false;
  }

  auto subset = output->SubsetStreamlines({ 1 });
  if (!subset || subset->GetNumberOfStreamlines() != 1 || subset->GetNumberOfVertices() != 2)
  {
    std::cerr << "Subset did not return expected counts." << std::endl;
    return false;
  }

  const auto subsetPoints = CollectPoints(subset->GetStreamlineRange(0));
  if (subsetPoints.size() != 2 ||
      subsetPoints[0][0] != p2[0] || subsetPoints[0][1] != p2[1] || subsetPoints[0][2] != p2[2] ||
      subsetPoints[1][0] != p3[0] || subsetPoints[1][1] != p3[1] || subsetPoints[1][2] != p3[2])
  {
    std::cerr << "Subset points mismatch." << std::endl;
    return false;
  }

  DataType::PointType minCorner;
  minCorner[0] = 0.0;
  minCorner[1] = 0.0;
  minCorner[2] = 0.0;
  DataType::PointType maxCorner;
  maxCorner[0] = 3.0;
  maxCorner[1] = 3.0;
  maxCorner[2] = 3.0;
  auto query = output->QueryAabb(minCorner, maxCorner);
  if (!query || query->GetNumberOfStreamlines() != 1 || query->GetNumberOfVertices() != 2)
  {
    std::cerr << "QueryAabb did not return expected counts." << std::endl;
    return false;
  }

  return true;
}

bool
TestWriterMetadataRoundTrip(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;
  const std::string outputPath = basePath + ".trx";

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 5;
  dims[1] = 6;
  dims[2] = 7;

  itk::TrxStreamWriter::StreamlineType first;
  itk::TrxStreamWriter::StreamlineType second;
  first.reserve(2);
  second.reserve(1);
  itk::Point<double, 3> p0;
  p0[0] = 0.0;
  p0[1] = 1.0;
  p0[2] = 2.0;
  itk::Point<double, 3> p1;
  p1[0] = 3.0;
  p1[1] = 4.0;
  p1[2] = 5.0;
  itk::Point<double, 3> p2;
  p2[0] = 6.0;
  p2[1] = 7.0;
  p2[2] = 8.0;
  first.push_back(p0);
  first.push_back(p1);
  second.push_back(p2);

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outputPath);
  writer->SetUseCompression(true);
  writer->SetVoxelToRasMatrix(ras);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("weight", "float32");
  writer->RegisterDpvField("color", "float32");

  writer->PushStreamline(first, { { "weight", 0.25 } }, { { "color", { 1.0, 2.0 } } }, { "GroupA" });
  writer->PushStreamline(second, { { "weight", 0.75 } }, { { "color", { 3.0 } } }, {});
  writer->Finalize();

  trx::TrxReader<float> reader(outputPath);
  auto *                trxFile = reader.get();
  if (!trxFile)
  {
    std::cerr << "Failed to read trx output." << std::endl;
    return false;
  }
  if (trxFile->data_per_streamline.find("weight") == trxFile->data_per_streamline.end())
  {
    std::cerr << "Missing DPS field in trx output." << std::endl;
    return false;
  }
  if (trxFile->data_per_vertex.find("color") == trxFile->data_per_vertex.end())
  {
    std::cerr << "Missing DPV field in trx output." << std::endl;
    return false;
  }
  if (trxFile->groups.find("GroupA") == trxFile->groups.end())
  {
    std::cerr << "Missing GroupA in trx output." << std::endl;
    return false;
  }
  if (trxFile->groups["GroupA"]->_matrix.rows() != 1)
  {
    std::cerr << "Unexpected GroupA size." << std::endl;
    return false;
  }

  return true;
}

bool
TestSimulatedStreamWriter(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;
  constexpr size_t kStreamlineCount = 100;
  constexpr size_t kGroups = 10;

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 1;
  dims[1] = 1;
  dims[2] = 1;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(basePath);
  writer->SetUseCompression(false);
  writer->SetVoxelToRasMatrix(ras);
  writer->SetDimensions(dims);
  writer->RegisterDpsField("shape", "float32");
  writer->RegisterDpvField("scalar", "float32");
  writer->SetPositionsBufferMaxBytes(16 * 1024);

  std::mt19937 rng(1337);
  std::uniform_int_distribution<int> lengthDist(10, 200);
  std::uniform_real_distribution<double> valueDist(0.0, 1.0);

  size_t totalVertices = 0;
  std::array<size_t, kGroups> groupCounts{};
  for (size_t i = 0; i < kStreamlineCount; ++i)
  {
    const int length = lengthDist(rng);
    totalVertices += static_cast<size_t>(length);

    itk::TrxStreamWriter::StreamlineType points;
    points.reserve(static_cast<size_t>(length));
    std::vector<double> dpv;
    dpv.reserve(static_cast<size_t>(length));

    for (int v = 0; v < length; ++v)
    {
      itk::Point<double, 3> point;
      point[0] = valueDist(rng);
      point[1] = valueDist(rng);
      point[2] = valueDist(rng);
      points.push_back(point);
      dpv.push_back(valueDist(rng));
    }

    const double shape = valueDist(rng);
    const size_t groupId = i / (kStreamlineCount / kGroups);
    const std::string groupName = std::string("Group") + std::to_string(groupId);
    writer->PushStreamline(points, { { "shape", shape } }, { { "scalar", dpv } }, { groupName });
    ++groupCounts[groupId];
  }

  writer->Finalize();

  trx::TrxReader<float> reader(basePath);
  auto *                trxFile = reader.get();
  if (!trxFile)
  {
    std::cerr << "Failed to read simulated TRX output." << std::endl;
    return false;
  }

  if (trxFile->num_streamlines() != kStreamlineCount || trxFile->num_vertices() != totalVertices)
  {
    std::cerr << "Unexpected counts in simulated TRX output. streamlines=" << trxFile->num_streamlines()
              << " vertices=" << trxFile->num_vertices() << std::endl;
    return false;
  }

  const auto dpsIt = trxFile->data_per_streamline.find("shape");
  if (dpsIt == trxFile->data_per_streamline.end() || dpsIt->second->_matrix.rows() != kStreamlineCount)
  {
    std::cerr << "Unexpected DPS shape data." << std::endl;
    return false;
  }

  const auto dpvIt = trxFile->data_per_vertex.find("scalar");
  if (dpvIt == trxFile->data_per_vertex.end() ||
      static_cast<size_t>(dpvIt->second->_data.rows()) != totalVertices ||
      dpvIt->second->_data.cols() != 1)
  {
    std::cerr << "Unexpected DPV scalar data." << std::endl;
    return false;
  }

  for (size_t g = 0; g < kGroups; ++g)
  {
    const std::string groupName = std::string("Group") + std::to_string(g);
    const auto        groupIt = trxFile->groups.find(groupName);
    if (groupIt == trxFile->groups.end())
    {
      std::cerr << "Missing group " << groupName << std::endl;
      return false;
    }
    if (static_cast<size_t>(groupIt->second->_matrix.rows()) != groupCounts[g])
    {
      std::cerr << "Unexpected group size for " << groupName << std::endl;
      return false;
    }
  }

  return true;
}

bool
TestAabbQueryManyStreamlines(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;
  constexpr size_t kStreamlineCount = 1000;
  constexpr size_t kInsideCount = 250;
  constexpr size_t kPointsPerStreamline = 5;

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 1;
  dims[1] = 1;
  dims[2] = 1;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(basePath);
  writer->SetUseCompression(false);
  writer->SetVoxelToRasMatrix(ras);
  writer->SetDimensions(dims);
  writer->SetPositionsBufferMaxBytes(32 * 1024);

  for (size_t i = 0; i < kStreamlineCount; ++i)
  {
    itk::TrxStreamWriter::StreamlineType points;
    points.reserve(kPointsPerStreamline);

    const bool inside = i < kInsideCount;
    for (size_t p = 0; p < kPointsPerStreamline; ++p)
    {
      itk::Point<double, 3> point;
      if (inside)
      {
        point[0] = -0.8 + 0.05 * static_cast<double>(p);
        point[1] = 0.3 + 0.1 * static_cast<double>(p);
        point[2] = 0.1 + 0.05 * static_cast<double>(p);
      }
      else
      {
        point[0] = 0.0;
        point[1] = 0.0;
        point[2] = -1000.0 - static_cast<double>(i);
      }
      points.push_back(point);
    }
    writer->PushStreamline(points);
  }

  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(basePath);
  reader->Update();

  auto output = reader->GetOutput();
  if (!output)
  {
    std::cerr << "Failed to read AABB test output." << std::endl;
    return false;
  }

  DataType::PointType minCorner;
  minCorner[0] = -0.9;
  minCorner[1] = 0.2;
  minCorner[2] = 0.05;
  DataType::PointType maxCorner;
  maxCorner[0] = -0.1;
  maxCorner[1] = 1.1;
  maxCorner[2] = 0.55;

  auto subset = output->QueryAabb(minCorner, maxCorner);
  if (!subset)
  {
    std::cerr << "QueryAabb returned null output." << std::endl;
    return false;
  }

  DataType::PointType rasMinInput;
  rasMinInput[0] = -minCorner[0];
  rasMinInput[1] = -minCorner[1];
  rasMinInput[2] = minCorner[2];
  DataType::PointType rasMaxInput;
  rasMaxInput[0] = -maxCorner[0];
  rasMaxInput[1] = -maxCorner[1];
  rasMaxInput[2] = maxCorner[2];
  DataType::PointType rasMin;
  rasMin[0] = std::min(rasMinInput[0], rasMaxInput[0]);
  rasMin[1] = std::min(rasMinInput[1], rasMaxInput[1]);
  rasMin[2] = std::min(rasMinInput[2], rasMaxInput[2]);
  DataType::PointType rasMax;
  rasMax[0] = std::max(rasMinInput[0], rasMaxInput[0]);
  rasMax[1] = std::max(rasMinInput[1], rasMaxInput[1]);
  rasMax[2] = std::max(rasMinInput[2], rasMaxInput[2]);

  const size_t expectedCount = ComputeAabbIntersectionCount(output, minCorner, maxCorner);

  const auto subsetStreamlines = subset->GetNumberOfStreamlines();
  const size_t diff = subsetStreamlines > expectedCount ? subsetStreamlines - expectedCount
                                                        : expectedCount - subsetStreamlines;
  if (diff > 1)
  {
    std::cerr << "Unexpected AABB query count. got=" << subsetStreamlines
              << " expected=" << expectedCount << std::endl;
    return false;
  }

  const size_t cap = 10;
  auto capped = output->QueryAabb(minCorner, maxCorner, false, cap, 123);
  if (!capped)
  {
    std::cerr << "Capped QueryAabb returned null output." << std::endl;
    return false;
  }
  const size_t cappedCount = capped->GetNumberOfStreamlines();
  const size_t expectedCapped = std::min(expectedCount, cap);
  if (cappedCount != expectedCapped)
  {
    std::cerr << "Capped AABB query count mismatch. got=" << cappedCount
              << " expected=" << expectedCapped << std::endl;
    return false;
  }

  return true;
}

bool
TestAabbQueryWithTransforms(const std::string & basePath)
{
  using DataType = itk::TrxStreamlineData;
  constexpr size_t kStreamlineCount = 200;
  constexpr size_t kPointsPerStreamline = 6;

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 1;
  dims[1] = 1;
  dims[2] = 1;

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(basePath);
  writer->SetUseCompression(false);
  writer->SetVoxelToRasMatrix(ras);
  writer->SetDimensions(dims);

  for (size_t i = 0; i < kStreamlineCount; ++i)
  {
    itk::TrxStreamWriter::StreamlineType points;
    points.reserve(kPointsPerStreamline);
    for (size_t p = 0; p < kPointsPerStreamline; ++p)
    {
      itk::Point<double, 3> point;
      point[0] = -8.0 + static_cast<double>(p);
      point[1] = 3.0 + static_cast<double>(p);
      point[2] = 1.0 + static_cast<double>(p);
      points.push_back(point);
    }
    writer->PushStreamline(points);
  }
  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(basePath);
  reader->Update();
  auto original = reader->GetOutput();
  if (!original)
  {
    std::cerr << "Failed to read transform test output." << std::endl;
    return false;
  }

  DataType::PointType minCorner;
  minCorner[0] = -9.0;
  minCorner[1] = 2.0;
  minCorner[2] = 0.0;
  DataType::PointType maxCorner;
  maxCorner[0] = -2.0;
  maxCorner[1] = 9.0;
  maxCorner[2] = 7.0;

  const size_t expectedBefore = ComputeAabbIntersectionCount(original, minCorner, maxCorner);
  const auto   subsetBefore = original->QueryAabb(minCorner, maxCorner);
  if (!subsetBefore || subsetBefore->GetNumberOfStreamlines() != expectedBefore)
  {
    std::cerr << "Unexpected AABB count before transform." << std::endl;
    return false;
  }

  using TransformType = itk::TranslationTransform<double, 3>;
  auto transform = TransformType::New();
  TransformType::OutputVectorType translation;
  translation[0] = 2.0;
  translation[1] = -1.0;
  translation[2] = 3.0;
  transform->Translate(translation);

  original->TransformInPlace(transform.GetPointer());
  const size_t expectedAfter = ComputeAabbIntersectionCount(original, minCorner, maxCorner);
  const auto   subsetAfter = original->QueryAabb(minCorner, maxCorner);
  if (!subsetAfter || subsetAfter->GetNumberOfStreamlines() != expectedAfter)
  {
    std::cerr << "Unexpected AABB count after in-place transform." << std::endl;
    return false;
  }

  const std::string transformedPath = basePath + "_xf";
  CleanupPath(transformedPath);
  auto writer2 = itk::TrxStreamWriter::New();
  writer2->SetFileName(transformedPath);
  writer2->SetUseCompression(false);
  writer2->SetVoxelToRasMatrix(ras);
  writer2->SetDimensions(dims);

  auto readerOriginal = itk::TrxFileReader::New();
  readerOriginal->SetFileName(basePath);
  readerOriginal->Update();
  auto originalForStream = readerOriginal->GetOutput();
  if (!originalForStream)
  {
    std::cerr << "Failed to re-read original output for streamed transform." << std::endl;
    return false;
  }

  const auto totalStreamlines = originalForStream->GetNumberOfStreamlines();
  for (size_t i = 0; i < totalStreamlines; ++i)
  {
    itk::TrxStreamWriter::StreamlineType points;
    const auto range = originalForStream->GetStreamlineRange(static_cast<DataType::SizeValueType>(i));
    for (const auto & point : range)
    {
      const auto transformed = transform->TransformPoint(point);
      points.push_back(transformed);
    }
    writer2->PushStreamline(points);
  }
  writer2->Finalize();

  auto reader2 = itk::TrxFileReader::New();
  reader2->SetFileName(transformedPath);
  reader2->Update();
  auto transformedData = reader2->GetOutput();
  if (!transformedData)
  {
    std::cerr << "Failed to read transformed output." << std::endl;
    return false;
  }

  const size_t expectedStreamed = ComputeAabbIntersectionCount(transformedData, minCorner, maxCorner);
  const auto   subsetStreamed = transformedData->QueryAabb(minCorner, maxCorner);
  if (!subsetStreamed || subsetStreamed->GetNumberOfStreamlines() != expectedStreamed)
  {
    std::cerr << "Unexpected AABB count after streamed transform." << std::endl;
    return false;
  }

  return true;
}
} // namespace

int
itkTrxWrapperTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " output_base_path" << std::endl;
    return EXIT_FAILURE;
  }

  const std::string basePath = argv[1];
  const std::string outputDir = itksys::SystemTools::GetFilenamePath(basePath);
  if (!EnsureDir(outputDir))
  {
    std::cerr << "Failed to create output directory: " << outputDir << std::endl;
    return EXIT_FAILURE;
  }

  if (!TestBasicRoundTrip(basePath + "_basic"))
  {
    return EXIT_FAILURE;
  }
  if (!TestSubsetAndQuery(basePath + "_subset"))
  {
    return EXIT_FAILURE;
  }
  if (!TestWriterMetadataRoundTrip(basePath + "_meta"))
  {
    return EXIT_FAILURE;
  }
  if (!TestSimulatedStreamWriter(basePath + "_simulated"))
  {
    return EXIT_FAILURE;
  }
  if (!TestAabbQueryManyStreamlines(basePath + "_aabb"))
  {
    return EXIT_FAILURE;
  }
  if (!TestAabbQueryWithTransforms(basePath + "_aabb_xf"))
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
