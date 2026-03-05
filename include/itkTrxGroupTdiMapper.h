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
#ifndef itkTrxGroupTdiMapper_h
#define itkTrxGroupTdiMapper_h

#include "TractographyTRXExport.h"

#include "itkImage.h"
#include "itkObject.h"
#include "itkObjectFactory.h"
#include "itkTrxStreamlineData.h"

#include <string>
#include <vector>

namespace itk
{
/**
 * \class TrxGroupTdiMapper
 * \brief Maps one TRX group to a 3D TDI image on a reference NIfTI grid.
 *
 * Streamlines are traversed in native TRX RAS coordinates and mapped to voxel
 * indices using only the caller-provided reference image geometry.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxGroupTdiMapper : public Object
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxGroupTdiMapper);

  using Self = TrxGroupTdiMapper;
  using Superclass = Object;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxGroupTdiMapper);

  using OutputImageType = Image<float, 3>;

  enum class VoxelStatistic
  {
    Sum,
    Mean
  };

  enum class MappingMode
  {
    NearestUniqueVoxelPerStreamline
  };

  struct Options
  {
    VoxelStatistic voxelStatistic{ VoxelStatistic::Sum };
    MappingMode    mappingMode{ MappingMode::NearestUniqueVoxelPerStreamline };
    bool           useDpsWeightField{ false };
    std::string    weightFieldName;
  };

  void
  SetInputFileName(const std::string & filename);

  const std::string &
  GetInputFileName() const;

  void
  SetGroupName(const std::string & groupName);

  const std::string &
  GetGroupName() const;

  void
  SetReferenceImageFileName(const std::string & filename);

  const std::string &
  GetReferenceImageFileName() const;

  void
  SetInput(TrxStreamlineData::ConstPointer input);

  TrxStreamlineData::ConstPointer
  GetInput() const;

  void
  SetOptions(const Options & options);

  const Options &
  GetOptions() const;

  /** Optional explicit streamline selection (indices in input space). */
  void
  SetSelectedStreamlineIds(const std::vector<uint32_t> & ids);

  const std::vector<uint32_t> &
  GetSelectedStreamlineIds() const;

  OutputImageType *
  GetOutput();

  const OutputImageType *
  GetOutput() const;

  /** Build the output image. */
  void
  Update();

  /** Convenience helper for default usage. */
  static OutputImageType::Pointer
  Compute(const std::string & inputTrxFileName,
          const std::string & groupName,
          const std::string & referenceImageFileName);

  /** Convenience helper with custom options. */
  static OutputImageType::Pointer
  Compute(const std::string & inputTrxFileName,
          const std::string & groupName,
          const std::string & referenceImageFileName,
          const Options &     options);

protected:
  TrxGroupTdiMapper() = default;
  ~TrxGroupTdiMapper() override = default;

  void
  PrintSelf(std::ostream & os, Indent indent) const override;

private:
  TrxStreamlineData::ConstPointer m_Input;
  std::string                     m_InputFileName;
  std::string                     m_GroupName;
  std::string                     m_ReferenceImageFileName;
  Options                         m_Options;
  bool                            m_HasSelectedStreamlineIds{ false };
  std::vector<uint32_t>           m_SelectedStreamlineIds;
  OutputImageType::Pointer        m_Output;
};
} // namespace itk

#endif
