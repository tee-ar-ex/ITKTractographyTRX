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
#include "itkTrxStreamWriter.h"

#include <trx/trx.h>

#include <algorithm>
#include <limits>

namespace itk
{
namespace
{
itk::TrxStreamWriter::MatrixType
ConvertVoxelToRasFromLps(const itk::TrxStreamWriter::MatrixType & lps)
{
  itk::TrxStreamWriter::MatrixType ras = lps;
  for (unsigned int col = 0; col < 4; ++col)
  {
    ras[0][col] = -ras[0][col];
    ras[1][col] = -ras[1][col];
  }
  return ras;
}
} // namespace

TrxStreamWriter::TrxStreamWriter() = default;

void
TrxStreamWriter::SetFileName(const std::string & filename)
{
  if (m_FileName == filename)
  {
    return;
  }
  m_FileName = filename;
  this->Modified();
}

const std::string &
TrxStreamWriter::GetFileName() const
{
  return m_FileName;
}

void
TrxStreamWriter::SetVoxelToRasMatrix(const MatrixType & matrix)
{
  m_VoxelToRasMatrix = matrix;
  m_HasVoxelToRas = true;
  this->Modified();
}

void
TrxStreamWriter::SetVoxelToLpsMatrix(const MatrixType & matrix)
{
  m_VoxelToLpsMatrix = matrix;
  m_HasVoxelToLps = true;
  this->Modified();
}

void
TrxStreamWriter::SetDimensions(const DimensionsType & dims)
{
  m_Dimensions = dims;
  m_HasDimensions = true;
  this->Modified();
}

void
TrxStreamWriter::RegisterDpsField(const std::string & name, const std::string & dtype)
{
  if (name.empty())
  {
    itkExceptionMacro("DPS field name cannot be empty.");
  }
  auto & field = m_Dps[name];
  field.dtype = dtype;
}

void
TrxStreamWriter::RegisterDpvField(const std::string & name, const std::string & dtype)
{
  if (name.empty())
  {
    itkExceptionMacro("DPV field name cannot be empty.");
  }
  auto & field = m_Dpv[name];
  field.dtype = dtype;
}

void
TrxStreamWriter::EnsureStream()
{
  if (!m_Stream)
  {
    m_Stream = std::make_unique<trx::TrxStream>("float32");
    if (m_PositionsBufferMaxBytes > 0)
    {
      m_Stream->set_positions_buffer_max_bytes(m_PositionsBufferMaxBytes);
    }
    if (m_HasVoxelToRas || m_HasVoxelToLps)
    {
      MatrixType ras = m_HasVoxelToRas ? m_VoxelToRasMatrix : ConvertVoxelToRasFromLps(m_VoxelToLpsMatrix);
      Eigen::Matrix4f affine;
      for (unsigned int row = 0; row < 4; ++row)
      {
        for (unsigned int col = 0; col < 4; ++col)
        {
          affine(row, col) = static_cast<float>(ras[row][col]);
        }
      }
      m_Stream->set_voxel_to_rasmm(affine);
    }
    if (m_HasDimensions)
    {
      std::array<uint16_t, 3> dims{ m_Dimensions[0], m_Dimensions[1], m_Dimensions[2] };
      m_Stream->set_dimensions(dims);
    }
  }
}

void
TrxStreamWriter::ValidateDpsValues(const std::map<std::string, double> & dpsValues) const
{
  if (m_Dps.empty())
  {
    if (!dpsValues.empty())
    {
      itkExceptionMacro("DPS values provided without registered fields.");
    }
    return;
  }
  if (dpsValues.size() != m_Dps.size())
  {
    itkExceptionMacro("DPS values missing entries for registered fields.");
  }
  for (const auto & kv : m_Dps)
  {
    if (dpsValues.find(kv.first) == dpsValues.end())
    {
      itkExceptionMacro("Missing DPS value for field: " << kv.first);
    }
  }
}

void
TrxStreamWriter::ValidateDpvValues(const std::map<std::string, std::vector<double>> & dpvValues,
                                   size_t                                            pointCount) const
{
  if (m_Dpv.empty())
  {
    if (!dpvValues.empty())
    {
      itkExceptionMacro("DPV values provided without registered fields.");
    }
    return;
  }
  if (dpvValues.size() != m_Dpv.size())
  {
    itkExceptionMacro("DPV values missing entries for registered fields.");
  }
  for (const auto & kv : m_Dpv)
  {
    auto it = dpvValues.find(kv.first);
    if (it == dpvValues.end())
    {
      itkExceptionMacro("Missing DPV values for field: " << kv.first);
    }
    if (it->second.size() != pointCount)
    {
      itkExceptionMacro("DPV values size does not match streamline point count for field: " << kv.first);
    }
  }
}

std::vector<float>
TrxStreamWriter::FlattenPointsToRas(const StreamlineType & points) const
{
  std::vector<float> xyz;
  xyz.reserve(points.size() * 3);
  for (const auto & point : points)
  {
    // LPS -> RAS
    xyz.push_back(static_cast<float>(-point[0]));
    xyz.push_back(static_cast<float>(-point[1]));
    xyz.push_back(static_cast<float>(point[2]));
  }
  return xyz;
}

std::vector<float>
TrxStreamWriter::FlattenPointsToRas(const vnl_matrix<double> & points) const
{
  if (points.columns() != 3)
  {
    itkExceptionMacro("Streamline matrix must have 3 columns.");
  }
  std::vector<float> xyz;
  xyz.reserve(static_cast<size_t>(points.rows()) * 3);
  for (unsigned int i = 0; i < points.rows(); ++i)
  {
    const double x = points(i, 0);
    const double y = points(i, 1);
    const double z = points(i, 2);
    // LPS -> RAS
    xyz.push_back(static_cast<float>(-x));
    xyz.push_back(static_cast<float>(-y));
    xyz.push_back(static_cast<float>(z));
  }
  return xyz;
}

void
TrxStreamWriter::PushStreamline(const StreamlineType &                             points,
                                const std::map<std::string, double> &              dpsValues,
                                const std::map<std::string, std::vector<double>> & dpvValues,
                                const std::vector<std::string> &                   groupNames)
{
  if (m_Finalized)
  {
    itkExceptionMacro("Cannot push streamline after finalize.");
  }
  EnsureStream();

  ValidateDpsValues(dpsValues);
  ValidateDpvValues(dpvValues, points.size());

  const auto xyz = FlattenPointsToRas(points);
  m_Stream->push_streamline(xyz);

  for (const auto & kv : m_Dps)
  {
    auto & field = m_Dps[kv.first];
    const auto it = dpsValues.find(kv.first);
    field.values.push_back(it->second);
  }

  for (const auto & kv : m_Dpv)
  {
    auto & field = m_Dpv[kv.first];
    const auto it = dpvValues.find(kv.first);
    field.values.insert(field.values.end(), it->second.begin(), it->second.end());
  }

  for (const auto & group : groupNames)
  {
    m_Groups[group].push_back(static_cast<uint32_t>(m_StreamlineCount));
  }

  m_VertexCount += points.size();
  ++m_StreamlineCount;
}

void
TrxStreamWriter::PushStreamline(const vnl_matrix<double> &                          points,
                                const std::map<std::string, double> &               dpsValues,
                                const std::map<std::string, std::vector<double>> &  dpvValues,
                                const std::vector<std::string> &                    groupNames)
{
  if (m_Finalized)
  {
    itkExceptionMacro("Cannot push streamline after finalize.");
  }
  EnsureStream();

  ValidateDpsValues(dpsValues);
  ValidateDpvValues(dpvValues, points.rows());

  const auto xyz = FlattenPointsToRas(points);
  m_Stream->push_streamline(xyz);

  for (const auto & kv : m_Dps)
  {
    auto & field = m_Dps[kv.first];
    const auto it = dpsValues.find(kv.first);
    field.values.push_back(it->second);
  }

  for (const auto & kv : m_Dpv)
  {
    auto & field = m_Dpv[kv.first];
    const auto it = dpvValues.find(kv.first);
    field.values.insert(field.values.end(), it->second.begin(), it->second.end());
  }

  for (const auto & group : groupNames)
  {
    m_Groups[group].push_back(static_cast<uint32_t>(m_StreamlineCount));
  }

  m_VertexCount += points.rows();
  ++m_StreamlineCount;
}

void
TrxStreamWriter::Finalize()
{
  if (m_Finalized)
  {
    itkExceptionMacro("Finalize called multiple times.");
  }
  if (m_FileName.empty())
  {
    itkExceptionMacro("FileName is empty.");
  }
  EnsureStream();

  for (const auto & kv : m_Dps)
  {
    if (kv.second.values.size() != m_StreamlineCount)
    {
      itkExceptionMacro("DPS values count does not match number of streamlines for field: " << kv.first);
    }
    m_Stream->push_dps_from_vector(kv.first, kv.second.dtype, kv.second.values);
  }

  for (const auto & kv : m_Dpv)
  {
    if (kv.second.values.size() != m_VertexCount)
    {
      itkExceptionMacro("DPV values count does not match number of vertices for field: " << kv.first);
    }
    m_Stream->push_dpv_from_vector(kv.first, kv.second.dtype, kv.second.values);
  }

  for (const auto & kv : m_Groups)
  {
    m_Stream->push_group_from_indices(kv.first, kv.second);
  }

  const zip_uint32_t compression = m_UseCompression ? ZIP_CM_DEFLATE : ZIP_CM_STORE;
  m_Stream->finalize<float>(m_FileName, compression);
  m_Finalized = true;
}
} // end namespace itk
