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
#include "itkTrxParcellationLabeler.h"

#include "itkTrxFileReader.h"
#include "itkTrxStreamWriter.h"

#include "itkBinaryBallStructuringElement.h"
#include "itkGrayscaleDilateImageFilter.h"
#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkNiftiImageIO.h"

#include "itk_eigen.h"
#include ITK_EIGEN(Core)

#include "vnl/vnl_matrix.h"

#include <trx/trx.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace itk
{

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

namespace
{

using LabelImageType = Image<int32_t, 3>;

/** Parse a dseg.txt file: lines of the form "<int> <name>". */
std::map<int32_t, std::string>
ParseLabelFile(const std::string & path)
{
  std::map<int32_t, std::string> result;
  std::ifstream                  in(path);
  if (!in.is_open())
  {
    itkGenericExceptionMacro("Cannot open label file: " << path);
  }
  std::string line;
  while (std::getline(in, line))
  {
    // Strip carriage return
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    // Skip blank lines and comments
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#')
    {
      continue;
    }
    std::istringstream ss(line);
    int32_t            label;
    std::string        name;
    if (!(ss >> label >> name))
    {
      continue;
    }
    result[label] = name;
  }
  return result;
}

/** Load a NIfTI parcellation image using ITK (result is in LPS). */
LabelImageType::Pointer
LoadParcellation(const std::string & niftiPath)
{
  using ReaderType = ImageFileReader<LabelImageType>;
  auto io = NiftiImageIO::New();
  auto reader = ReaderType::New();
  reader->SetImageIO(io);
  reader->SetFileName(niftiPath);
  reader->Update();
  return reader->GetOutput();
}

/** Morphological max-dilation of the label image (in-place replacement). */
LabelImageType::Pointer
DilateLabelImage(LabelImageType::Pointer image, unsigned int radius)
{
  if (radius == 0)
  {
    return image;
  }
  using SEType = BinaryBallStructuringElement<LabelImageType::PixelType, 3>;
  using DilateType = GrayscaleDilateImageFilter<LabelImageType, LabelImageType, SEType>;
  SEType ball;
  ball.SetRadius(radius);
  ball.CreateStructuringElement();
  auto dilate = DilateType::New();
  dilate->SetInput(image);
  dilate->SetKernel(ball);
  dilate->Update();
  return dilate->GetOutput();
}

/** Per-atlas lookup state computed once and reused for every streamline. */
struct AtlasState
{
  const int32_t *                     buffer{ nullptr };
  itk::Size<3>                        dims;
  // Row-major 3×3 physical-to-index matrix (from image->GetPhysicalPointToIndexMatrix()).
  // Applied as: ci = M[0..2] · diff, cj = M[3..5] · diff, ck = M[6..8] · diff
  std::array<double, 9>               M;
  std::array<double, 3>               origin;
  std::map<int32_t, std::string>      labelMap;
  std::string                         prefix;
  // Held here to keep the image buffer alive.
  LabelImageType::Pointer             image;
};

AtlasState
BuildAtlasState(LabelImageType::Pointer image,
                const std::map<int32_t, std::string> & labelMap,
                const std::string & prefix)
{
  AtlasState s;
  s.image = image;
  s.labelMap = labelMap;
  s.prefix = prefix;
  s.dims = image->GetLargestPossibleRegion().GetSize();
  s.buffer = image->GetBufferPointer();

  // Reconstruct physical->index matrix from public ImageBase API for
  // compatibility across ITK versions:
  //   index = diag(1/spacing) * inverse(direction) * (point - origin)
  const auto & invDir = image->GetInverseDirection();
  const auto & spacing = image->GetSpacing();
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 3; ++c)
    {
      s.M[static_cast<size_t>(r * 3 + c)] = invDir(r, c) / spacing[r];
    }
  }

  const auto & orig = image->GetOrigin();
  s.origin[0] = orig[0];
  s.origin[1] = orig[1];
  s.origin[2] = orig[2];
  return s;
}

/** Convert a physical LPS point to a voxel index using precomputed M and origin.
 *  Returns false if the resulting index is outside the image bounds. */
