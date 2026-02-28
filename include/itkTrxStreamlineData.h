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
#ifndef itkTrxStreamlineData_h
#define itkTrxStreamlineData_h

#include "TractographyTRXExport.h"

#include "itkDataObject.h"
#include "itkIntTypes.h"
#include "itkFixedArray.h"
#include "itkMatrix.h"
#include "itkPoint.h"

#include "itk_eigen.h"
#include ITK_EIGEN(Core)
#include "vnl/vnl_matrix.h"
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace itk
{
class TrxHandleBase;
class TrxStreamWriter;
/**
 * \class TrxStreamlineData
 * \brief Data object holding TRX streamline points and offsets.
 *
 * Positions are stored as a contiguous array of XYZ triplets. Offsets define the
 * start index of each streamline in the positions array.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxStreamlineData : public DataObject
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxStreamlineData);

  using Self = TrxStreamlineData;
  using Superclass = DataObject;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxStreamlineData);

  enum class CoordinateType
  {
    Float16,
    Float32,
    Float64
  };

  enum class CoordinateSystem
  {
    RAS, // TRX native space
    LPS  // ITK-preferred space
  };

  using OffsetType = uint64_t;
  using SizeValueType = itk::SizeValueType;
  using MatrixType = Matrix<double, 4, 4>;
  using DimensionsType = FixedArray<uint16_t, 3>;
  using PointType = itk::Point<double, 3>;
  using StreamlineType = std::vector<PointType>;
  using AabbType = std::array<double, 6>;

  struct StreamlineView
  {
    const void *        xyz{ nullptr };
    SizeValueType       pointCount{ 0 };
    CoordinateType      coordinateType{ CoordinateType::Float32 };
    CoordinateSystem    coordinateSystem{ CoordinateSystem::LPS };
  };

  class TractographyTRX_EXPORT StreamlinePointRange
  {
  public:
    class TractographyTRX_EXPORT Iterator
    {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = PointType;
      using difference_type = std::ptrdiff_t;
      using pointer = void;
      using reference = PointType;

      Iterator() = default;
      Iterator(const TrxStreamlineData * data, SizeValueType index);

      PointType
      operator*() const;

      Iterator &
      operator++();

      bool
      operator==(const Iterator & other) const;

      bool
      operator!=(const Iterator & other) const;

    private:
      const TrxStreamlineData * m_Data{ nullptr };
      SizeValueType             m_Index{ 0 };
    };

    StreamlinePointRange() = default;
    StreamlinePointRange(const TrxStreamlineData * data, SizeValueType start, SizeValueType end);

    Iterator
    begin() const;

    Iterator
    end() const;

  private:
    const TrxStreamlineData * m_Data{ nullptr };
    SizeValueType             m_Start{ 0 };
    SizeValueType             m_End{ 0 };
  };

  std::vector<PointType>
  GetStreamline(SizeValueType streamlineIndex) const;

  StreamlinePointRange
  GetStreamlineRange(SizeValueType streamlineIndex) const;

  /** Return a zero-copy view over a streamline when positions are loaded. */
  bool
  GetStreamlineView(SizeValueType streamlineIndex, StreamlineView & view) const;

  /**
   * Iterate streamlines and provide raw XYZ pointers.
   * If requestedSystem is LPS and the backing data is RAS-only, each
   * streamline is converted into a temporary buffer before callback.
   */
  void
  ForEachStreamlineChunked(
    const std::function<void(SizeValueType, const void *, SizeValueType, CoordinateType, CoordinateSystem)> & fn,
    CoordinateSystem requestedSystem = CoordinateSystem::LPS) const;

  void
  SetTrxHandle(const std::shared_ptr<TrxHandleBase> & trxHandle);

  bool
  HasTrxHandle() const;

  void
  Save(const std::string & filename, bool useCompression) const;

  CoordinateType
  GetCoordinateType() const;

  bool
  HasFloat16Positions() const;

  bool
  HasFloat32Positions() const;

  bool
  HasFloat64Positions() const;

  const std::vector<Eigen::half> *
  GetFloat16Positions() const;

  const std::vector<float> *
  GetFloat32Positions() const;

  const std::vector<double> *
  GetFloat64Positions() const;

  std::vector<Eigen::half> *
  GetFloat16Positions();

  std::vector<float> *
  GetFloat32Positions();

  std::vector<double> *
  GetFloat64Positions();

  const std::vector<OffsetType> &
  GetOffsets() const;

  void
  SetOffsets(std::vector<OffsetType> && offsets);

  SizeValueType
  GetNumberOfVertices() const;

  SizeValueType
  GetNumberOfStreamlines() const;

  Pointer
  SubsetStreamlines(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult = false) const;

  /**
   * Return a lazy subset when a TRX backing handle is available.
   * Falls back to the eager subset when no handle is present.
   */
  Pointer
  SubsetStreamlinesLazy(const std::vector<uint32_t> & streamlineIds, bool buildCacheForResult = false) const;

  Pointer
  QueryAabb(const PointType & minCornerLps,
            const PointType & maxCornerLps,
            bool              buildCacheForResult = false) const;

  Pointer
  QueryAabb(const PointType & minCornerLps,
            const PointType & maxCornerLps,
            bool              buildCacheForResult,
            size_t            maxStreamlines,
            uint32_t          rngSeed) const;

  const std::vector<AabbType> &
  GetOrBuildStreamlineAabbs() const;

  void
  InvalidateAabbCache() const;

  void
  SetVoxelToRasMatrix(const MatrixType & matrix);

  const MatrixType &
  GetVoxelToRasMatrix() const;

  bool
  HasVoxelToRasMatrix() const;

  void
  SetVoxelToLpsMatrix(const MatrixType & matrix);

  const MatrixType &
  GetVoxelToLpsMatrix() const;

  bool
  HasVoxelToLpsMatrix() const;

  void
  SetDimensions(const DimensionsType & dims);

  const DimensionsType &
  GetDimensions() const;

  bool
  HasDimensions() const;

  void
  SetCoordinateSystem(CoordinateSystem system);

  CoordinateSystem
  GetCoordinateSystem() const;

  void
  FlipXYInPlace();

  template <typename TTransform>
  void
  TransformInPlace(const TTransform * transform);

  template <typename TTransform>
  void
  TransformInPlaceChunked(const TTransform * transform, SizeValueType chunkPoints);

  template <typename TTransform>
  void
  TransformToWriterChunked(const TTransform * transform,
                           class TrxStreamWriter * writer,
                           SizeValueType reservePoints = 0) const;

  template <typename TTransform>
  void
  TransformToWriterChunkedReuseBuffer(const TTransform * transform,
                                      class TrxStreamWriter * writer,
                                      StreamlineType & buffer,
                                      SizeValueType reservePoints = 0) const;

  /** Transform points into a reusable N-by-3 vnl matrix buffer and stream to writer. */
  template <typename TTransform>
  void
  TransformToWriterChunkedReuseVnlBuffer(const TTransform * transform,
                                         class TrxStreamWriter * writer,
                                         vnl_matrix<double> & buffer) const;

  /** Copy internal TRX state without deep copying buffers. */
  void
  Graft(const DataObject * data) override;

protected:
  TrxStreamlineData() = default;
  ~TrxStreamlineData() override = default;

  void
  PrintSelf(std::ostream & os, Indent indent) const override;


private:
  using PositionStorageType = std::variant<std::vector<Eigen::half>, std::vector<float>, std::vector<double>>;

  void
  SetPositions(std::vector<Eigen::half> && positions);

  void
  SetPositions(std::vector<float> && positions);

  void
  SetPositions(std::vector<double> && positions);

  void
  UpdateVertexCount();

  void
  EnsurePositionsLoaded() const;

  PointType
  GetPointLpsAtIndex(SizeValueType index) const;

  mutable PositionStorageType m_Positions;
  std::vector<OffsetType> m_Offsets;
  SizeValueType           m_NumberOfVertices{ 0 };
  CoordinateType          m_FileCoordinateType{ CoordinateType::Float32 };
  std::shared_ptr<TrxHandleBase> m_TrxHandle;
  mutable bool            m_PositionsLoaded{ true };
  mutable std::vector<AabbType> m_AabbCache;
  mutable bool                  m_AabbCacheValid{ false };

  MatrixType       m_VoxelToRasMatrix{};
  MatrixType       m_VoxelToLpsMatrix{};
  DimensionsType   m_Dimensions{ { 0, 0, 0 } };
  bool             m_HasVoxelToRas{ false };
  bool             m_HasVoxelToLps{ false };
  bool             m_HasDimensions{ false };
  CoordinateSystem m_CoordinateSystem{ CoordinateSystem::RAS };
};

std::shared_ptr<TrxHandleBase>
LoadTrxHandle(const std::string & path);

} // end namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#  include "itkTrxStreamlineData.hxx"
#endif

#endif
