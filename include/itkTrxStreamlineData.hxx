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
#ifndef itkTrxStreamlineData_hxx
#define itkTrxStreamlineData_hxx

#include "itkPoint.h"
#include "itkTrxStreamWriter.h"

#include "vnl/vnl_matrix.h"

#include <type_traits>

namespace itk
{
template <typename TTransform>
void
TrxStreamlineData::TransformInPlace(const TTransform * transform)
{
  this->TransformInPlaceChunked(transform, 0);
}

template <typename TTransform>
void
TrxStreamlineData::TransformInPlaceChunked(const TTransform * transform, SizeValueType chunkPoints)
{
  if (transform == nullptr)
  {
    itkExceptionMacro("Transform is null.");
  }

  this->EnsurePositionsLoaded();

  using PointType = itk::Point<double, 3>;

  const SizeValueType total = m_NumberOfVertices;
  const SizeValueType chunk = (chunkPoints == 0) ? total : chunkPoints;

  auto apply = [&](auto & positions) {
    for (SizeValueType start = 0; start < total; start += chunk)
    {
      const SizeValueType end = std::min<SizeValueType>(total, start + chunk);
      for (SizeValueType index = start; index < end; ++index)
      {
        const SizeValueType base = index * 3;
        PointType           point;
        point[0] = static_cast<double>(positions[base]);
        point[1] = static_cast<double>(positions[base + 1]);
        point[2] = static_cast<double>(positions[base + 2]);
        const PointType transformed = transform->TransformPoint(point);
        positions[base] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[0]);
        positions[base + 1] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[1]);
        positions[base + 2] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[2]);
      }
    }
  };

  std::visit(apply, m_Positions);
  m_AabbCacheValid = false;
}

