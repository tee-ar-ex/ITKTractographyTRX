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

#include <optional>
#include <string>
#include <vector>

namespace itk
{
/**
 * \class TrxGroupTdiMapper
 * \brief Maps a selection of TRX streamlines to a 3D Track Density Image (TDI).
 *
 * Streamlines are selected via a named group (SetGroupName()), an explicit index
 * list (SetSelectedStreamlineIds()), or the intersection of both.  At least one
 * selector must be set before calling Update().
 *
 * Coordinate mapping uses the caller-provided reference NIfTI geometry
 * (SetReferenceImageFileName()) exclusively; the TRX header geometry is ignored.
 * Streamline coordinates are consumed in native TRX RAS space for efficiency,
 * with the RAS-to-voxel transform computed internally from the reference image.
 *
 * ## Voxel statistics
 *
 * Each voxel touched by a streamline is counted at most once per streamline
 * (NearestUniqueVoxelPerStreamline mode).  Two statistics are available:
 * - **Sum** (default): the value accumulated per voxel equals the sum of
 *   per-streamline weights (1.0 unless a DPS weight field is set).
 * - **Mean**: divides the summed value by the number of distinct streamlines
 *   that contributed to each voxel.
 *
 * ## Optional DPS weight field
 *
 * Set `Options::weightField` to the name of a 1D DPS field to use
 * per-streamline weights instead of the default weight of 1.  Streamlines with
 * non-finite or non-positive weights are skipped.
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
    /**
     * If set, the named 1D DPS field is used as a per-streamline weight.
     * Streamlines with non-finite or non-positive weights are skipped.
     * If not set, every streamline contributes a weight of 1.
     */
    std::optional<std::string> weightField;
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
