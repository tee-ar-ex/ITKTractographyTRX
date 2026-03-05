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
 * Example CLI for Group TDI mapping.
 *
 * Maps one TRX group to a 3D NIfTI image on a caller-provided reference grid.
 */

#include "itkImageFileWriter.h"
#include "itkNiftiImageIO.h"
#include "itkTrxFileReader.h"
#include "itkTrxGroupTdiMapper.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void
PrintUsage(const char * exe)
{
  std::cerr << "Usage:\n"
            << "  " << exe
            << " --input <tractogram.trx>"
               " --reference <reference_3d.nii.gz>"
               " --output <out_tdi.nii.gz>"
               " [--group <group_name>]"
               " [--groups-all-of <g1,g2,...>]"
               " [--groups-any-of <g3,g4,...>]"
               " [--groups-none-of <g5,g6,...>]"
               " [--stat sum|mean]"
               " [--weight-field <dps_field_name>]\n\n"
            << "Notes:\n"
            << "  - Mapping uses TRX streamline coordinates in native RAS space.\n"
            << "  - Voxel assignment uses only the reference NIfTI geometry.\n"
            << "  - Selection logic: (--group AND all-of) AND (any-of OR none) AND NOT(none-of).\n";
}

bool
ReadArg(int & i, int argc, char * argv[], std::string & value)
{
  if (i + 1 >= argc)
  {
    return false;
  }
  value = argv[++i];
  return true;
}

