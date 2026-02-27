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
#include "itkTrxStreamlineData.h"

#include <trx/trx.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace itk
{
class TrxHandleBase
{
public:
  virtual ~TrxHandleBase() = default;
  virtual TrxStreamlineData::CoordinateType
  GetCoordinateType() const = 0;
  virtual size_t
  NumVertices() const = 0;
  virtual size_t
  NumStreamlines() const = 0;
  virtual void
  FillOffsets(std::vector<TrxStreamlineData::OffsetType> & offsets) const = 0;
  virtual bool
  GetVoxelToRas(TrxStreamlineData::MatrixType & ras) const = 0;
  virtual bool
  GetDimensions(TrxStreamlineData::DimensionsType & dims) const = 0;
  virtual TrxStreamlineData::PointType
  GetPointLpsAtIndex(TrxStreamlineData::SizeValueType index) const = 0;
  virtual void
  ForEachStreamlineRaw(
    const std::function<void(TrxStreamlineData::SizeValueType,
                             const void *,
                             TrxStreamlineData::SizeValueType,
                             TrxStreamlineData::CoordinateType)> & fn) const = 0;
  virtual void
  Save(const std::string & filename, bool useCompression) const = 0;
  virtual std::shared_ptr<TrxHandleBase>
  SubsetStreamlines(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult) const = 0;
  virtual std::shared_ptr<TrxHandleBase>
  QueryAabb(const std::array<float, 3> & minCorner,
            const std::array<float, 3> & maxCorner,
            bool                         buildCacheForResult) const = 0;
  virtual std::shared_ptr<TrxHandleBase>
  QueryAabb(const std::array<float, 3> & minCorner,
            const std::array<float, 3> & maxCorner,
            bool                         buildCacheForResult,
            size_t                       maxStreamlines,
            uint32_t                     rngSeed) const = 0;
  virtual const std::vector<std::array<Eigen::half, 6>> &
  GetOrBuildStreamlineAabbs() const = 0;
  virtual void
  InvalidateAabbCache() const = 0;
};

namespace
{
itk::TrxStreamlineData::MatrixType
ConvertVoxelToLps(const itk::TrxStreamlineData::MatrixType & ras)
{
  itk::TrxStreamlineData::MatrixType lps = ras;
  for (unsigned int col = 0; col < 4; ++col)
  {
    lps[0][col] = -lps[0][col];
    lps[1][col] = -lps[1][col];
  }
  return lps;
}

bool
GetVoxelToRasFromHeader(const json & header, TrxStreamlineData::MatrixType & ras)
{
  const auto rasValue = header["VOXEL_TO_RASMM"];
  if (!rasValue.is_array() || rasValue.array_items().size() != 4)
  {
    return false;
  }
  for (unsigned int row = 0; row < 4; ++row)
  {
    const auto rowValue = rasValue[row];
    if (!rowValue.is_array() || rowValue.array_items().size() != 4)
    {
      return false;
    }
    for (unsigned int col = 0; col < 4; ++col)
    {
      ras[row][col] = static_cast<double>(rowValue[col].number_value());
    }
  }
  return true;
}

bool
GetDimensionsFromHeader(const json & header, TrxStreamlineData::DimensionsType & dims)
{
  const auto dimsValue = header["DIMENSIONS"];
  if (!dimsValue.is_array() || dimsValue.array_items().size() != 3)
  {
    return false;
  }
  for (unsigned int index = 0; index < 3; ++index)
  {
    dims[index] = static_cast<uint16_t>(dimsValue[index].int_value());
  }
  return true;
}
} // namespace

template <typename DT>
class TypedTrxHandle : public TrxHandleBase
{
public:
  explicit TypedTrxHandle(std::unique_ptr<trx::TrxFile<DT>> trx)
    : m_Trx(std::move(trx))
  {
    if (!m_Trx)
    {
      throw std::runtime_error("TRX handle initialized with null TrxFile.");
    }
  }

  TrxStreamlineData::CoordinateType
  GetCoordinateType() const override
  {
    if constexpr (std::is_same_v<DT, Eigen::half>)
    {
      return TrxStreamlineData::CoordinateType::Float16;
    }
    if constexpr (std::is_same_v<DT, double>)
    {
      return TrxStreamlineData::CoordinateType::Float64;
    }
    return TrxStreamlineData::CoordinateType::Float32;
  }

  size_t
  NumVertices() const override
  {
    return m_Trx->num_vertices();
  }

  size_t
  NumStreamlines() const override
  {
    return m_Trx->num_streamlines();
  }

  void
  FillOffsets(std::vector<TrxStreamlineData::OffsetType> & offsets) const override
  {
    offsets.clear();
    if (!m_Trx->streamlines)
    {
      return;
    }
    const auto & mapped = m_Trx->streamlines->_offsets;
    if (mapped.size() <= 1)
    {
      return;
    }
    offsets.reserve(static_cast<size_t>(mapped.size() - 1));
    for (Eigen::Index i = 0; i + 1 < mapped.size(); ++i)
    {
      offsets.push_back(static_cast<TrxStreamlineData::OffsetType>(mapped(i)));
    }
  }

  bool
  GetVoxelToRas(TrxStreamlineData::MatrixType & ras) const override
  {
    return GetVoxelToRasFromHeader(m_Trx->header, ras);
  }

  bool
  GetDimensions(TrxStreamlineData::DimensionsType & dims) const override
  {
    return GetDimensionsFromHeader(m_Trx->header, dims);
  }

  TrxStreamlineData::PointType
  GetPointLpsAtIndex(TrxStreamlineData::SizeValueType index) const override
  {
    TrxStreamlineData::PointType point;
    const auto &                data = m_Trx->streamlines->_data;
    point[0] = -static_cast<double>(data(static_cast<Eigen::Index>(index), 0));
    point[1] = -static_cast<double>(data(static_cast<Eigen::Index>(index), 1));
    point[2] = static_cast<double>(data(static_cast<Eigen::Index>(index), 2));
    return point;
  }

  void
  ForEachStreamlineRaw(
    const std::function<void(TrxStreamlineData::SizeValueType,
                             const void *,
                             TrxStreamlineData::SizeValueType,
                             TrxStreamlineData::CoordinateType)> & fn) const override
  {
    if (!m_Trx->streamlines || m_Trx->streamlines->_offsets.size() == 0)
    {
      return;
    }
    const auto & offsets = m_Trx->streamlines->_offsets;
    const auto * data = m_Trx->streamlines->_data.data();
    const size_t count = static_cast<size_t>(offsets.size() - 1);
    for (size_t i = 0; i < count; ++i)
    {
      const uint64_t start = static_cast<uint64_t>(offsets(static_cast<Eigen::Index>(i), 0));
      const uint64_t end = static_cast<uint64_t>(offsets(static_cast<Eigen::Index>(i + 1), 0));
      if (end <= start)
      {
        fn(static_cast<TrxStreamlineData::SizeValueType>(i), nullptr, 0, GetCoordinateType());
        continue;
      }
      const void * ptr = static_cast<const void *>(data + start * 3);
      fn(static_cast<TrxStreamlineData::SizeValueType>(i),
         ptr,
         static_cast<TrxStreamlineData::SizeValueType>(end - start),
         GetCoordinateType());
    }
  }

  void
  Save(const std::string & filename, bool useCompression) const override
  {
    const zip_uint32_t compression = useCompression ? ZIP_CM_DEFLATE : ZIP_CM_STORE;
    m_Trx->save(filename, compression);
  }

  std::shared_ptr<TrxHandleBase>
  SubsetStreamlines(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult) const override
  {
    auto subset = m_Trx->subset_streamlines(streamlineIds, buildCacheForResult);
    return std::make_shared<TypedTrxHandle<DT>>(std::move(subset));
  }

  std::shared_ptr<TrxHandleBase>
  QueryAabb(const std::array<float, 3> & minCorner,
            const std::array<float, 3> & maxCorner,
            bool                         buildCacheForResult) const override
  {
    auto subset = m_Trx->query_aabb(minCorner, maxCorner, nullptr, buildCacheForResult);
    return std::make_shared<TypedTrxHandle<DT>>(std::move(subset));
  }

  std::shared_ptr<TrxHandleBase>
  QueryAabb(const std::array<float, 3> & minCorner,
            const std::array<float, 3> & maxCorner,
            bool                         buildCacheForResult,
            size_t                       maxStreamlines,
            uint32_t                     rngSeed) const override
  {
    auto subset = m_Trx->query_aabb(minCorner,
                                    maxCorner,
                                    nullptr,
                                    buildCacheForResult,
                                    maxStreamlines,
                                    rngSeed);
    return std::make_shared<TypedTrxHandle<DT>>(std::move(subset));
  }

  const std::vector<std::array<Eigen::half, 6>> &
  GetOrBuildStreamlineAabbs() const override
  {
    return m_Trx->get_or_build_streamline_aabbs();
  }

  void
  InvalidateAabbCache() const override
  {
    m_Trx->invalidate_aabb_cache();
  }

private:
  std::unique_ptr<trx::TrxFile<DT>> m_Trx;
};

std::shared_ptr<TrxHandleBase>
LoadTrxHandle(const std::string & path)
{
  const auto dtype = trx::detect_positions_scalar_type(path, trx::TrxScalarType::Float32);
  switch (dtype)
  {
    case trx::TrxScalarType::Float16:
      return std::make_shared<TypedTrxHandle<Eigen::half>>(trx::load<Eigen::half>(path));
    case trx::TrxScalarType::Float64:
      return std::make_shared<TypedTrxHandle<double>>(trx::load<double>(path));
    case trx::TrxScalarType::Float32:
    default:
      return std::make_shared<TypedTrxHandle<float>>(trx::load<float>(path));
  }
}

TrxStreamlineData::StreamlinePointRange::Iterator::Iterator(const TrxStreamlineData * data, SizeValueType index)
  : m_Data(data)
  , m_Index(index)
{}

TrxStreamlineData::PointType
TrxStreamlineData::StreamlinePointRange::Iterator::operator*() const
{
  return m_Data->GetPointLpsAtIndex(m_Index);
}

TrxStreamlineData::StreamlinePointRange::Iterator &
TrxStreamlineData::StreamlinePointRange::Iterator::operator++()
{
  ++m_Index;
  return *this;
}

bool
TrxStreamlineData::StreamlinePointRange::Iterator::operator==(const Iterator & other) const
{
  return m_Data == other.m_Data && m_Index == other.m_Index;
}

bool
TrxStreamlineData::StreamlinePointRange::Iterator::operator!=(const Iterator & other) const
{
  return !(*this == other);
}

TrxStreamlineData::StreamlinePointRange::StreamlinePointRange(const TrxStreamlineData * data,
                                                              SizeValueType             start,
                                                              SizeValueType             end)
  : m_Data(data)
  , m_Start(start)
  , m_End(end)
{}

TrxStreamlineData::StreamlinePointRange::Iterator
TrxStreamlineData::StreamlinePointRange::begin() const
{
  return Iterator(m_Data, m_Start);
}

TrxStreamlineData::StreamlinePointRange::Iterator
TrxStreamlineData::StreamlinePointRange::end() const
{
  return Iterator(m_Data, m_End);
}

std::vector<TrxStreamlineData::PointType>
TrxStreamlineData::GetStreamline(SizeValueType streamlineIndex) const
{
  auto range = GetStreamlineRange(streamlineIndex);
  std::vector<PointType> points;
  for (const auto & point : range)
  {
    points.push_back(point);
  }
  return points;
}

TrxStreamlineData::StreamlinePointRange
TrxStreamlineData::GetStreamlineRange(SizeValueType streamlineIndex) const
{
  if (streamlineIndex >= m_Offsets.size())
  {
    itkExceptionMacro("Streamline index out of range.");
  }

  const SizeValueType start = static_cast<SizeValueType>(m_Offsets[streamlineIndex]);
  const SizeValueType end = (streamlineIndex + 1 < m_Offsets.size())
                              ? static_cast<SizeValueType>(m_Offsets[streamlineIndex + 1])
                              : m_NumberOfVertices;
  return StreamlinePointRange(this, start, end);
}

bool
TrxStreamlineData::GetStreamlineView(SizeValueType streamlineIndex, StreamlineView & view) const
{
  if (!m_PositionsLoaded || m_Offsets.empty())
  {
    return false;
  }
  if (streamlineIndex >= m_Offsets.size())
  {
    itkExceptionMacro("Streamline index out of range.");
  }

  const SizeValueType start = static_cast<SizeValueType>(m_Offsets[streamlineIndex]);
  const SizeValueType end = (streamlineIndex + 1 < m_Offsets.size())
                              ? static_cast<SizeValueType>(m_Offsets[streamlineIndex + 1])
                              : m_NumberOfVertices;
  if (end <= start)
  {
    view = StreamlineView{};
    return true;
  }

  auto loadView = [&](const auto & positions) {
    using Scalar = typename std::remove_reference_t<decltype(positions)>::value_type;
    view.xyz = static_cast<const void *>(positions.data() + start * 3);
    view.pointCount = static_cast<SizeValueType>(end - start);
    if constexpr (std::is_same_v<Scalar, Eigen::half>)
    {
      view.coordinateType = CoordinateType::Float16;
    }
    else if constexpr (std::is_same_v<Scalar, double>)
    {
      view.coordinateType = CoordinateType::Float64;
    }
    else
    {
      view.coordinateType = CoordinateType::Float32;
    }
    view.coordinateSystem = m_CoordinateSystem;
  };
  std::visit(loadView, m_Positions);
  return true;
}

void
TrxStreamlineData::ForEachStreamlineChunked(
  const std::function<void(SizeValueType, const void *, SizeValueType, CoordinateType, CoordinateSystem)> & fn,
  CoordinateSystem requestedSystem) const
{
  if (!fn)
  {
    return;
  }

  if (m_PositionsLoaded && !m_Offsets.empty())
  {
    auto visit = [&](const auto & positions) {
      using Scalar = typename std::remove_reference_t<decltype(positions)>::value_type;
      CoordinateType coordType = CoordinateType::Float32;
      if constexpr (std::is_same_v<Scalar, Eigen::half>)
      {
        coordType = CoordinateType::Float16;
      }
      else if constexpr (std::is_same_v<Scalar, double>)
      {
        coordType = CoordinateType::Float64;
      }
      for (SizeValueType i = 0; i < static_cast<SizeValueType>(m_Offsets.size()); ++i)
      {
        const SizeValueType start = static_cast<SizeValueType>(m_Offsets[i]);
        const SizeValueType end = (i + 1 < static_cast<SizeValueType>(m_Offsets.size()))
                                    ? static_cast<SizeValueType>(m_Offsets[i + 1])
                                    : m_NumberOfVertices;
        if (end <= start)
        {
          fn(i, nullptr, 0, coordType, requestedSystem);
          continue;
        }
        const SizeValueType count = static_cast<SizeValueType>(end - start);
        const void * ptr = static_cast<const void *>(positions.data() + start * 3);
        if (requestedSystem == m_CoordinateSystem)
        {
          fn(i, ptr, count, coordType, m_CoordinateSystem);
          continue;
        }
        const size_t total = static_cast<size_t>(count) * 3;
        std::vector<Scalar> temp(total);
        for (size_t v = 0; v < total; v += 3)
        {
          temp[v] = static_cast<Scalar>(-static_cast<double>(positions[start * 3 + v]));
          temp[v + 1] = static_cast<Scalar>(-static_cast<double>(positions[start * 3 + v + 1]));
          temp[v + 2] = static_cast<Scalar>(positions[start * 3 + v + 2]);
        }
        fn(i, temp.data(), count, coordType, requestedSystem);
      }
    };
    std::visit(visit, m_Positions);
    return;
  }

  if (!m_TrxHandle)
  {
    return;
  }

  m_TrxHandle->ForEachStreamlineRaw(
    [&](SizeValueType index, const void * raw, SizeValueType count, CoordinateType coordType) {
      if (requestedSystem == CoordinateSystem::RAS)
      {
        fn(index, raw, count, coordType, CoordinateSystem::RAS);
        return;
      }
      if (raw == nullptr || count == 0)
      {
        fn(index, raw, count, coordType, CoordinateSystem::LPS);
        return;
      }

      const size_t total = static_cast<size_t>(count) * 3;
      if (coordType == CoordinateType::Float16)
      {
        const auto * src = static_cast<const Eigen::half *>(raw);
        std::vector<Eigen::half> temp(total);
        for (size_t i = 0; i < total; i += 3)
        {
          temp[i] = static_cast<Eigen::half>(-static_cast<float>(src[i]));
          temp[i + 1] = static_cast<Eigen::half>(-static_cast<float>(src[i + 1]));
          temp[i + 2] = src[i + 2];
        }
        fn(index, temp.data(), count, coordType, CoordinateSystem::LPS);
      }
      else if (coordType == CoordinateType::Float64)
      {
        const auto * src = static_cast<const double *>(raw);
        std::vector<double> temp(total);
        for (size_t i = 0; i < total; i += 3)
        {
          temp[i] = -src[i];
          temp[i + 1] = -src[i + 1];
          temp[i + 2] = src[i + 2];
        }
        fn(index, temp.data(), count, coordType, CoordinateSystem::LPS);
      }
      else
      {
        const auto * src = static_cast<const float *>(raw);
        std::vector<float> temp(total);
        for (size_t i = 0; i < total; i += 3)
        {
          temp[i] = -src[i];
          temp[i + 1] = -src[i + 1];
          temp[i + 2] = src[i + 2];
        }
        fn(index, temp.data(), count, coordType, CoordinateSystem::LPS);
      }
    });
}

void
TrxStreamlineData::SetTrxHandle(const std::shared_ptr<TrxHandleBase> & trxHandle)
{
  if (!trxHandle)
  {
    itkExceptionMacro("TRX file handle is null.");
  }

  m_TrxHandle = trxHandle;
  m_PositionsLoaded = false;

  m_FileCoordinateType = trxHandle->GetCoordinateType();
  switch (m_FileCoordinateType)
  {
    case CoordinateType::Float16:
      m_Positions = std::vector<Eigen::half>();
      break;
    case CoordinateType::Float64:
      m_Positions = std::vector<double>();
      break;
    case CoordinateType::Float32:
    default:
      m_Positions = std::vector<float>();
      break;
  }

  const auto nbStreamlines = trxHandle->NumStreamlines();
  m_Offsets.clear();
  m_Offsets.reserve(nbStreamlines);
  trxHandle->FillOffsets(m_Offsets);

  m_NumberOfVertices = static_cast<SizeValueType>(trxHandle->NumVertices());
  m_CoordinateSystem = CoordinateSystem::LPS;
  m_AabbCacheValid = false;

  TrxStreamlineData::MatrixType ras;
  if (trxHandle->GetVoxelToRas(ras))
  {
    SetVoxelToRasMatrix(ras);
    SetVoxelToLpsMatrix(ConvertVoxelToLps(ras));
  }
  TrxStreamlineData::DimensionsType dims;
  if (trxHandle->GetDimensions(dims))
  {
    SetDimensions(dims);
  }

  this->Modified();
}

bool
TrxStreamlineData::HasTrxHandle() const
{
  return m_TrxHandle != nullptr;
}

void
TrxStreamlineData::Save(const std::string & filename, bool useCompression) const
{
  if (!m_TrxHandle)
  {
    itkExceptionMacro("Cannot save without a TRX backing handle.");
  }
  m_TrxHandle->Save(filename, useCompression);
}

void
TrxStreamlineData::SetPositions(std::vector<Eigen::half> && positions)
{
  // Accept caller-owned buffer, track element count, and mark dirty.
  m_Positions = std::move(positions);
  m_FileCoordinateType = CoordinateType::Float16;
  m_PositionsLoaded = true;
  m_TrxHandle.reset();
  m_CoordinateSystem = CoordinateSystem::LPS;
  m_AabbCacheValid = false;
  UpdateVertexCount();
  this->Modified();
}

void
TrxStreamlineData::SetPositions(std::vector<float> && positions)
{
  // Accept caller-owned buffer, track element count, and mark dirty.
  m_Positions = std::move(positions);
  m_FileCoordinateType = CoordinateType::Float32;
  m_PositionsLoaded = true;
  m_TrxHandle.reset();
  m_CoordinateSystem = CoordinateSystem::LPS;
  m_AabbCacheValid = false;
  UpdateVertexCount();
  this->Modified();
}

void
TrxStreamlineData::SetPositions(std::vector<double> && positions)
{
  // Accept caller-owned buffer, track element count, and mark dirty.
  m_Positions = std::move(positions);
  m_FileCoordinateType = CoordinateType::Float64;
  m_PositionsLoaded = true;
  m_TrxHandle.reset();
  m_CoordinateSystem = CoordinateSystem::LPS;
  m_AabbCacheValid = false;
  UpdateVertexCount();
  this->Modified();
}

TrxStreamlineData::CoordinateType
TrxStreamlineData::GetCoordinateType() const
{
  return m_FileCoordinateType;
}

bool
TrxStreamlineData::HasFloat16Positions() const
{
  return m_FileCoordinateType == CoordinateType::Float16;
}

bool
TrxStreamlineData::HasFloat32Positions() const
{
  return m_FileCoordinateType == CoordinateType::Float32;
}

bool
TrxStreamlineData::HasFloat64Positions() const
{
  return m_FileCoordinateType == CoordinateType::Float64;
}

const std::vector<Eigen::half> *
TrxStreamlineData::GetFloat16Positions() const
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<Eigen::half>>(&m_Positions);
}

