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
 * Example CLI for adding parcellation-derived groups to a TRX file.
 *
 * Supports in-place updates by writing to a temporary output and replacing the
 * original artifact after successful completion.
 */

#include "itkTrxFileReader.h"
#include "itkTrxParcellationLabeler.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct AtlasSpec
{
  std::string niftiPath;
  std::string labelFilePath;
  std::string prefix;
};

void
PrintUsage(const char * exe)
{
  std::cerr << "Usage:\n"
            << "  " << exe
            << " --input <tractogram.trx_or_dir>"
               " [--output <out.trx_or_dir> | --in-place]"
               " --atlas-spec <nifti,labeltxt,prefix>"
               " [--atlas-spec <nifti,labeltxt,prefix> ...]"
               " [--dilation-radius <voxels>]\n\n"
            << "Examples:\n"
            << "  " << exe
            << " --input subj.trx --in-place"
               " --atlas-spec native_seg.nii.gz,native_seg.txt,Glasser\n"
            << "  " << exe
            << " --input subj.trx --output subj_labeled.trx"
               " --atlas-spec seg1.nii.gz,seg1.txt,Atlas1"
               " --atlas-spec seg2.nii.gz,seg2.txt,Atlas2\n";
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
ParseAtlasSpec(const std::string & spec, AtlasSpec & out)
{
  std::stringstream ss(spec);
  std::string part;
  std::vector<std::string> parts;
  while (std::getline(ss, part, ','))
  {
    parts.push_back(part);
  }
  if (parts.size() != 3)
  {
    return false;
  }
  out.niftiPath = parts[0];
  out.labelFilePath = parts[1];
  out.prefix = parts[2];
  return !(out.niftiPath.empty() || out.labelFilePath.empty() || out.prefix.empty());
}

std::string
BuildTempOutputPath(const std::string & inputPath, bool inputIsDirectory)
{
  const std::filesystem::path inPath(inputPath);
  const std::string suffix = ".tmp_add_groups";
  if (inputIsDirectory)
  {
    return (inPath.string() + suffix);
  }
  return (inPath.string() + suffix + ".trx");
}

bool
ReplaceArtifact(const std::string & src, const std::string & dst, bool dstIsDirectory)
{
  std::error_code ec;
  std::filesystem::path srcPath(src);
  std::filesystem::path dstPath(dst);

  if (dstIsDirectory)
  {
    std::filesystem::remove_all(dstPath, ec);
    ec.clear();
  }
  else
  {
    std::filesystem::remove(dstPath, ec);
    ec.clear();
  }
  std::filesystem::rename(srcPath, dstPath, ec);
  if (ec)
  {
    std::cerr << "Failed to replace output artifact: " << ec.message() << "\n";
    return false;
  }
  return true;
}
} // namespace

int
main(int argc, char * argv[])
{
  std::string inputPath;
  std::string outputPath;
  bool inPlace = false;
  unsigned int dilationRadius = 0;
  std::vector<AtlasSpec> atlasSpecs;

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
    else if (arg == "--in-place")
    {
      inPlace = true;
    }
    else if (arg == "--atlas-spec")
    {
      std::string spec;
      if (!ReadArg(i, argc, argv, spec))
      {
        std::cerr << "Missing value for --atlas-spec\n";
        return EXIT_FAILURE;
      }
      AtlasSpec parsed;
      if (!ParseAtlasSpec(spec, parsed))
      {
        std::cerr << "Invalid --atlas-spec. Expected: <nifti,labeltxt,prefix>\n";
        return EXIT_FAILURE;
      }
      atlasSpecs.push_back(parsed);
    }
    else if (arg == "--dilation-radius")
    {
      std::string value;
      if (!ReadArg(i, argc, argv, value))
      {
        std::cerr << "Missing value for --dilation-radius\n";
        return EXIT_FAILURE;
      }
      try
      {
        dilationRadius = static_cast<unsigned int>(std::stoul(value));
      }
      catch (const std::exception &)
      {
        std::cerr << "Invalid --dilation-radius: " << value << "\n";
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

  if (inputPath.empty() || atlasSpecs.empty())
  {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }
  if (inPlace && !outputPath.empty() && outputPath != inputPath)
  {
    std::cerr << "--in-place cannot be used with a different --output path.\n";
    return EXIT_FAILURE;
  }
  if (!inPlace && outputPath.empty())
  {
    std::cerr << "Specify either --output or --in-place.\n";
    return EXIT_FAILURE;
  }

  const bool outputIsInPlace = inPlace || (outputPath == inputPath);
  const std::string finalOutputPath = outputIsInPlace ? inputPath : outputPath;

  std::error_code ec;
  const bool inputIsDirectory = std::filesystem::is_directory(std::filesystem::path(inputPath), ec);
  if (ec)
  {
    std::cerr << "Failed to inspect input path: " << ec.message() << "\n";
    return EXIT_FAILURE;
  }
  const std::string workOutputPath =
    outputIsInPlace ? BuildTempOutputPath(inputPath, inputIsDirectory) : finalOutputPath;

  try
  {
    auto reader = itk::TrxFileReader::New();
    reader->SetFileName(inputPath);
    reader->Update();
    auto inputData = reader->GetOutput();

    auto labeler = itk::TrxParcellationLabeler::New();
    labeler->SetInput(inputData);
    labeler->SetInputFileName(inputPath);
    labeler->SetOutputFileName(workOutputPath);
    labeler->SetDilationRadius(dilationRadius);

    for (const auto & spec : atlasSpecs)
    {
      itk::TrxParcellationLabeler::ParcellationSpec p;
      p.niftiPath = spec.niftiPath;
      p.labelFilePath = spec.labelFilePath;
      p.groupPrefix = spec.prefix;
      labeler->AddParcellation(p);
    }
    labeler->Update();
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

  if (outputIsInPlace)
  {
    if (!ReplaceArtifact(workOutputPath, finalOutputPath, inputIsDirectory))
    {
      return EXIT_FAILURE;
    }
  }

  std::cout << "Groups written to: " << finalOutputPath << "\n";
  return EXIT_SUCCESS;
}