std::vector<std::string>
SplitCsv(const std::string & csv)
{
  std::vector<std::string> out;
  std::string current;
  for (char ch : csv)
  {
    if (ch == ',')
    {
      if (!current.empty())
      {
        out.push_back(current);
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty())
  {
    out.push_back(current);
  }
  return out;
}

bool
ContainsName(const std::vector<std::string> & names, const std::string & name)
{
  for (const auto & n : names)
  {
    if (n == name)
    {
      return true;
    }
  }
  return false;
}

std::vector<uint8_t>
BuildGroupMask(const itk::TrxStreamlineData * data, const std::string & groupName, size_t nStreamlines)
{
  std::vector<uint8_t> mask(nStreamlines, 0);
  auto group = data->GetGroup(groupName);
  if (!group)
  {
    return mask;
  }
  for (uint32_t idx : group->GetStreamlineIndices())
  {
    if (idx < nStreamlines)
    {
      mask[idx] = 1;
    }
  }
  return mask;
}

std::vector<uint32_t>
SelectStreamlines(const itk::TrxStreamlineData *   data,
                  const std::vector<std::string> & allOfGroups,
                  const std::vector<std::string> & anyOfGroups)
{
  const size_t nStreamlines = static_cast<size_t>(data->GetNumberOfStreamlines());
  std::vector<uint8_t> allMask(nStreamlines, allOfGroups.empty() ? 1 : 0);
  std::vector<uint8_t> anyMask(nStreamlines, anyOfGroups.empty() ? 1 : 0);

  for (size_t g = 0; g < allOfGroups.size(); ++g)
  {
    const auto groupMask = BuildGroupMask(data, allOfGroups[g], nStreamlines);
    if (g == 0)
    {
      allMask = groupMask;
    }
    else
    {
      for (size_t i = 0; i < nStreamlines; ++i)
      {
        allMask[i] = static_cast<uint8_t>(allMask[i] && groupMask[i]);
      }
    }
  }

  if (!anyOfGroups.empty())
  {
    std::fill(anyMask.begin(), anyMask.end(), 0);
    for (const auto & name : anyOfGroups)
    {
      const auto groupMask = BuildGroupMask(data, name, nStreamlines);
      for (size_t i = 0; i < nStreamlines; ++i)
      {
        anyMask[i] = static_cast<uint8_t>(anyMask[i] || groupMask[i]);
      }
    }
  }

  std::vector<uint32_t> selected;
  selected.reserve(nStreamlines);
  for (size_t i = 0; i < nStreamlines; ++i)
  {
    if (allMask[i] && anyMask[i])
    {
      selected.push_back(static_cast<uint32_t>(i));
    }
  }
  return selected;
}

} // namespace

int
main(int argc, char * argv[])
{
  std::string inputPath;
  std::string groupName;
  std::string groupsAllOfCsv;
  std::string groupsAnyOfCsv;
  std::string groupsNoneOfCsv;
  std::string referencePath;
  std::string outputPath;
  std::string stat = "sum";
  std::string weightFieldName;

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
      if (!ReadArg(i, argc, argv, inputPath))
      {
        std::cerr << "Missing value for --input\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--group")
    {
      if (!ReadArg(i, argc, argv, groupName))
      {
        std::cerr << "Missing value for --group\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--groups-all-of")
    {
      if (!ReadArg(i, argc, argv, groupsAllOfCsv))
      {
        std::cerr << "Missing value for --groups-all-of\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--groups-any-of")
    {
      if (!ReadArg(i, argc, argv, groupsAnyOfCsv))
      {
        std::cerr << "Missing value for --groups-any-of\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--groups-none-of")
    {
      if (!ReadArg(i, argc, argv, groupsNoneOfCsv))
      {
        std::cerr << "Missing value for --groups-none-of\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--reference")
    {
      if (!ReadArg(i, argc, argv, referencePath))
      {
        std::cerr << "Missing value for --reference\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--output")
    {
      if (!ReadArg(i, argc, argv, outputPath))
      {
        std::cerr << "Missing value for --output\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--stat")
    {
      if (!ReadArg(i, argc, argv, stat))
      {
        std::cerr << "Missing value for --stat\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--weight-field")
    {
      if (!ReadArg(i, argc, argv, weightFieldName))
      {
        std::cerr << "Missing value for --weight-field\n";
        return EXIT_FAILURE;
      }
    }
    else
    {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (inputPath.empty() || referencePath.empty() || outputPath.empty())
  {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  std::vector<std::string> allOfGroups = SplitCsv(groupsAllOfCsv);
  std::vector<std::string> anyOfGroups = SplitCsv(groupsAnyOfCsv);
  std::vector<std::string> noneOfGroups = SplitCsv(groupsNoneOfCsv);
  if (!groupName.empty())
  {
    allOfGroups.push_back(groupName);
  }
  if (allOfGroups.empty() && anyOfGroups.empty() && noneOfGroups.empty())
  {
    std::cerr << "Specify --group and/or --groups-all-of/--groups-any-of/--groups-none-of.\n";
    return EXIT_FAILURE;
  }

  itk::TrxGroupTdiMapper::Options options;
  if (stat == "sum")
  {
    options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
  }
  else if (stat == "mean")
  {
    options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Mean;
  }
  else
  {
    std::cerr << "Invalid --stat value: " << stat << " (expected sum|mean)\n";
    return EXIT_FAILURE;
  }

  if (!weightFieldName.empty())
  {
    options.useDpsWeightField = true;
    options.weightFieldName = weightFieldName;
  }

  try
  {
    auto reader = itk::TrxFileReader::New();
    reader->SetFileName(inputPath);
    reader->Update();
    auto data = reader->GetOutput();
    const auto existingGroups = data->GetGroupNames();
    for (const auto & name : allOfGroups)
    {
      if (!ContainsName(existingGroups, name))
      {
        std::cerr << "Group not found for all-of selector: " << name << "\n";
        return EXIT_FAILURE;
      }
    }
    for (const auto & name : anyOfGroups)
    {
      if (!ContainsName(existingGroups, name))
      {
        std::cerr << "Group not found for any-of selector: " << name << "\n";
        return EXIT_FAILURE;
      }
    }
    for (const auto & name : noneOfGroups)
    {
      if (!ContainsName(existingGroups, name))
      {
        std::cerr << "Group not found for none-of selector: " << name << "\n";
        return EXIT_FAILURE;
      }
    }
    auto selected = SelectStreamlines(data, allOfGroups, anyOfGroups);
    if (!noneOfGroups.empty())
    {
      const size_t nStreamlines = static_cast<size_t>(data->GetNumberOfStreamlines());
      std::vector<uint8_t> excludeMask(nStreamlines, 0);
      for (const auto & name : noneOfGroups)
      {
        const auto mask = BuildGroupMask(data, name, nStreamlines);
        for (size_t i = 0; i < nStreamlines; ++i)
        {
          excludeMask[i] = static_cast<uint8_t>(excludeMask[i] || mask[i]);
        }
      }
      std::vector<uint32_t> filtered;
      filtered.reserve(selected.size());
      for (const auto idx : selected)
      {
        if (idx < nStreamlines && !excludeMask[idx])
        {
          filtered.push_back(idx);
        }
      }
      selected.swap(filtered);
    }

    auto mapper = itk::TrxGroupTdiMapper::New();
    mapper->SetInput(data);
    mapper->SetReferenceImageFileName(referencePath);
    mapper->SetOptions(options);
    mapper->SetSelectedStreamlineIds(selected);
    mapper->Update();
    auto outputImage = mapper->GetOutput();

    using WriterType = itk::ImageFileWriter<itk::TrxGroupTdiMapper::OutputImageType>;
    auto io = itk::NiftiImageIO::New();
    auto writer = WriterType::New();
    writer->SetImageIO(io);
    writer->SetFileName(outputPath);
    writer->SetInput(outputImage);
    writer->Update();
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

  std::cout << "Wrote Group TDI image: " << outputPath << "\n";
  return EXIT_SUCCESS;
}
