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
#ifndef itkTrxGroup_h
#define itkTrxGroup_h

#include "TractographyTRXExport.h"
#include "itkFixedArray.h"
#include "itkObject.h"
#include "itkObjectFactory.h"
#include "itkSmartPointer.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace itk
{
class TrxStreamlineData; // forward declaration — include itkTrxStreamlineData.h to use GetStreamlines()

/**
 * \class TrxGroup
 * \brief Represents a named group of streamlines within a TRX file.
 *
 * \note Construction and initialization are performed exclusively by
 * TrxStreamlineData::GetGroup(). Do not construct TrxGroup objects directly.
 *
 * A TRX file can contain named groups (e.g., "CST_left", "AF_right"), each of
 * which is a subset of the total streamline set. TrxGroup stores the group's
 * name, its streamline indices, any per-group scalar fields (DPG), and GUI
 * display properties (color and visibility).
 *
 * ## Typical usage
 *
 * Groups are obtained through TrxStreamlineData, not constructed directly:
 *
 * ```cpp
 * #include "itkTrxFileReader.h"
 * #include "itkTrxStreamlineData.h"   // also brings in itkTrxGroup.h
 *
 * auto reader = itk::TrxFileReader::New();
 * reader->SetFileName("bundle.trx");
 * reader->Update();
 * auto data = reader->GetOutput();
 *
 * // Enumerate groups
 * for (const auto & name : data->GetGroupNames())
 * {
 *   auto group = data->GetGroup(name);
 *   std::cout << name << ": " << group->GetStreamlineIndices().size()
 *             << " streamlines\n";
 * }
 *
 * // Get the streamlines belonging to one group (lazy subset, no copy)
 * auto group = data->GetGroup("CST_left");
 * auto subset = group->GetStreamlines(data);   // TrxStreamlineData::Pointer
 *
 * // GUI: toggle visibility (fires Modified(), observers notified)
 * group->SetVisible(false);
 *
 * // GUI: change color
 * itk::TrxGroup::ColorType red = {{ 1.0f, 0.0f, 0.0f }};
 * group->SetColor(red);
 * ```
 *
 * ## Color assignment
 *
 * If the TRX file contains a DPG (data-per-group) field named "color" with at
 * least 3 float values for this group, those values are used as the initial
 * RGB color in [0, 1]. Otherwise a color is auto-assigned from a 10-entry
 * perceptually distinct palette, cycling by group index.
 *
 * ## Observer pattern
 *
 * TrxGroup inherits from itk::Object, so ITK's observer mechanism works:
 *
 * ```cpp
 * group->AddObserver(itk::ModifiedEvent(), myCallback);
 * group->SetVisible(false);   // myCallback is invoked
 * ```
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxGroup : public Object
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxGroup);

  using Self = TrxGroup;
  using Superclass = Object;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  /** RGB color in [0, 1]. */
  using ColorType = FixedArray<float, 3>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxGroup);

  // -----------------------------------------------------------------------
  // File-derived state (set at load time, not intended for external mutation)
  // -----------------------------------------------------------------------

  /** Name of this group as stored in the TRX file. */
  itkGetConstReferenceMacro(Name, std::string);

  /**
   * Indices into the parent TrxStreamlineData identifying which streamlines
   * belong to this group. Indices are in the range [0, N) where N is the
   * total number of streamlines in the parent object.
   */
  itkGetConstReferenceMacro(StreamlineIndices, std::vector<uint32_t>);

  // -----------------------------------------------------------------------
  // Per-group scalar fields (DPG)
  // -----------------------------------------------------------------------

  /** Names of all data-per-group (DPG) fields stored for this group. */
  std::vector<std::string>
  GetDpgFieldNames() const;

  /** True if a DPG field with the given name exists. */
  bool
  HasDpgField(const std::string & name) const;

  /**
   * Values of a DPG field, widened to float regardless of on-disk dtype.
   * For a scalar field this is a single-element vector. For a matrix field
   * the values are returned flat in row-major order.
   * Returns empty if the field does not exist.
   */
  std::vector<float>
  GetDpgField(const std::string & name) const;

  // -----------------------------------------------------------------------
  // GUI display state (observable via ITK Modified/observers)
  // -----------------------------------------------------------------------

  /** Whether this group should be rendered. Changing fires Modified(). */
  itkSetMacro(Visible, bool);
  itkGetConstMacro(Visible, bool);
  itkBooleanMacro(Visible);

  /**
   * RGB color in [0, 1] for rendering this group.
   * Initialized from the DPG "color" field if present, otherwise
   * auto-assigned from a palette. Changing fires Modified().
   */
  itkSetMacro(Color, ColorType);
  itkGetConstMacro(Color, ColorType);

  // -----------------------------------------------------------------------
  // Streamline access
  // -----------------------------------------------------------------------

  /**
   * Return a TrxStreamlineData containing only the streamlines in this
   * group. The subset is created lazily (no position data is copied; the
   * backing TRX handle is remapped). Pass the parent TrxStreamlineData
   * that this group was obtained from.
   *
   * Callers must #include "itkTrxStreamlineData.h" to use this method.
   */
  SmartPointer<TrxStreamlineData>
  GetStreamlines(TrxStreamlineData::ConstPointer parent) const;

protected:
  TrxGroup() = default;
  ~TrxGroup() override = default;

  void
  PrintSelf(std::ostream & os, Indent indent) const override;

private:
  friend class TrxStreamlineData;

  /** Set group name — called once at construction by TrxStreamlineData. */
  void
  SetName(const std::string & name);

  /** Set streamline index list — called once at construction by TrxStreamlineData. */
  void
  SetStreamlineIndices(std::vector<uint32_t> indices);

  /** Set DPG field map — called once at construction by TrxStreamlineData. */
  void
  SetDpgFields(std::map<std::string, std::vector<float>> dpgFields);

  std::string                               m_Name;
  std::vector<uint32_t>                     m_StreamlineIndices;
  std::map<std::string, std::vector<float>> m_DpgFields;
  ColorType                                 m_Color{ { 1.0f, 1.0f, 1.0f } };
  bool                                      m_Visible{ true };
};

} // namespace itk

#endif