const std::vector<float> *
TrxStreamlineData::GetFloat32Positions() const
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<float>>(&m_Positions);
}

const std::vector<double> *
TrxStreamlineData::GetFloat64Positions() const
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<double>>(&m_Positions);
}

std::vector<Eigen::half> *
TrxStreamlineData::GetFloat16Positions()
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<Eigen::half>>(&m_Positions);
}

std::vector<float> *
TrxStreamlineData::GetFloat32Positions()
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<float>>(&m_Positions);
}

std::vector<double> *
TrxStreamlineData::GetFloat64Positions()
{
  EnsurePositionsLoaded();
  return std::get_if<std::vector<double>>(&m_Positions);
}

const std::vector<TrxStreamlineData::OffsetType> &
TrxStreamlineData::GetOffsets() const
{
  return m_Offsets;
}

void
TrxStreamlineData::SetOffsets(std::vector<OffsetType> && offsets)
{
  m_Offsets = std::move(offsets);
  m_AabbCacheValid = false;
  this->Modified();
}

SizeValueType
TrxStreamlineData::GetNumberOfVertices() const
{
  return m_NumberOfVertices;
}

SizeValueType
TrxStreamlineData::GetNumberOfStreamlines() const
{
  return static_cast<SizeValueType>(m_Offsets.size());
}

