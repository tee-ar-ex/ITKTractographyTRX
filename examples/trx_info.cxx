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
#include "itkTrxGroup.h"
#include "itkTrxStreamlineData.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void
PrintUsage(const char * exe)
{
  std::cerr << "Usage:\n"
            << "  " << exe << " --input <tractogram.trx_or_dir>\n";
}

const char *
CoordinateTypeToString(itk::TrxStreamlineData::CoordinateType ct)
{
  switch (ct)
  {
    case itk::TrxStreamlineData::CoordinateType::Float16:
      return "float16";
    case itk::TrxStreamlineData::CoordinateType::Float32:
      return "float32";
    case itk::TrxStreamlineData::CoordinateType::Float64:
      return "float64";
    default:
      return "unknown";
  }
}

const char *
CoordinateSystemToString(itk::TrxStreamlineData::CoordinateSystem cs)
{
  switch (cs)
  {
    case itk::TrxStreamlineData::CoordinateSystem::RAS:
      return "RAS";
    case itk::TrxStreamlineData::CoordinateSystem::LPS:
      return "LPS";
    default:
      return "unknown";
  }
}

void
PrintMatrix4x4(const itk::TrxStreamlineData::MatrixType & mat)
{
  std::cout << std::fixed << std::setprecision(6);
  for (unsigned int r = 0; r < 4; ++r)
  {
    std::cout << "    [";
    for (unsigned int c = 0; c < 4; ++c)
    {
      std::cout << std::setw(11) << mat[r][c];
      if (c + 1 < 4)
      {
        std::cout << " ";
      }
    }
    std::cout << "]\n";
  }
}

void
PrintDpsSummary(const itk::TrxStreamlineData * data)
{
  const auto dpsNames = data->GetDpsFieldNames();
  const auto nStreamlines = static_cast<size_t>(data->GetNumberOfStreamlines());
  std::cout << "DPS fields (" << dpsNames.size() << "):\n";
  for (const auto & name : dpsNames)
  {
    const auto values = data->GetDpsField(name);
    size_t nCols = 0;
    if (nStreamlines > 0 && values.size() % nStreamlines == 0)
    {
      nCols = values.size() / nStreamlines;
    }
    std::cout << "  - " << name << ": values=" << values.size();
    if (nCols > 0)
    {
      std::cout << " (" << nCols << " col)";
    }
    std::cout << "\n";
  }
}

void
PrintDpvSummary(const itk::TrxStreamlineData * data)
{
  const auto dpvNames = data->GetDpvFieldNames();
  const auto nVertices = static_cast<size_t>(data->GetNumberOfVertices());
  std::cout << "DPV fields (" << dpvNames.size() << "):\n";
  for (const auto & name : dpvNames)
  {
    const auto values = data->GetDpvField(name);
    size_t nCols = 0;
    if (nVertices > 0 && values.size() % nVertices == 0)
    {
      nCols = values.size() / nVertices;
    }
    std::cout << "  - " << name << ": values=" << values.size();
    if (nCols > 0)
    {
      std::cout << " (" << nCols << " col)";
    }
    std::cout << "\n";
  }
}

void
PrintGroupSummary(const itk::TrxStreamlineData * data)
{
  const auto groupNames = data->GetGroupNames();
  std::cout << "Groups (" << groupNames.size() << "):\n";
  for (const auto & name : groupNames)
  {
    auto group = data->GetGroup(name);
    const size_t count = group ? group->GetStreamlineIndices().size() : 0;
    std::cout << "  - " << name << ": " << count << " streamlines\n";
  }
}
} // namespace

int
main(int argc, char * argv[])
{
  std::string inputPath;
  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h")
    {
      PrintUsage(argv[0]);
      return EXIT_SUCCESS;
    }
    if (arg == "--input")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "Missing value for --input\n";
        return EXIT_FAILURE;
      }
      inputPath = argv[++i];
    }
    else
    {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (inputPath.empty())
  {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  try
  {
    auto reader = itk::TrxFileReader::New();
    reader->SetFileName(inputPath);
    reader->Update();
    auto data = reader->GetOutput();
    if (!data)
    {
      std::cerr << "Failed to load TRX data from: " << inputPath << "\n";
      return EXIT_FAILURE;
    }

    std::cout << "TRX info: " << inputPath << "\n\n";
    std::cout << "Streamlines: " << data->GetNumberOfStreamlines() << "\n";
    std::cout << "Vertices:    " << data->GetNumberOfVertices() << "\n";
    std::cout << "Coord dtype: " << CoordinateTypeToString(data->GetCoordinateType()) << "\n";
    std::cout << "Coord space: " << CoordinateSystemToString(data->GetCoordinateSystem()) << "\n";

    if (data->HasDimensions())
    {
      const auto dims = data->GetDimensions();
      std::cout << "Dims:        [" << dims[0] << ", " << dims[1] << ", " << dims[2] << "]\n";
    }
    else
    {
      std::cout << "Dims:        <none>\n";
    }

    if (data->HasVoxelToRasMatrix())
    {
      std::cout << "\nVoxelToRAS:\n";
      PrintMatrix4x4(data->GetVoxelToRasMatrix());
    }
    if (data->HasVoxelToLpsMatrix())
    {
      std::cout << "\nVoxelToLPS:\n";
      PrintMatrix4x4(data->GetVoxelToLpsMatrix());
    }

    std::cout << "\n";
    PrintDpsSummary(data);
    PrintDpvSummary(data);
    PrintGroupSummary(data);
  }
  catch (const itk::ExceptionObject & e)
  {
    std::cerr << "ITK error: " << e << "\n";
    return EXIT_FAILURE;
  }
  catch (const std::exception & e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
