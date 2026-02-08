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

#include "itk_eigen.h"
#include "itksys/SystemTools.hxx"
#include ITK_EIGEN(Core)

int
itkTrxReadWriteTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " output_path" << std::endl;
    return EXIT_FAILURE;
  }

  using DataType = itk::TrxStreamlineData;

  const float values[15] = { 1.0f, 2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f, 8.0f,
                             9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };

  DataType::MatrixType ras;
  ras.SetIdentity();
  DataType::DimensionsType dims;
  dims[0] = 100;
  dims[1] = 101;
  dims[2] = 102;

  const std::string basePath = argv[1];
  const std::string outputDir = itksys::SystemTools::GetFilenamePath(basePath);
  if (!outputDir.empty())
  {
    itksys::SystemTools::MakeDirectory(outputDir);
  }

  auto validateOutput = [&](const DataType * output) -> bool {
    if (!output->HasFloat32Positions())
    {
      std::cerr << "Expected float32 positions in output." << std::endl;
      return false;
    }
    if (output->GetNumberOfVertices() != 5 || output->GetNumberOfStreamlines() != 2)
    {
      std::cerr << "Unexpected vertex or streamline count." << std::endl;
      return false;
    }
    const auto & offsets = output->GetOffsets();
    if (offsets.size() != 2 || offsets[0] != 0 || offsets[1] != 3)
    {
      std::cerr << "Unexpected offsets." << std::endl;
      return false;
    }

    const auto * outputPositions = output->GetFloat32Positions();
    if (outputPositions == nullptr || outputPositions->size() != 15)
    {
      std::cerr << "Unexpected positions size." << std::endl;
      return false;
    }

    for (size_t index = 0; index < 15; ++index)
    {
      const float outValue = static_cast<float>((*outputPositions)[index]);
      if (outValue != values[index])
      {
        std::cerr << "Position mismatch at index " << index << std::endl;
        return false;
      }
    }

    return true;
  };

  auto roundTrip = [&](const std::string & outputPath, bool useCompression, bool expectDirectory) -> bool {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(outputPath);
    writer->SetUseCompression(useCompression);
    writer->SetVoxelToRasMatrix(ras);
    writer->SetDimensions(dims);

    itk::TrxStreamWriter::StreamlineType first;
    itk::TrxStreamWriter::StreamlineType second;
    first.reserve(3);
    second.reserve(2);
    for (size_t i = 0; i < 5; ++i)
    {
      itk::Point<double, 3> point;
      point[0] = values[i * 3];
      point[1] = values[i * 3 + 1];
      point[2] = values[i * 3 + 2];
      if (i < 3)
      {
        first.push_back(point);
      }
      else
      {
        second.push_back(point);
      }
    }

    writer->PushStreamline(first);
    writer->PushStreamline(second);
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

    return validateOutput(reader->GetOutput());
  };

  if (!roundTrip(basePath, false, true))
  {
    return EXIT_FAILURE;
  }

  const std::string zipPath = basePath + ".trx";
  if (!roundTrip(zipPath, false, false))
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