void
TrxStreamlineData::SetVoxelToRasMatrix(const MatrixType & matrix)
{
  m_VoxelToRasMatrix = matrix;
  m_HasVoxelToRas = true;
  this->Modified();
}

const TrxStreamlineData::MatrixType &
TrxStreamlineData::GetVoxelToRasMatrix() const
{
  return m_VoxelToRasMatrix;
}

bool
TrxStreamlineData::HasVoxelToRasMatrix() const
{
  return m_HasVoxelToRas;
}

void
TrxStreamlineData::SetVoxelToLpsMatrix(const MatrixType & matrix)
{
  m_VoxelToLpsMatrix = matrix;
  m_HasVoxelToLps = true;
  this->Modified();
}

const TrxStreamlineData::MatrixType &
TrxStreamlineData::GetVoxelToLpsMatrix() const
{
  return m_VoxelToLpsMatrix;
}

bool
TrxStreamlineData::HasVoxelToLpsMatrix() const
{
  return m_HasVoxelToLps;
}

void
TrxStreamlineData::SetDimensions(const DimensionsType & dims)
{
  m_Dimensions = dims;
  m_HasDimensions = true;
  this->Modified();
}

const TrxStreamlineData::DimensionsType &
TrxStreamlineData::GetDimensions() const
{
  return m_Dimensions;
}

