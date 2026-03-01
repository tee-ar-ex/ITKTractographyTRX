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

#include "itksys/SystemTools.hxx"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string
GetTestDataRoot()
{
  const char * env = std::getenv("TRX_TEST_DATA_DIR");
  if (env == nullptr || std::string(env).empty())
  {
    return {};
  }
  return std::string(env);
}

fs::path
ResolveGoldStandardDir(const std::string & root)
{
  const fs::path rootPath(root);
  const fs::path gsDir = rootPath / "gold_standard";
  if (fs::exists(gsDir))
  {
    return gsDir;
  }
  return rootPath;
}

bool
EnsureGoldStandardFiles(const fs::path & gsDir)
{
  const fs::path gsTrx = gsDir / "gs.trx";
  const fs::path gsDirTrx = gsDir / "gs_fldr.trx";
  const fs::path coords = gsDir / "gs_rasmm_space.txt";
  if (!fs::exists(gsTrx) || !fs::exists(gsDirTrx) || !fs::exists(coords))
  {
    std::cerr << "Missing gold standard files under " << gsDir << std::endl;
    return false;
  }
  return true;
}

std::vector<itk::Point<double, 3>>
LoadRasmmCoords(const fs::path & path)
{
  std::ifstream in(path.string());
  if (!in.is_open())
  {
    throw std::runtime_error("Failed to open coordinate file: " + path.string());
  }

  std::vector<double> values;
  double              v;
  while (in >> v)
  {
    values.push_back(v);
  }
  if (values.size() % 3 != 0)
  {
    throw std::runtime_error("Coordinate file does not contain triples of floats.");
  }

  const size_t count = values.size() / 3;
  std::vector<itk::Point<double, 3>> coords;
  coords.reserve(count);
  for (size_t i = 0; i < count; ++i)
  {
    itk::Point<double, 3> point;
    point[0] = values[i * 3 + 0];
    point[1] = values[i * 3 + 1];
    point[2] = values[i * 3 + 2];
    coords.push_back(point);
  }
  return coords;
}

std::vector<itk::Point<double, 3>>
LoadTrxAsRasPoints(const fs::path & path)
{
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(path.string());
  reader->Update();

  const auto * output = reader->GetOutput();
  if (!output)
  {
    throw std::runtime_error("Failed to read TRX data: " + path.string());
  }

  std::vector<itk::Point<double, 3>> points;
  points.reserve(static_cast<size_t>(output->GetNumberOfVertices()));

  const auto numStreamlines = output->GetNumberOfStreamlines();
  for (size_t i = 0; i < numStreamlines; ++i)
  {
    const auto range = output->GetStreamlineRange(static_cast<itk::SizeValueType>(i));
    for (const auto & pointLps : range)
    {
      itk::Point<double, 3> pointRas;
      pointRas[0] = -pointLps[0];
      pointRas[1] = -pointLps[1];
      pointRas[2] = pointLps[2];
      points.push_back(pointRas);
    }
  }
  return points;
}

bool
CompareSamples(const std::vector<itk::Point<double, 3>> & actual,
               const std::vector<itk::Point<double, 3>> & expected,
               double                                      tol)
{
  if (actual.size() != expected.size())
  {
    std::cerr << "Point count mismatch: " << actual.size() << " vs " << expected.size() << std::endl;
    return false;
  }

  const size_t total = actual.size();
  const size_t sampleCount = std::min<size_t>(50, total);

  auto checkIndex = [&](size_t idx) -> bool {
    for (unsigned int dim = 0; dim < 3; ++dim)
    {
      const double diff = std::abs(actual[idx][dim] - expected[idx][dim]);
      if (diff > tol)
      {
        std::cerr << "Point mismatch at index " << idx << " dim " << dim << " diff " << diff << std::endl;
        return false;
      }
    }
    return true;
  };

  for (size_t i = 0; i < sampleCount; ++i)
  {
    if (!checkIndex(i))
    {
      return false;
    }
  }

  for (size_t i = 0; i < sampleCount; ++i)
  {
    const size_t idx = total - 1 - i;
    if (!checkIndex(idx))
    {
      return false;
    }
  }

  return true;
}
} // namespace

int
itkTrxGoldStandardTest(int, char *[])
{
  const std::string root = GetTestDataRoot();
  if (root.empty())
  {
    std::cerr << "TRX_TEST_DATA_DIR is not set." << std::endl;
    return EXIT_FAILURE;
  }

  const fs::path gsDir = ResolveGoldStandardDir(root);
  if (!EnsureGoldStandardFiles(gsDir))
  {
    return EXIT_FAILURE;
  }

  try
  {
    const auto expected = LoadRasmmCoords(gsDir / "gs_rasmm_space.txt");
    const auto fromZip = LoadTrxAsRasPoints(gsDir / "gs.trx");
    const auto fromDir = LoadTrxAsRasPoints(gsDir / "gs_fldr.trx");

    if (expected.empty())
    {
      std::cerr << "Gold standard coordinate file is empty." << std::endl;
      return EXIT_FAILURE;
    }

    if (!CompareSamples(fromZip, expected, 1e-4))
    {
      return EXIT_FAILURE;
    }
    if (!CompareSamples(fromDir, expected, 1e-4))
    {
      return EXIT_FAILURE;
    }
  }
  catch (const std::exception & ex)
  {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
