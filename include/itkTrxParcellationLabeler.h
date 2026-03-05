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
#ifndef itkTrxParcellationLabeler_h
#define itkTrxParcellationLabeler_h

#include "TractographyTRXExport.h"

#include "itkObject.h"
#include "itkObjectFactory.h"
#include "itkTrxStreamlineData.h"

#include <map>
#include <string>
#include <vector>

namespace itk
{
/**
 * \class TrxParcellationLabeler
 * \brief Assigns streamlines to TRX groups based on brain parcellation images.
 *
 * Reads one or more NIfTI-1 label images (parcellations), performs a single
 * sequential pass over all streamline coordinates in LPS space, and records
 * which streamlines intersect which region voxels.  Results are written as
 * named groups in a new TRX file that preserves all existing DPS and DPV
 * fields from the input.
 *
 * A streamline is assigned to a region group when **any** of its coordinates
 * falls inside a voxel bearing that region's integer label.  The check is
 * whole-streamline (endpoints are not treated specially).
 *
 * ## Usage
 *
 * ```cpp
 * auto labeler = itk::TrxParcellationLabeler::New();
 * labeler->SetInput(streamlineData);
 * labeler->SetOutputFileName("labeled.trx");
 *
 * itk::TrxParcellationLabeler::ParcellationSpec glasser;
 * glasser.niftiPath      = "seg-Glasser_dseg.nii.gz";
 * glasser.labelFilePath  = "seg-Glasser_dseg.txt";
 * glasser.groupPrefix    = "Glasser";
 * labeler->AddParcellation(glasser);
 *
 * labeler->SetDilationRadius(1);   // optional: expand each region by 1 voxel
 * labeler->Update();
 * ```
 *
 * ## Group naming
 *
 * Groups are named `{prefix}_{label_name}`, e.g. `Glasser_Left_V1`.
 * Pre-existing groups in the input TRX are preserved in the output.
 *
 * ## Coordinate system
 *
 * All computation is performed in ITK's LPS physical coordinate system.
 * NIfTI images are read via `itk::ImageFileReader`, which delivers images in
 * LPS automatically.  The TRX coordinate conversion is handled internally by
 * `TrxStreamlineData::ForEachStreamlineChunked`.  Callers need not be aware
 * of the TRX-internal RAS representation.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxParcellationLabeler : public Object
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxParcellationLabeler);

  using Self = TrxParcellationLabeler;
  using Superclass = Object;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxParcellationLabeler);

  /** Specification for a single brain parcellation atlas. */
  struct ParcellationSpec
  {
    /** Path to a NIfTI-1 label image (.nii or .nii.gz). */
    std::string niftiPath;
    /**
     * Path to a two-column text file mapping integer labels to names.
     * Each non-empty, non-comment line must have the form:
     *   <integer> <whitespace> <name>
     */
    std::string labelFilePath;
    /**
     * Prefix prepended to every group name from this atlas.
     * The final group name is "{prefix}_{label_name}".
     */
    std::string groupPrefix;
  };

  /** Add a parcellation atlas. May be called multiple times. */
  void
  AddParcellation(const ParcellationSpec & spec);

  /** Set the source streamline data. */
  void
  SetInput(TrxStreamlineData::ConstPointer input);

  /**
   * Optional path to the source TRX artifact used for copy-through output.
   *
   * When set, Update() will copy this source artifact to OutputFileName and
   * append computed groups in-place, preserving original coordinate dtype and
   * payload encoding.
   *
   * If unset, Update() falls back to full rewrite mode.
   */
  void
  SetInputFileName(const std::string & filename);

  const std::string &
  GetInputFileName() const;

  /** Set the output TRX file path.
   *  A ".trx" extension produces a zip archive; any other extension (or no
   *  extension) produces an uncompressed directory. */
  void
  SetOutputFileName(const std::string & filename);

  const std::string &
  GetOutputFileName() const;

  /** Bytes in output artifact immediately before group append. */
  size_t
  GetPreGroupFileBytes() const;

  /** Bytes in output artifact after group append (final output). */
  size_t
  GetFinalFileBytes() const;

  /**
   * Morphological dilation radius applied to each label image before lookup,
   * measured in voxels.  A ball-shaped structuring element is used.
   * Set to 0 (default) to skip dilation.
   * Dilation uses a grayscale max-value rule, which expands each label into
   * adjacent background voxels; at region boundaries the higher label value
   * wins.
   */
  itkSetMacro(DilationRadius, unsigned int);
  itkGetConstMacro(DilationRadius, unsigned int);

  /**
   * Maximum bytes of DPV data to load per field.  Fields whose total byte
   * size exceeds this limit are skipped with a warning rather than copied.
   * Default: 512 MiB.
   */
  itkSetMacro(MaxDpvBytes, size_t);
  itkGetConstMacro(MaxDpvBytes, size_t);

  /** Run labeling and write the output TRX file. */
  void
  Update();

protected:
  TrxParcellationLabeler() = default;
  ~TrxParcellationLabeler() override = default;

  void
  PrintSelf(std::ostream & os, Indent indent) const override;

private:
  TrxStreamlineData::ConstPointer m_Input;
  std::string                     m_InputFileName;
  std::vector<ParcellationSpec>   m_Parcellations;
  std::string                     m_OutputFileName;
  unsigned int                    m_DilationRadius{ 0 };
  size_t                          m_MaxDpvBytes{ 512ULL * 1024ULL * 1024ULL };
  size_t                          m_PreGroupFileBytes{ 0 };
  size_t                          m_FinalFileBytes{ 0 };
};

} // namespace itk

#endif