bool
TrxStreamlineData::HasDimensions() const
{
  return m_HasDimensions;
}

void
TrxStreamlineData::SetCoordinateSystem(CoordinateSystem system)
{
  m_CoordinateSystem = system;
  this->Modified();
}

TrxStreamlineData::CoordinateSystem
TrxStreamlineData::GetCoordinateSystem() const
{
  return m_CoordinateSystem;
}

void
TrxStreamlineData::FlipXYInPlace()
{
  EnsurePositionsLoaded();
  auto flip = [&](auto & positions) {
    for (SizeValueType index = 0; index < m_NumberOfVertices; ++index)
    {
      const SizeValueType base = index * 3;
      positions[base] = -positions[base];
      positions[base + 1] = -positions[base + 1];
    }
  };

  std::visit(flip, m_Positions);
}

namespace
{
template <typename TPositions, typename TScalar>
void
CopySubsetFromLps(const TPositions & positions,
                  const std::vector<TrxStreamlineData::OffsetType> & offsets,
                  TrxStreamlineData::SizeValueType                   nbVertices,
                  const std::vector<uint32_t> &                      selected,
                  std::vector<TScalar> &                             outPositions,
                  std::vector<TrxStreamlineData::OffsetType> &        outOffsets)
{
  size_t totalVertices = 0;
  for (uint32_t idx : selected)
  {
    const uint64_t start = offsets[idx];
    const uint64_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : static_cast<uint64_t>(nbVertices);
    totalVertices += static_cast<size_t>(end - start);
  }

  outPositions.resize(totalVertices * 3);
  outOffsets.resize(selected.size());

  size_t cursor = 0;
  for (size_t i = 0; i < selected.size(); ++i)
  {
    const uint32_t idx = selected[i];
    outOffsets[i] = static_cast<TrxStreamlineData::OffsetType>(cursor);
    const uint64_t start = offsets[idx];
    const uint64_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : static_cast<uint64_t>(nbVertices);
    for (uint64_t p = start; p < end; ++p, ++cursor)
    {
      const size_t base = static_cast<size_t>(p) * 3;
      outPositions[cursor * 3] = static_cast<TScalar>(positions[base]);
      outPositions[cursor * 3 + 1] = static_cast<TScalar>(positions[base + 1]);
      outPositions[cursor * 3 + 2] = static_cast<TScalar>(positions[base + 2]);
    }
  }
}

template <typename TScalar>
void
CopySubsetFromRas(const trx::TypedArray &                       positions,
                  const std::vector<TrxStreamlineData::OffsetType> & offsets,
                  TrxStreamlineData::SizeValueType                   nbVertices,
                  const std::vector<uint32_t> &                      selected,
                  std::vector<TScalar> &                             outPositions,
                  std::vector<TrxStreamlineData::OffsetType> &        outOffsets)
{
  const auto matrix = positions.as_matrix<TScalar>();

  size_t totalVertices = 0;
  for (uint32_t idx : selected)
  {
    const uint64_t start = offsets[idx];
    const uint64_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : static_cast<uint64_t>(nbVertices);
    totalVertices += static_cast<size_t>(end - start);
  }

  outPositions.resize(totalVertices * 3);
  outOffsets.resize(selected.size());

  size_t cursor = 0;
  for (size_t i = 0; i < selected.size(); ++i)
  {
    const uint32_t idx = selected[i];
    outOffsets[i] = static_cast<TrxStreamlineData::OffsetType>(cursor);
    const uint64_t start = offsets[idx];
    const uint64_t end = (idx + 1 < offsets.size()) ? offsets[idx + 1] : static_cast<uint64_t>(nbVertices);
    for (uint64_t p = start; p < end; ++p, ++cursor)
    {
      outPositions[cursor * 3] = static_cast<TScalar>(-matrix(static_cast<Eigen::Index>(p), 0));
      outPositions[cursor * 3 + 1] = static_cast<TScalar>(-matrix(static_cast<Eigen::Index>(p), 1));
      outPositions[cursor * 3 + 2] = static_cast<TScalar>(matrix(static_cast<Eigen::Index>(p), 2));
    }
  }
}
} // namespace

