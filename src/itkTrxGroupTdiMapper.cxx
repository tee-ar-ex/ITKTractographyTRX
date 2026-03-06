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

#include "itkTrxGroupTdiMapper.h"

#include "itkImageFileReader.h"
#include "itkMath.h"
#include "itkNiftiImageIO.h"
#include "itkTrxFileReader.h"

#include "itk_eigen.h"
#include ITK_EIGEN(Core)

#include <cmath>
#include <limits>
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace itk
{
namespace
{
using RefImageType = TrxGroupTdiMapper::OutputImageType;

struct RasToVoxelState
{
  std::array<double, 9> LpsM{};
  std::array<double, 3> origin{};
  itk::Index<3>         bufferStart{};
  itk::Size<3>          bufferSize{};
};

RasToVoxelState
BuildRasToVoxelState(const RefImageType * image)
{
  RasToVoxelState state;
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
      state.LpsM[static_cast<size_t>(r * 3 + c)] = invDir(r, c) / spacing[r];
    }
    state.origin[static_cast<size_t>(r)] = origin[r];
  }
  return state;
}

inline bool
RasPointToIndex(const RasToVoxelState & state, double rx, double ry, double rz, int & i, int & j, int & k)
{
  using IndexValueType = RefImageType::IndexType::IndexValueType;
  const double lx = -rx;
  const double ly = -ry;
  const double lz = rz;
  const double dx = lx - state.origin[0];
  const double dy = ly - state.origin[1];
  const double dz = lz - state.origin[2];
  const double fi = state.LpsM[0] * dx + state.LpsM[1] * dy + state.LpsM[2] * dz;
  const double fj = state.LpsM[3] * dx + state.LpsM[4] * dy + state.LpsM[5] * dz;
  const double fk = state.LpsM[6] * dx + state.LpsM[7] * dy + state.LpsM[8] * dz;

  i = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fi));
  j = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fj));
  k = static_cast<int>(itk::Math::RoundHalfIntegerUp<IndexValueType>(fk));

  const int startI = state.bufferStart[0];
  const int startJ = state.bufferStart[1];
  const int startK = state.bufferStart[2];
  const int endI = startI + static_cast<int>(state.bufferSize[0]);
  const int endJ = startJ + static_cast<int>(state.bufferSize[1]);
  const int endK = startK + static_cast<int>(state.bufferSize[2]);

  return (i >= startI && i < endI && j >= startJ && j < endJ && k >= startK && k < endK);
}
} // namespace

void
TrxGroupTdiMapper::SetInputFileName(const std::string & filename)
{
  m_InputFileName = filename;
  this->Modified();
}

const std::string &
TrxGroupTdiMapper::GetInputFileName() const
{
  return m_InputFileName;
}

void
TrxGroupTdiMapper::SetGroupName(const std::string & groupName)
{
  m_GroupName = groupName;
  this->Modified();
}

const std::string &
TrxGroupTdiMapper::GetGroupName() const
{
  return m_GroupName;
}

void
TrxGroupTdiMapper::SetReferenceImageFileName(const std::string & filename)
{
  m_ReferenceImageFileName = filename;
  this->Modified();
}

const std::string &
TrxGroupTdiMapper::GetReferenceImageFileName() const
{
  return m_ReferenceImageFileName;
}

void
TrxGroupTdiMapper::SetInput(TrxStreamlineData::ConstPointer input)
{
  m_Input = input;
  this->Modified();
}

TrxStreamlineData::ConstPointer
TrxGroupTdiMapper::GetInput() const
{
  return m_Input;
}

void
TrxGroupTdiMapper::SetOptions(const Options & options)
{
  m_Options = options;
  this->Modified();
}

const TrxGroupTdiMapper::Options &
TrxGroupTdiMapper::GetOptions() const
{
  return m_Options;
}

void
TrxGroupTdiMapper::SetSelectedStreamlineIds(const std::vector<uint32_t> & ids)
{
  m_HasSelectedStreamlineIds = true;
  m_SelectedStreamlineIds = ids;
  this->Modified();
}

const std::vector<uint32_t> &
TrxGroupTdiMapper::GetSelectedStreamlineIds() const
{
  return m_SelectedStreamlineIds;
}

TrxGroupTdiMapper::OutputImageType *
TrxGroupTdiMapper::GetOutput()
{
  return m_Output;
}

const TrxGroupTdiMapper::OutputImageType *
TrxGroupTdiMapper::GetOutput() const
{
  return m_Output;
}

