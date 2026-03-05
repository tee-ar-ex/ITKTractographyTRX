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
  std::array<double, 9> M{};
  std::array<double, 3> b{};
  itk::Size<3>          dims{};
};

RasToVoxelState
BuildRasToVoxelState(const RefImageType * image)
{
  RasToVoxelState state;
  state.dims = image->GetLargestPossibleRegion().GetSize();

  const auto & invDir = image->GetInverseDirection();
  const auto & spacing = image->GetSpacing();
  const auto & origin = image->GetOrigin();

  // ITK image geometry is LPS. Build lps->voxel, then compose with
  // ras->lps = diag(-1,-1,1) so mapping runs directly on TRX-native RAS points.
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

inline bool
RasPointToIndex(const RasToVoxelState & state, double rx, double ry, double rz, int & i, int & j, int & k)
{
  const double fi = state.M[0] * rx + state.M[1] * ry + state.M[2] * rz + state.b[0];
  const double fj = state.M[3] * rx + state.M[4] * ry + state.M[5] * rz + state.b[1];
  const double fk = state.M[6] * rx + state.M[7] * ry + state.M[8] * rz + state.b[2];

  i = static_cast<int>(std::round(fi));
  j = static_cast<int>(std::round(fj));
  k = static_cast<int>(std::round(fk));

  return (i >= 0 && i < static_cast<int>(state.dims[0]) && j >= 0 && j < static_cast<int>(state.dims[1]) && k >= 0 &&
          k < static_cast<int>(state.dims[2]));
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

  const auto dims = ref->GetLargestPossibleRegion().GetSize();
  const size_t nx = dims[0];
  const size_t ny = dims[1];
  const size_t nz = dims[2];
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
        const size_t flat = static_cast<size_t>(i) + nx * (static_cast<size_t>(j) + ny * static_cast<size_t>(k));
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