TrxStreamlineData::Pointer
TrxStreamlineData::SubsetStreamlines(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult) const
{
  if (m_TrxHandle)
  {
    auto handle = m_TrxHandle->SubsetStreamlines(streamlineIds, buildCacheForResult);
    auto output = TrxStreamlineData::New();
    output->SetTrxHandle(handle);
    return output;
  }

  if (streamlineIds.empty() || m_Offsets.empty())
  {
    return TrxStreamlineData::New();
  }

  const size_t nbStreamlines = m_Offsets.size();
  std::vector<uint32_t> selected;
  selected.reserve(streamlineIds.size());
  std::vector<uint8_t> seen(nbStreamlines, 0);
  for (uint32_t id : streamlineIds)
  {
    if (id >= nbStreamlines)
    {
      itkExceptionMacro("Streamline id out of range.");
    }
    if (!seen[id])
    {
      selected.push_back(id);
      seen[id] = 1;
    }
  }

  if (selected.empty())
  {
    return TrxStreamlineData::New();
  }

  auto output = TrxStreamlineData::New();
  output->m_FileCoordinateType = m_FileCoordinateType;
  output->m_CoordinateSystem = CoordinateSystem::LPS;

  if (m_HasVoxelToRas)
  {
    output->SetVoxelToRasMatrix(m_VoxelToRasMatrix);
  }
  if (m_HasVoxelToLps)
  {
    output->SetVoxelToLpsMatrix(m_VoxelToLpsMatrix);
  }
  if (m_HasDimensions)
  {
    output->SetDimensions(m_Dimensions);
  }

  if (m_PositionsLoaded)
  {
    auto copyFromLps = [&](const auto & positions) {
      using Scalar = typename std::remove_reference_t<decltype(positions)>::value_type;
      std::vector<Scalar> outPositions;
      std::vector<OffsetType> outOffsets;
      CopySubsetFromLps(positions, m_Offsets, m_NumberOfVertices, selected, outPositions, outOffsets);
      output->SetPositions(std::move(outPositions));
      output->SetOffsets(std::move(outOffsets));
    };
    std::visit(copyFromLps, m_Positions);
  }
  else
  {
    itkExceptionMacro("No positions available to subset.");
  }

  return output;
}