void
TrxGroupTdiMapper::Update()
{
  if (m_GroupName.empty())
  if (m_ReferenceImageFileName.empty())
  {
    itkGenericExceptionMacro("ReferenceImageFileName is empty.");
  }
  if (m_Options.mappingMode != MappingMode::NearestUniqueVoxelPerStreamline)
  {
    itkGenericExceptionMacro("Unsupported mapping mode.");
  }

  TrxStreamlineData::ConstPointer data = m_Input;
  if (!data)
  {
    if (m_InputFileName.empty())
    {
      itkGenericExceptionMacro("Input is null and InputFileName is empty.");
    }
    auto reader = TrxFileReader::New();
    reader->SetFileName(m_InputFileName);
    reader->Update();
    data = reader->GetOutput();
  }
  if (!data)
  {
    itkGenericExceptionMacro("No input TRX data available.");
  }

  using ReaderType = ImageFileReader<RefImageType>;
  auto io = NiftiImageIO::New();
  auto reader = ReaderType::New();
  reader->SetImageIO(io);
  reader->SetFileName(m_ReferenceImageFileName);
  reader->Update();
  auto ref = reader->GetOutput();
  if (!ref)
  {
    itkGenericExceptionMacro("Failed to read reference image: " << m_ReferenceImageFileName);
  }

  m_Output = RefImageType::New();
  m_Output->CopyInformation(ref);
  m_Output->SetRegions(ref->GetLargestPossibleRegion());
  m_Output->Allocate(true);

  const auto outRegion = m_Output->GetBufferedRegion();
  const auto outStart = outRegion.GetIndex();
  const auto outSize = outRegion.GetSize();
  const size_t nx = outSize[0];
  const size_t ny = outSize[1];
  const size_t nz = outSize[2];
  const size_t nVoxels = nx * ny * nz;
  auto *        outBuffer = m_Output->GetBufferPointer();
  if (!outBuffer)
  {
    itkGenericExceptionMacro("Failed to allocate output image buffer.");
  }

  std::vector<float> counts;
  if (m_Options.voxelStatistic == VoxelStatistic::Mean)
  {
    counts.assign(nVoxels, 0.0f);
  }

  const auto nStreamlines = data->GetNumberOfStreamlines();
  std::vector<uint8_t> inGroup(static_cast<size_t>(nStreamlines), 1);
  bool hasAnySelector = false;

  if (!m_GroupName.empty())
  {
    hasAnySelector = true;
    std::fill(inGroup.begin(), inGroup.end(), 0);
    const auto groupNames = data->GetGroupNames();
    const auto nameIt = std::find(groupNames.begin(), groupNames.end(), m_GroupName);
    if (nameIt == groupNames.end())
    {
      itkGenericExceptionMacro("Group not found: " << m_GroupName);
    }
    auto group = data->GetGroup(m_GroupName);
    if (!group)
    {
      itkGenericExceptionMacro("Group lookup failed: " << m_GroupName);
    }
    for (const uint32_t slIdx : group->GetStreamlineIndices())
    {
      if (slIdx < nStreamlines)
      {
        inGroup[slIdx] = 1;
      }
    }
  }

  if (m_HasSelectedStreamlineIds)
  {
    std::vector<uint8_t> selectedMask(static_cast<size_t>(nStreamlines), 0);
    for (const uint32_t slIdx : m_SelectedStreamlineIds)
    {
      if (slIdx < nStreamlines)
      {
        selectedMask[slIdx] = 1;
      }
    }
    if (!hasAnySelector)
    {
      inGroup.swap(selectedMask);
    }
    else
    {
      for (size_t i = 0; i < static_cast<size_t>(nStreamlines); ++i)
      {
        inGroup[i] = static_cast<uint8_t>(inGroup[i] && selectedMask[i]);
      }
    }
    hasAnySelector = true;
  }

  if (!hasAnySelector)
  {
    itkGenericExceptionMacro("No streamline selector set. Provide GroupName and/or SelectedStreamlineIds.");
  }

  std::vector<float> weights;
  if (m_Options.weightField.has_value())
  {
    weights = data->GetDpsField(*m_Options.weightField);
    if (weights.size() != static_cast<size_t>(nStreamlines))
    {
      itkGenericExceptionMacro("DPS weight field '" << *m_Options.weightField << "' size mismatch. Expected "
                                                    << nStreamlines << ", got " << weights.size());
    }
  }

  const auto rasToVoxel = BuildRasToVoxelState(ref);

  data->ForEachStreamlineChunked(
    [&](TrxStreamlineData::SizeValueType streamlineIndex,
        const void *                    xyz,
        TrxStreamlineData::SizeValueType nPts,
        TrxStreamlineData::CoordinateType coordType,
        TrxStreamlineData::CoordinateSystem /*cs*/) {
      if (streamlineIndex >= nStreamlines || !inGroup[streamlineIndex] || xyz == nullptr || nPts == 0)
      {
        return;
      }

      float w = 1.0f;
      if (!weights.empty())
      {
        w = weights[streamlineIndex];
        if (!std::isfinite(w) || w <= 0.0f)
        {
          return;
        }
      }

      std::unordered_set<size_t> visited;
      visited.reserve(static_cast<size_t>(nPts));

      for (TrxStreamlineData::SizeValueType p = 0; p < nPts; ++p)
      {
        double rx = 0.0, ry = 0.0, rz = 0.0;
        if (coordType == TrxStreamlineData::CoordinateType::Float16)
        {
          const auto * src = static_cast<const Eigen::half *>(xyz);
          rx = static_cast<double>(src[p * 3]);
          ry = static_cast<double>(src[p * 3 + 1]);
          rz = static_cast<double>(src[p * 3 + 2]);
        }
        else if (coordType == TrxStreamlineData::CoordinateType::Float64)
        {
          const auto * src = static_cast<const double *>(xyz);
          rx = src[p * 3];
          ry = src[p * 3 + 1];
          rz = src[p * 3 + 2];
        }
        else
        {
          const auto * src = static_cast<const float *>(xyz);
          rx = static_cast<double>(src[p * 3]);
          ry = static_cast<double>(src[p * 3 + 1]);
          rz = static_cast<double>(src[p * 3 + 2]);
        }

        if (!std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz))
        {
          continue;
        }

        int i = 0, j = 0, k = 0;
        if (!RasPointToIndex(rasToVoxel, rx, ry, rz, i, j, k))
        {
          continue;
        }
        const size_t localI = static_cast<size_t>(i - outStart[0]);
        const size_t localJ = static_cast<size_t>(j - outStart[1]);
        const size_t localK = static_cast<size_t>(k - outStart[2]);
        const size_t flat = localI + nx * (localJ + ny * localK);
        if (!visited.insert(flat).second)
        {
          continue;
        }
        outBuffer[flat] += w;
        if (!counts.empty())
        {
          counts[flat] += w;
        }
      }
    },
    TrxStreamlineData::CoordinateSystem::RAS);

  if (!counts.empty())
  {
    for (size_t idx = 0; idx < nVoxels; ++idx)
    {
      if (counts[idx] > 0.0f)
      {
        outBuffer[idx] /= counts[idx];
      }
    }
  }
}