inline bool
PhysicalToIndex(const AtlasState & atlas, double px, double py, double pz, int & i, int & j, int & k)
{
  const double dx = px - atlas.origin[0];
  const double dy = py - atlas.origin[1];
  const double dz = pz - atlas.origin[2];
  i = static_cast<int>(std::round(atlas.M[0] * dx + atlas.M[1] * dy + atlas.M[2] * dz));
  j = static_cast<int>(std::round(atlas.M[3] * dx + atlas.M[4] * dy + atlas.M[5] * dz));
  k = static_cast<int>(std::round(atlas.M[6] * dx + atlas.M[7] * dy + atlas.M[8] * dz));
  return (i >= 0 && i < static_cast<int>(atlas.dims[0]) && j >= 0 &&
          j < static_cast<int>(atlas.dims[1]) && k >= 0 && k < static_cast<int>(atlas.dims[2]));
}

/** Return the label at a validated voxel index (ITK buffer: x varies fastest). */
inline int32_t
LabelAt(const AtlasState & atlas, int i, int j, int k)
{
  return atlas.buffer[static_cast<size_t>(i) +
                      static_cast<size_t>(j) * atlas.dims[0] +
                      static_cast<size_t>(k) * atlas.dims[0] * atlas.dims[1]];
}

size_t
ComputeArtifactSizeBytes(const std::string & path)
{
  std::error_code         ec;
  const std::filesystem::path p(path);
  if (!std::filesystem::exists(p, ec))
  {
    return 0;
  }
  if (std::filesystem::is_regular_file(p, ec))
  {
    const auto size = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<size_t>(size);
  }
  if (!std::filesystem::is_directory(p, ec))
  {
    return 0;
  }

  size_t total = 0;
  for (auto it = std::filesystem::recursive_directory_iterator(p, ec);
       !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec))
  {
    if (it->is_regular_file(ec))
    {
      total += static_cast<size_t>(it->file_size(ec));
    }
    ec.clear();
  }
  return total;
}

void
CopyTrxArtifact(const std::string & source, const std::string & destination)
{
  std::error_code ec;
  const std::filesystem::path srcPath(source);
  const std::filesystem::path dstPath(destination);
  if (srcPath.empty() || dstPath.empty())
  {
    itkGenericExceptionMacro("Source and destination paths must be non-empty.");
  }
  if (!std::filesystem::exists(srcPath, ec))
  {
    itkGenericExceptionMacro("InputFileName does not exist: " << source);
  }
  if (std::filesystem::equivalent(srcPath, dstPath, ec))
  {
    itkGenericExceptionMacro("InputFileName and OutputFileName must differ for copy-through mode.");
  }

  if (std::filesystem::is_directory(srcPath, ec))
  {
    if (std::filesystem::exists(dstPath, ec))
    {
      std::filesystem::remove_all(dstPath, ec);
      if (ec)
      {
        itkGenericExceptionMacro("Failed to clear output directory '" << destination << "': " << ec.message());
      }
    }
    std::filesystem::create_directories(dstPath.parent_path(), ec);
    ec.clear();
    std::filesystem::copy(srcPath, dstPath, std::filesystem::copy_options::recursive, ec);
    if (ec)
    {
      itkGenericExceptionMacro("Failed to copy input directory '" << source << "' to '" << destination
                                                                  << "': " << ec.message());
    }
    return;
  }

  const auto parent = dstPath.parent_path();
  if (!parent.empty())
  {
    std::filesystem::create_directories(parent, ec);
    if (ec)
    {
      itkGenericExceptionMacro("Failed to create output parent directory '" << parent.string()
                                                                            << "': " << ec.message());
    }
  }
  std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec)
  {
    itkGenericExceptionMacro("Failed to copy input file '" << source << "' to '" << destination << "': "
                                                           << ec.message());
  }
}

} // anonymous namespace

// -----------------------------------------------------------------------
// TrxParcellationLabeler implementation
// -----------------------------------------------------------------------

void
TrxParcellationLabeler::AddParcellation(const ParcellationSpec & spec)
{
  m_Parcellations.push_back(spec);
}

void
TrxParcellationLabeler::SetInput(TrxStreamlineData::ConstPointer input)
{
  m_Input = input;
}

void
TrxParcellationLabeler::SetInputFileName(const std::string & filename)
{
  m_InputFileName = filename;
}

const std::string &
TrxParcellationLabeler::GetInputFileName() const
{
  return m_InputFileName;
}