TrxStreamlineData::Pointer
TrxStreamlineData::SubsetStreamlinesLazy(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult) const
{
  if (m_TrxHandle)
  {
    auto handle = m_TrxHandle->SubsetStreamlines(streamlineIds, buildCacheForResult);
    auto output = TrxStreamlineData::New();
    output->SetTrxHandle(handle);
    return output;
  }
  return SubsetStreamlines(streamlineIds, buildCacheForResult);
}

TrxStreamlineData::Pointer
TrxStreamlineData::QueryAabb(const PointType & minCornerLps,
                             const PointType & maxCornerLps,
                             bool buildCacheForResult) const
{
  return QueryAabb(minCornerLps, maxCornerLps, buildCacheForResult, 0, 42);
}

TrxStreamlineData::Pointer
TrxStreamlineData::QueryAabb(const PointType & minCornerLps,
                             const PointType & maxCornerLps,
                             bool              buildCacheForResult,
                             size_t            maxStreamlines,
                             uint32_t          rngSeed) const
{
  PointType rasMinInput;
  rasMinInput[0] = -minCornerLps[0];
  rasMinInput[1] = -minCornerLps[1];
  rasMinInput[2] = minCornerLps[2];
  PointType rasMaxInput;
  rasMaxInput[0] = -maxCornerLps[0];
  rasMaxInput[1] = -maxCornerLps[1];
  rasMaxInput[2] = maxCornerLps[2];
  PointType rasMin;
  rasMin[0] = std::min(rasMinInput[0], rasMaxInput[0]);
  rasMin[1] = std::min(rasMinInput[1], rasMaxInput[1]);
  rasMin[2] = std::min(rasMinInput[2], rasMaxInput[2]);
  PointType rasMax;
  rasMax[0] = std::max(rasMinInput[0], rasMaxInput[0]);
  rasMax[1] = std::max(rasMinInput[1], rasMaxInput[1]);
  rasMax[2] = std::max(rasMinInput[2], rasMaxInput[2]);

  if (m_TrxHandle)
  {
    std::array<float, 3> minCorner{ static_cast<float>(rasMin[0]),
                                    static_cast<float>(rasMin[1]),
                                    static_cast<float>(rasMin[2]) };
    std::array<float, 3> maxCorner{ static_cast<float>(rasMax[0]),
                                    static_cast<float>(rasMax[1]),
                                    static_cast<float>(rasMax[2]) };
    auto handle = (maxStreamlines > 0)
                    ? m_TrxHandle->QueryAabb(minCorner, maxCorner, buildCacheForResult, maxStreamlines, rngSeed)
                    : m_TrxHandle->QueryAabb(minCorner, maxCorner, buildCacheForResult);
    auto output = TrxStreamlineData::New();
    output->SetTrxHandle(handle);
    return output;
  }

  if (m_Offsets.empty())
  {
    return TrxStreamlineData::New();
  }

  std::vector<uint32_t> selected;
  selected.reserve(m_Offsets.size());

  if (m_PositionsLoaded)
  {
    auto queryFromLps = [&](const auto & positions) {
      for (size_t i = 0; i < m_Offsets.size(); ++i)
      {
        const uint64_t start = m_Offsets[i];
        const uint64_t end = (i + 1 < m_Offsets.size()) ? m_Offsets[i + 1]
                                                        : static_cast<uint64_t>(m_NumberOfVertices);
        if (end <= start)
        {
          continue;
        }

        double minX = std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double minZ = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        double maxZ = -std::numeric_limits<double>::infinity();

        for (uint64_t p = start; p < end; ++p)
        {
          const size_t base = static_cast<size_t>(p) * 3;
          const double x = static_cast<double>(positions[base]);
          const double y = static_cast<double>(positions[base + 1]);
          const double z = static_cast<double>(positions[base + 2]);
          minX = std::min(minX, x);
          minY = std::min(minY, y);
          minZ = std::min(minZ, z);
          maxX = std::max(maxX, x);
          maxY = std::max(maxY, y);
          maxZ = std::max(maxZ, z);
        }

        if (minX <= maxCornerLps[0] && maxX >= minCornerLps[0] &&
            minY <= maxCornerLps[1] && maxY >= minCornerLps[1] &&
            minZ <= maxCornerLps[2] && maxZ >= minCornerLps[2])
        {
          selected.push_back(static_cast<uint32_t>(i));
        }
      }
    };
    std::visit(queryFromLps, m_Positions);
  }
  else
  {
    itkExceptionMacro("No positions available for AABB query.");
  }

  if (maxStreamlines > 0 && selected.size() > maxStreamlines)
  {
    std::mt19937 rng(rngSeed);
    std::shuffle(selected.begin(), selected.end(), rng);
    selected.resize(maxStreamlines);
    std::sort(selected.begin(), selected.end());
  }

  return SubsetStreamlines(selected);
}