template <typename TTransform>
void
TrxStreamlineData::TransformToWriterChunked(const TTransform * transform,
                                            TrxStreamWriter * writer,
                                            SizeValueType reservePoints) const
{
  if (transform == nullptr)
  {
    itkExceptionMacro("Transform is null.");
  }
  if (writer == nullptr)
  {
    itkExceptionMacro("Writer is null.");
  }

  using StreamlineType = TrxStreamWriter::StreamlineType;
  StreamlineType buffer;
  if (reservePoints > 0)
  {
    buffer.reserve(static_cast<size_t>(reservePoints));
  }

  auto visit = [&](const void * raw,
                   SizeValueType count,
                   CoordinateType coordType,
                   CoordinateSystem coordSystem) {
    if (coordSystem != CoordinateSystem::LPS)
    {
      itkExceptionMacro("TransformToWriterChunked requires LPS coordinates.");
    }
    buffer.resize(static_cast<size_t>(count));
    if (coordType == CoordinateType::Float16)
    {
      const auto * src = static_cast<const Eigen::half *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    else if (coordType == CoordinateType::Float64)
    {
      const auto * src = static_cast<const double *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = src[base];
        point[1] = src[base + 1];
        point[2] = src[base + 2];
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    else
    {
      const auto * src = static_cast<const float *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    writer->PushStreamline(buffer);
  };

  this->ForEachStreamlineChunked(
    [&](SizeValueType, const void * raw, SizeValueType count, CoordinateType coordType, CoordinateSystem coordSystem) {
      if (count == 0)
      {
        writer->PushStreamline(StreamlineType{});
        return;
      }
      visit(raw, count, coordType, coordSystem);
    },
    CoordinateSystem::LPS);
}

template <typename TTransform>
void
TrxStreamlineData::TransformToWriterChunkedReuseBuffer(const TTransform * transform,
                                                       TrxStreamWriter * writer,
                                                       StreamlineType & buffer,
                                                       SizeValueType reservePoints) const
{
  if (transform == nullptr)
  {
    itkExceptionMacro("Transform is null.");
  }
  if (writer == nullptr)
  {
    itkExceptionMacro("Writer is null.");
  }

  if (reservePoints > 0 && buffer.capacity() < static_cast<size_t>(reservePoints))
  {
    buffer.reserve(static_cast<size_t>(reservePoints));
  }

  auto visit = [&](const void * raw,
                   SizeValueType count,
                   CoordinateType coordType,
                   CoordinateSystem coordSystem) {
    if (coordSystem != CoordinateSystem::LPS)
    {
      itkExceptionMacro("TransformToWriterChunkedReuseBuffer requires LPS coordinates.");
    }
    buffer.resize(static_cast<size_t>(count));
    if (coordType == CoordinateType::Float16)
    {
      const auto * src = static_cast<const Eigen::half *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    else if (coordType == CoordinateType::Float64)
    {
      const auto * src = static_cast<const double *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = src[base];
        point[1] = src[base + 1];
        point[2] = src[base + 2];
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    else
    {
      const auto * src = static_cast<const float *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        buffer[static_cast<size_t>(i)] = transform->TransformPoint(point);
      }
    }
    writer->PushStreamline(buffer);
  };

  this->ForEachStreamlineChunked(
    [&](SizeValueType, const void * raw, SizeValueType count, CoordinateType coordType, CoordinateSystem coordSystem) {
      if (count == 0)
      {
        buffer.clear();
        writer->PushStreamline(buffer);
        return;
      }
      visit(raw, count, coordType, coordSystem);
    },
    CoordinateSystem::LPS);
}

template <typename TTransform>
void
TrxStreamlineData::TransformToWriterChunkedReuseVnlBuffer(const TTransform * transform,
                                                          TrxStreamWriter * writer,
                                                          vnl_matrix<double> & buffer) const
{
  if (transform == nullptr)
  {
    itkExceptionMacro("Transform is null.");
  }
  if (writer == nullptr)
  {
    itkExceptionMacro("Writer is null.");
  }

  auto visit = [&](const void * raw,
                   SizeValueType count,
                   CoordinateType coordType,
                   CoordinateSystem coordSystem) {
    if (coordSystem != CoordinateSystem::LPS)
    {
      itkExceptionMacro("TransformToWriterChunkedReuseVnlBuffer requires LPS coordinates.");
    }
    if (count == 0)
    {
      buffer.set_size(0, 3);
      writer->PushStreamline(buffer);
      return;
    }

    buffer.set_size(static_cast<unsigned int>(count), 3);
    if (coordType == CoordinateType::Float16)
    {
      const auto * src = static_cast<const Eigen::half *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        const PointType transformed = transform->TransformPoint(point);
        buffer(static_cast<unsigned int>(i), 0) = transformed[0];
        buffer(static_cast<unsigned int>(i), 1) = transformed[1];
        buffer(static_cast<unsigned int>(i), 2) = transformed[2];
      }
    }
    else if (coordType == CoordinateType::Float64)
    {
      const auto * src = static_cast<const double *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = src[base];
        point[1] = src[base + 1];
        point[2] = src[base + 2];
        const PointType transformed = transform->TransformPoint(point);
        buffer(static_cast<unsigned int>(i), 0) = transformed[0];
        buffer(static_cast<unsigned int>(i), 1) = transformed[1];
        buffer(static_cast<unsigned int>(i), 2) = transformed[2];
      }
    }
    else
    {
      const auto * src = static_cast<const float *>(raw);
      for (SizeValueType i = 0; i < count; ++i)
      {
        const SizeValueType base = i * 3;
        PointType point;
        point[0] = static_cast<double>(src[base]);
        point[1] = static_cast<double>(src[base + 1]);
        point[2] = static_cast<double>(src[base + 2]);
        const PointType transformed = transform->TransformPoint(point);
        buffer(static_cast<unsigned int>(i), 0) = transformed[0];
        buffer(static_cast<unsigned int>(i), 1) = transformed[1];
        buffer(static_cast<unsigned int>(i), 2) = transformed[2];
      }
    }

    writer->PushStreamline(buffer);
  };

  this->ForEachStreamlineChunked(
    [&](SizeValueType, const void * raw, SizeValueType count, CoordinateType coordType, CoordinateSystem coordSystem) {
      visit(raw, count, coordType, coordSystem);
    },
    CoordinateSystem::LPS);
}
} // end namespace itk

#endif
