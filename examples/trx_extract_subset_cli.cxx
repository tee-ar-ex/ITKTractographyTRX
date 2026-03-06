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
 * Example CLI for extracting a streamline subset into a new TRX file.
 *
 * Supported filtering:
 *  1) Group filter: union of one or more named TRX groups.
 *  2) Count filter: first N or random N streamlines (without replacement).
 *
 * Filters can be combined in one invocation. Count filtering is applied after
 * group filtering.
 */

#include "itkTrxFileReader.h"
#include "itkTrxFileWriter.h"
#include "itkTrxGroup.h"
#include "itkTrxStreamlineData.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace
{
void
PrintUsage(const char * exe)
{
  std::cerr << "Usage:\n"
            << "  " << exe
            << " --input <tractogram.trx_or_dir>"
               " --output <subset.trx_or_dir>"
               " [--group <name> --group <name> ...]"
               " [--num-streamlines <N> --selection-method first|random]"
               " [--seed <uint32>]\n\n"
            << "Examples:\n"
            << "  " << exe
            << " --input in.trx --output out.trx --group CST_left --group CST_right\n"
            << "  " << exe
            << " --input in.trx --output out.trx"
               " --num-streamlines <N>"
               " --selection-method first|random"
               " [--seed <uint32>]\n"
            << "  " << exe
            << " --input in.trx --output out.trx"
               " --group CST_left"
               " --num-streamlines <N>"
               " --selection-method random --seed 42\n\n"
            << "Notes:\n"
            << "  - Provide at least one filter: --group and/or --num-streamlines.\n"
            << "  - Group filter takes the union of all provided groups.\n"
            << "  - Count filter is applied to the current candidate set.\n"
            << "  - --num-streamlines must satisfy 0 < N < candidate streamlines.\n"
            << "  - Random selection is without replacement.\n";
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

bool
ContainsName(const std::vector<std::string> & names, const std::string & name)
{
  return std::find(names.begin(), names.end(), name) != names.end();
}

std::vector<uint32_t>
CollectGroupSelection(const itk::TrxStreamlineData * data, const std::vector<std::string> & groupNames)
{
  std::set<uint32_t> unique;
  for (const auto & name : groupNames)
  {
    auto group = data->GetGroup(name);
    if (!group)
    {
      continue;
    }
    const auto & ids = group->GetStreamlineIndices();
    unique.insert(ids.begin(), ids.end());
  }
  return std::vector<uint32_t>(unique.begin(), unique.end());
}

std::vector<uint32_t>
CollectCountSelection(const std::vector<uint32_t> & candidates,
                      size_t                        nRequested,
                      const std::string &           selectionMethod,
                      uint32_t                      seed)
{
  std::vector<uint32_t> ids = candidates;
  if (selectionMethod == "first")
  {
    ids.resize(nRequested);
    return ids;
  }

  std::mt19937 rng(seed);
  std::shuffle(ids.begin(), ids.end(), rng);
  ids.resize(nRequested);
  std::sort(ids.begin(), ids.end());
  return ids;
}
} // namespace

int
main(int argc, char * argv[])
{
  std::string              inputPath;
  std::string              outputPath;
  std::vector<std::string> requestedGroups;
  std::string              numStreamlinesArg;
  std::string              selectionMethod;
  std::string              seedArg;

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
    else if (arg == "--output")
    {
      if (!ReadArg(i, argc, argv, outputPath))
      {
        std::cerr << "Missing value for --output\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--group")
    {
      std::string groupName;
      if (!ReadArg(i, argc, argv, groupName))
      {
        std::cerr << "Missing value for --group\n";
        return EXIT_FAILURE;
      }
      requestedGroups.push_back(groupName);
    }
    else if (arg == "--num-streamlines")
    {
      if (!ReadArg(i, argc, argv, numStreamlinesArg))
      {
        std::cerr << "Missing value for --num-streamlines\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--selection-method")
    {
      if (!ReadArg(i, argc, argv, selectionMethod))
      {
        std::cerr << "Missing value for --selection-method\n";
        return EXIT_FAILURE;
      }
    }
    else if (arg == "--seed")
    {
      if (!ReadArg(i, argc, argv, seedArg))
      {
        std::cerr << "Missing value for --seed\n";
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

  if (inputPath.empty() || outputPath.empty())
  {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  const bool useGroupFilter = !requestedGroups.empty();
  const bool useCountFilter = !numStreamlinesArg.empty() || !selectionMethod.empty() || !seedArg.empty();

  size_t   nRequested = 0;
  uint32_t rngSeed = std::random_device{}();
  if (!useGroupFilter && !useCountFilter)
  {
    std::cerr << "No filter specified. Use --group and/or --num-streamlines.\n";
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  if (useCountFilter)
  {
    if (numStreamlinesArg.empty() || selectionMethod.empty())
    {
      std::cerr << "Count filtering requires both --num-streamlines and --selection-method.\n";
      return EXIT_FAILURE;
    }
    if (selectionMethod != "first" && selectionMethod != "random")
    {
      std::cerr << "Invalid --selection-method: " << selectionMethod << " (expected first|random)\n";
      return EXIT_FAILURE;
    }
    try
    {
      nRequested = static_cast<size_t>(std::stoull(numStreamlinesArg));
    }
    catch (const std::exception &)
    {
      std::cerr << "Invalid --num-streamlines: " << numStreamlinesArg << "\n";
      return EXIT_FAILURE;
    }
    if (!seedArg.empty())
    {
      try
      {
        const auto parsedSeed = std::stoull(seedArg);
        if (parsedSeed > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max()))
        {
          std::cerr << "--seed is out of range for uint32: " << seedArg << "\n";
          return EXIT_FAILURE;
        }
        rngSeed = static_cast<uint32_t>(parsedSeed);
      }
      catch (const std::exception &)
      {
        std::cerr << "Invalid --seed: " << seedArg << "\n";
        return EXIT_FAILURE;
      }
    }
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

    const auto totalStreamlines = static_cast<size_t>(data->GetNumberOfStreamlines());
    std::vector<uint32_t> selectedIds(totalStreamlines);
    std::iota(selectedIds.begin(), selectedIds.end(), 0u);

    if (useGroupFilter)
    {
      const auto availableGroups = data->GetGroupNames();
      for (const auto & name : requestedGroups)
      {
        if (!ContainsName(availableGroups, name))
        {
          std::cerr << "Group not found: " << name << "\n";
          return EXIT_FAILURE;
        }
      }

      selectedIds = CollectGroupSelection(data, requestedGroups);
      std::cout << "Group filter kept " << selectedIds.size() << " streamlines from " << requestedGroups.size()
                << " group(s).\n";
    }

    if (useCountFilter)
    {
      const auto nCandidates = selectedIds.size();
      if (nRequested == 0 || nRequested >= nCandidates)
      {
        std::cerr << "--num-streamlines must satisfy 0 < N < " << nCandidates << "\n";
        return EXIT_FAILURE;
      }
      selectedIds = CollectCountSelection(selectedIds, nRequested, selectionMethod, rngSeed);
      std::cout << "Count filter kept " << selectedIds.size() << " streamlines using method '" << selectionMethod
                << "'";
      if (selectionMethod == "random")
      {
        std::cout << " (seed=" << rngSeed << ")";
      }
      std::cout << ".\n";
    }

    auto subset = data->SubsetStreamlinesLazy(selectedIds);
    if (!subset)
    {
      std::cerr << "Failed to build streamline subset.\n";
      return EXIT_FAILURE;
    }

    auto writer = itk::TrxFileWriter::New();
    writer->SetFileName(outputPath);
    writer->SetInput(subset);
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

  std::cout << "Wrote subset TRX to: " << outputPath << "\n";
  return EXIT_SUCCESS;
}