const std::vector<TrxStreamlineData::AabbType> &
TrxStreamlineData::GetOrBuildStreamlineAabbs() const
{
  if (m_AabbCacheValid)
  {
    return m_AabbCache;
  }

  m_AabbCache.clear();

  if (m_TrxHandle)
  {
    const auto & rasAabbs = m_TrxHandle->GetOrBuildStreamlineAabbs();
    m_AabbCache.reserve(rasAabbs.size());
    for (const auto & ras : rasAabbs)
    {
      const double minX = static_cast<double>(ras[0]);
      const double minY = static_cast<double>(ras[1]);
      const double minZ = static_cast<double>(ras[2]);
      const double maxX = static_cast<double>(ras[3]);
      const double maxY = static_cast<double>(ras[4]);
      const double maxZ = static_cast<double>(ras[5]);
      const double lpsMinX = std::min(-maxX, -minX);
      const double lpsMaxX = std::max(-maxX, -minX);
      const double lpsMinY = std::min(-maxY, -minY);
      const double lpsMaxY = std::max(-maxY, -minY);
      m_AabbCache.push_back({ lpsMinX, lpsMinY, minZ, lpsMaxX, lpsMaxY, maxZ });
    }
    m_AabbCacheValid = true;
    return m_AabbCache;
  }

  if (!m_PositionsLoaded || m_Offsets.empty())
  {
    itkExceptionMacro("No positions available to build AABB cache.");
  }

  m_AabbCache.reserve(m_Offsets.size());
  auto buildFromLps = [&](const auto & positions) {
    for (size_t i = 0; i < m_Offsets.size(); ++i)
    {
      const uint64_t start = m_Offsets[i];
      const uint64_t end = (i + 1 < m_Offsets.size()) ? m_Offsets[i + 1]
                                                      : static_cast<uint64_t>(m_NumberOfVertices);
      if (end <= start)
      {
        m_AabbCache.push_back({ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
        continue;
      }
      double minX = std::numeric_limits<double>::infinity();
      double minY = std::numeric_limits<double>::infinity();
      double minZ = std::numeric_limits<double>::infinity();
      double maxX = -std::numeric_limits<double>::infinity();
      double maxY = -std::numeric_limits<double>::infinity();
      double maxZ = -std::numeric_limits<double>::infinity();
      for (uint64_t p = start; p < end; ++p)
      {
        const size_t base = static_cast<size_t>(p) * 3;
        const double x = static_cast<double>(positions[base]);
        const double y = static_cast<double>(positions[base + 1]);
        const double z = static_cast<double>(positions[base + 2]);
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        minZ = std::min(minZ, z);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
        maxZ = std::max(maxZ, z);
      }
      m_AabbCache.push_back({ minX, minY, minZ, maxX, maxY, maxZ });
    }
  };
  std::visit(buildFromLps, m_Positions);
  m_AabbCacheValid = true;
  return m_AabbCache;
}

void
TrxStreamlineData::InvalidateAabbCache() const
{
  m_AabbCacheValid = false;
  m_AabbCache.clear();
  if (m_TrxHandle)
  {
    m_TrxHandle->InvalidateAabbCache();
  }
}

void
TrxStreamlineData::UpdateVertexCount()
{
  m_NumberOfVertices = 0;
  auto update = [&](const auto & positions) {
    const auto size = positions.size();
    if (size % 3 != 0)
    {
      itkExceptionMacro("Positions buffer size is not a multiple of 3.");
    }
    m_NumberOfVertices = static_cast<SizeValueType>(size / 3);
  };
  std::visit(update, m_Positions);
}

void
TrxStreamlineData::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "NumberOfVertices: " << m_NumberOfVertices << '\n';
  os << indent << "NumberOfStreamlines: " << m_Offsets.size() << '\n';
  os << indent << "CoordinateType: " << static_cast<int>(GetCoordinateType()) << '\n';
}

void
TrxStreamlineData::Graft(const DataObject * data)
{
  Superclass::Graft(data);

  const auto * trxData = dynamic_cast<const TrxStreamlineData *>(data);
  if (!trxData)
  {
    return;
  }

  m_Positions = trxData->m_Positions;
  m_Offsets = trxData->m_Offsets;
  m_NumberOfVertices = trxData->m_NumberOfVertices;
  m_FileCoordinateType = trxData->m_FileCoordinateType;
  m_TrxHandle = trxData->m_TrxHandle;
  m_PositionsLoaded = trxData->m_PositionsLoaded;

  m_VoxelToRasMatrix = trxData->m_VoxelToRasMatrix;
  m_VoxelToLpsMatrix = trxData->m_VoxelToLpsMatrix;
  m_Dimensions = trxData->m_Dimensions;
  m_HasVoxelToRas = trxData->m_HasVoxelToRas;
  m_HasVoxelToLps = trxData->m_HasVoxelToLps;
  m_HasDimensions = trxData->m_HasDimensions;
  m_CoordinateSystem = trxData->m_CoordinateSystem;
}

void
TrxStreamlineData::EnsurePositionsLoaded() const
{
  if (m_PositionsLoaded)
  {
    return;
  }
  if (!m_TrxHandle)
  {
    itkExceptionMacro("Lazy positions requested without a TRX backing file.");
  }

  const size_t nbVertices = m_TrxHandle->NumVertices();
  if (m_FileCoordinateType == CoordinateType::Float16)
  {
    std::vector<Eigen::half> out(nbVertices * 3);
    for (size_t i = 0; i < nbVertices; ++i)
    {
      const PointType point = m_TrxHandle->GetPointLpsAtIndex(static_cast<SizeValueType>(i));
      out[i * 3] = static_cast<Eigen::half>(point[0]);
      out[i * 3 + 1] = static_cast<Eigen::half>(point[1]);
      out[i * 3 + 2] = static_cast<Eigen::half>(point[2]);
    }
    m_Positions = std::move(out);
  }
  else if (m_FileCoordinateType == CoordinateType::Float64)
  {
    std::vector<double> out(nbVertices * 3);
    for (size_t i = 0; i < nbVertices; ++i)
    {
      const PointType point = m_TrxHandle->GetPointLpsAtIndex(static_cast<SizeValueType>(i));
      out[i * 3] = point[0];
      out[i * 3 + 1] = point[1];
      out[i * 3 + 2] = point[2];
    }
    m_Positions = std::move(out);
  }
  else
  {
    std::vector<float> out(nbVertices * 3);
    for (size_t i = 0; i < nbVertices; ++i)
    {
      const PointType point = m_TrxHandle->GetPointLpsAtIndex(static_cast<SizeValueType>(i));
      out[i * 3] = static_cast<float>(point[0]);
      out[i * 3 + 1] = static_cast<float>(point[1]);
      out[i * 3 + 2] = static_cast<float>(point[2]);
    }
    m_Positions = std::move(out);
  }

  m_PositionsLoaded = true;
}

TrxStreamlineData::PointType
TrxStreamlineData::GetPointLpsAtIndex(SizeValueType index) const
{
  if (index >= m_NumberOfVertices)
  {
    itkExceptionMacro("Point index out of range.");
  }

  if (m_PositionsLoaded)
  {
    PointType point;
    auto      fetch = [&](const auto & positions) {
      const SizeValueType base = index * 3;
      point[0] = static_cast<double>(positions[base]);
      point[1] = static_cast<double>(positions[base + 1]);
      point[2] = static_cast<double>(positions[base + 2]);
    };
    std::visit(fetch, m_Positions);
    return point;
  }

  if (!m_TrxHandle)
  {
    itkExceptionMacro("Lazy point access requested without a TRX backing file.");
  }

  return m_TrxHandle->GetPointLpsAtIndex(index);
}
} // end namespace itk
