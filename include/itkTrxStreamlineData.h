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
#include "itkFixedArray.h"
#include "itkMatrix.h"
#include "itkPoint.h"

#include "itk_eigen.h"
#include ITK_EIGEN(Core)
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace itk
{
class TrxHandleBase;
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
  using MatrixType = Matrix<double, 4, 4>;
  using DimensionsType = FixedArray<uint16_t, 3>;
  using PointType = itk::Point<double, 3>;

  class StreamlinePointRange
  {
  public:
    class Iterator
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

  Pointer
  QueryAabb(const PointType & minCornerLps,
            const PointType & maxCornerLps,
            bool              buildCacheForResult = false) const;

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