void
TrxParcellationLabeler::SetOutputFileName(const std::string & filename)
{
  m_OutputFileName = filename;
}

const std::string &
TrxParcellationLabeler::GetOutputFileName() const
{
  return m_OutputFileName;
}

size_t
TrxParcellationLabeler::GetPreGroupFileBytes() const
{
  return m_PreGroupFileBytes;
}

size_t
TrxParcellationLabeler::GetFinalFileBytes() const
{
  return m_FinalFileBytes;
}

void
TrxParcellationLabeler::Update()
{
  if (!m_Input)
  {
    itkExceptionMacro("Input TrxStreamlineData is not set.");
  }
  if (m_OutputFileName.empty())
  {
    itkExceptionMacro("OutputFileName is not set.");
  }
  if (m_Parcellations.empty())
  {
    itkExceptionMacro("No parcellations have been added.");
  }
  m_PreGroupFileBytes = 0;
  m_FinalFileBytes = 0;

  // ------------------------------------------------------------------
  // Phase 0: load and prepare all atlases
  // ------------------------------------------------------------------
  std::vector<AtlasState> atlases;
  atlases.reserve(m_Parcellations.size());
  for (const auto & spec : m_Parcellations)
  {
    auto image = LoadParcellation(spec.niftiPath);
    image = DilateLabelImage(image, m_DilationRadius);
    auto labelMap = ParseLabelFile(spec.labelFilePath);
    atlases.push_back(BuildAtlasState(image, labelMap, spec.groupPrefix));
  }

  const size_t nAtlases = atlases.size();

  // ------------------------------------------------------------------
  // Phase 1: single-pass labeling — accumulate label→[streamline_idx]
  // ------------------------------------------------------------------
  // Outer vector: one map per atlas.  Inner map: label int → sorted indices.
  std::vector<std::unordered_map<int32_t, std::vector<uint32_t>>> labelGroups(nAtlases);

  const SizeValueType nStreamlines = m_Input->GetNumberOfStreamlines();

  m_Input->ForEachStreamlineChunked(
    [&](SizeValueType slIdx,
        const void *  xyz,
        SizeValueType nPts,
        TrxStreamlineData::CoordinateType   coordType,
        TrxStreamlineData::CoordinateSystem /*cs*/) {
      if (!xyz || nPts == 0)
      {
        return;
      }

      // Per-atlas: track which labels this streamline has already matched
      // (so each (streamline, atlas, label) triple is recorded at most once).
      std::vector<std::unordered_set<int32_t>> matched(nAtlases);

      for (SizeValueType p = 0; p < nPts; ++p)
      {
        double px, py, pz;
        if (coordType == TrxStreamlineData::CoordinateType::Float16)
        {
          const auto * src = static_cast<const Eigen::half *>(xyz);
          px = static_cast<double>(src[p * 3]);
          py = static_cast<double>(src[p * 3 + 1]);
          pz = static_cast<double>(src[p * 3 + 2]);
        }
        else if (coordType == TrxStreamlineData::CoordinateType::Float64)
        {
          const auto * src = static_cast<const double *>(xyz);
          px = src[p * 3];
          py = src[p * 3 + 1];
          pz = src[p * 3 + 2];
        }
        else
        {
          const auto * src = static_cast<const float *>(xyz);
          px = static_cast<double>(src[p * 3]);
          py = static_cast<double>(src[p * 3 + 1]);
          pz = static_cast<double>(src[p * 3 + 2]);
        }

        for (size_t a = 0; a < nAtlases; ++a)
        {
          int i, j, k;
          if (!PhysicalToIndex(atlases[a], px, py, pz, i, j, k))
          {
            continue;
          }
          const int32_t label = LabelAt(atlases[a], i, j, k);
          if (label == 0)
          {
            continue;
          }
          // Insert returns {iterator, inserted}; only record first hit per label.
          if (matched[a].insert(label).second)
          {
            labelGroups[a][label].push_back(static_cast<uint32_t>(slIdx));
          }
        }
      }
    },
    TrxStreamlineData::CoordinateSystem::LPS);

  // Build the final group map (existing input groups + newly computed groups).
  std::map<std::string, std::vector<uint32_t>> allGroups;
  for (const auto & groupName : m_Input->GetGroupNames())
  {
    auto group = m_Input->GetGroup(groupName);
    if (group)
    {
      allGroups[groupName] = group->GetStreamlineIndices();
    }
  }
  for (size_t a = 0; a < nAtlases; ++a)
  {
    const auto & atlas = atlases[a];
    for (const auto & kv : labelGroups[a])
    {
      const int32_t label = kv.first;
      auto          it = atlas.labelMap.find(label);
      if (it == atlas.labelMap.end())
      {
        continue;
      }
      const std::string groupName = atlas.prefix + "_" + it->second;
      std::vector<uint32_t> indices = kv.second;
      std::sort(indices.begin(), indices.end());
      allGroups[groupName] = std::move(indices);
    }
  }

  // Fast path: byte-copy input artifact and append groups in-place.
  // This preserves original coordinate dtype and payload encoding.
  if (!m_InputFileName.empty())
  {
    // Guardrail: copy-through mode must operate on the same full dataset that
    // was used to compute labels. This prevents accidental subset mismatch.
    auto srcReader = TrxFileReader::New();
    srcReader->SetFileName(m_InputFileName);
    srcReader->Update();
    auto srcData = srcReader->GetOutput();
    if (srcData->GetNumberOfStreamlines() != m_Input->GetNumberOfStreamlines() ||
        srcData->GetNumberOfVertices() != m_Input->GetNumberOfVertices())
    {
      itkExceptionMacro("InputFileName dataset does not match input streamline data. "
                        "Copy-through mode requires identical streamline/vertex counts.");
    }

    CopyTrxArtifact(m_InputFileName, m_OutputFileName);
    m_PreGroupFileBytes = ComputeArtifactSizeBytes(m_OutputFileName);
    std::error_code ec;
    const bool      copiedIsDirectory = std::filesystem::is_directory(std::filesystem::path(m_OutputFileName), ec);
    if (!ec && copiedIsDirectory)
    {
      trx::append_groups_to_directory(m_OutputFileName, allGroups);
    }
    else
    {
      trx::append_groups_to_zip(m_OutputFileName, allGroups);
    }
    m_FinalFileBytes = ComputeArtifactSizeBytes(m_OutputFileName);
    return;
  }

  // ------------------------------------------------------------------
  // Phase 2: load DPS / DPV data for passthrough
  // ------------------------------------------------------------------
  const auto   dpsNames = m_Input->GetDpsFieldNames();
  const auto   dpvNames = m_Input->GetDpvFieldNames();
  const size_t nVerts = m_Input->GetNumberOfVertices();

  std::map<std::string, std::vector<float>> dpsData;
  for (const auto & name : dpsNames)
  {
    dpsData[name] = m_Input->GetDpsField(name);
  }

  std::map<std::string, std::vector<float>> dpvData;
  for (const auto & name : dpvNames)
  {
    const size_t estimatedBytes = nVerts * sizeof(float);
    if (estimatedBytes > m_MaxDpvBytes)
    {
      itkWarningMacro("DPV field '" << name << "' skipped: estimated size "
                                    << estimatedBytes << " bytes exceeds MaxDpvBytes "
                                    << m_MaxDpvBytes << ".");
      continue;
    }
    dpvData[name] = m_Input->GetDpvField(name);
  }

  // Infer DPV column count per field (total values / total vertices).
  std::map<std::string, size_t> dpvNcols;
  for (const auto & kv : dpvData)
  {
    dpvNcols[kv.first] = (nVerts > 0) ? kv.second.size() / nVerts : 1;
  }

  // ------------------------------------------------------------------
  // Phase 3: stream positions + DPS/DPV to a new TRX (no groups yet)
  // ------------------------------------------------------------------
  auto writer = TrxStreamWriter::New();
  writer->SetFileName(m_OutputFileName);
  writer->UseCompressionOff();
  if (m_Input->HasVoxelToLpsMatrix())
  {
    writer->SetVoxelToLpsMatrix(m_Input->GetVoxelToLpsMatrix());
  }
  if (m_Input->HasVoxelToRasMatrix())
  {
    writer->SetVoxelToRasMatrix(m_Input->GetVoxelToRasMatrix());
  }
  if (m_Input->HasDimensions())
  {
    writer->SetDimensions(m_Input->GetDimensions());
  }
  for (const auto & name : dpsNames)
  {
    writer->RegisterDpsField(name, "float32");
  }
  for (const auto & name : dpvData)
  {
    writer->RegisterDpvField(name.first, "float32");
  }

  const auto & offsets = m_Input->GetOffsets();

  vnl_matrix<double> pointBuf;

  m_Input->ForEachStreamlineChunked(
    [&](SizeValueType slIdx,
        const void *  xyz,
        SizeValueType nPts,
        TrxStreamlineData::CoordinateType   coordType,
        TrxStreamlineData::CoordinateSystem /*cs*/) {
      pointBuf.set_size(static_cast<unsigned int>(nPts), 3);

      for (SizeValueType p = 0; p < nPts; ++p)
      {
        if (coordType == TrxStreamlineData::CoordinateType::Float16)
        {
          const auto * src = static_cast<const Eigen::half *>(xyz);
          pointBuf(p, 0) = static_cast<double>(src[p * 3]);
          pointBuf(p, 1) = static_cast<double>(src[p * 3 + 1]);
          pointBuf(p, 2) = static_cast<double>(src[p * 3 + 2]);
        }
        else if (coordType == TrxStreamlineData::CoordinateType::Float64)
        {
          const auto * src = static_cast<const double *>(xyz);
          pointBuf(p, 0) = src[p * 3];
          pointBuf(p, 1) = src[p * 3 + 1];
          pointBuf(p, 2) = src[p * 3 + 2];
        }
        else
        {
          const auto * src = static_cast<const float *>(xyz);
          pointBuf(p, 0) = static_cast<double>(src[p * 3]);
          pointBuf(p, 1) = static_cast<double>(src[p * 3 + 1]);
          pointBuf(p, 2) = static_cast<double>(src[p * 3 + 2]);
        }
      }

      // DPS: one scalar per streamline.
      std::map<std::string, double> dpsValues;
      for (const auto & kv : dpsData)
      {
        dpsValues[kv.first] = static_cast<double>(kv.second[slIdx]);
      }

      // DPV: slice flat array using offsets.
      std::map<std::string, std::vector<double>> dpvValues;
      if (!dpvData.empty())
      {
        const size_t vStart = static_cast<size_t>(offsets[slIdx]);
        const size_t vEnd = static_cast<size_t>(offsets[slIdx + 1]);
        for (const auto & kv : dpvData)
        {
          const size_t nc = dpvNcols.at(kv.first);
          std::vector<double> vals;
          vals.reserve((vEnd - vStart) * nc);
          for (size_t v = vStart; v < vEnd; ++v)
          {
            for (size_t c = 0; c < nc; ++c)
            {
              vals.push_back(static_cast<double>(kv.second[v * nc + c]));
            }
          }
          dpvValues[kv.first] = std::move(vals);
        }
      }

      writer->PushStreamline(pointBuf, dpsValues, dpvValues, {});
    },
    TrxStreamlineData::CoordinateSystem::LPS);

  writer->Finalize();
  m_PreGroupFileBytes = ComputeArtifactSizeBytes(m_OutputFileName);

  // Detect output format from file extension.
  const std::string ext = std::filesystem::path(m_OutputFileName).extension().string();
  if (ext == ".trx")
  {
    trx::append_groups_to_zip(m_OutputFileName, allGroups);
  }
  else
  {
    trx::append_groups_to_directory(m_OutputFileName, allGroups);
  }
  m_FinalFileBytes = ComputeArtifactSizeBytes(m_OutputFileName);
}

void
TrxParcellationLabeler::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "InputFileName: " << m_InputFileName << "\n";
  os << indent << "OutputFileName: " << m_OutputFileName << "\n";
  os << indent << "DilationRadius: " << m_DilationRadius << "\n";
  os << indent << "MaxDpvBytes: " << m_MaxDpvBytes << "\n";
  os << indent << "PreGroupFileBytes: " << m_PreGroupFileBytes << "\n";
  os << indent << "FinalFileBytes: " << m_FinalFileBytes << "\n";
  os << indent << "Parcellations: " << m_Parcellations.size() << "\n";
  for (size_t i = 0; i < m_Parcellations.size(); ++i)
  {
    os << indent << "  [" << i << "] prefix=" << m_Parcellations[i].groupPrefix
       << "  nifti=" << m_Parcellations[i].niftiPath << "\n";
  }
}

} // namespace itk