TrxGroupTdiMapper::OutputImageType::Pointer
TrxGroupTdiMapper::Compute(const std::string & inputTrxFileName,
                           const std::string & groupName,
                           const std::string & referenceImageFileName)
{
  return Self::Compute(inputTrxFileName, groupName, referenceImageFileName, Options{});
}

TrxGroupTdiMapper::OutputImageType::Pointer
TrxGroupTdiMapper::Compute(const std::string & inputTrxFileName,
                           const std::string & groupName,
                           const std::string & referenceImageFileName,
                           const Options &     options)
{
  auto mapper = Self::New();
  mapper->SetInputFileName(inputTrxFileName);
  mapper->SetGroupName(groupName);
  mapper->SetReferenceImageFileName(referenceImageFileName);
  mapper->SetOptions(options);
  mapper->Update();
  auto out = mapper->GetOutput();
  if (!out)
  {
    itkGenericExceptionMacro("TrxGroupTdiMapper produced null output.");
  }
  out->DisconnectPipeline();
  return out;
}

void
TrxGroupTdiMapper::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "InputFileName: " << m_InputFileName << '\n';
  os << indent << "GroupName: " << m_GroupName << '\n';
  os << indent << "ReferenceImageFileName: " << m_ReferenceImageFileName << '\n';
  os << indent << "Options.VoxelStatistic: "
     << (m_Options.voxelStatistic == VoxelStatistic::Sum ? "Sum" : "Mean") << '\n';
  os << indent << "Options.MappingMode: NearestUniqueVoxelPerStreamline\n";
  os << indent << "Options.WeightField: "
     << (m_Options.weightField.has_value() ? *m_Options.weightField : "(none)") << '\n';
  os << indent << "HasSelectedStreamlineIds: " << (m_HasSelectedStreamlineIds ? "On" : "Off") << '\n';
  os << indent << "SelectedStreamlineIds: " << m_SelectedStreamlineIds.size() << '\n';
}
} // namespace itk
