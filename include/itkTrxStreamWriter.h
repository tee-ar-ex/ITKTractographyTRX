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
#ifndef itkTrxStreamWriter_h
#define itkTrxStreamWriter_h

#include "TractographyTRXExport.h"

#include "itkFixedArray.h"
#include "itkMatrix.h"
#include "itkObject.h"
#include "itkObjectFactory.h"
#include "itkPoint.h"

#include "vnl/vnl_matrix.h"

#include <trx/trx.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace trx
{
class TrxStream;
}

namespace itk
{
/**
 * \class TrxStreamWriter
 * \brief Streaming TRX writer that ingests ITK streamlines and metadata.
 *
 * This class wraps trx::TrxStream and enforces per-streamline synchronization
 * of DPS/DPV fields when pushing streamlines.
 *
 * \ingroup TractographyTRX
 */
class TractographyTRX_EXPORT TrxStreamWriter : public Object
{
public:
  ITK_DISALLOW_COPY_AND_MOVE(TrxStreamWriter);

  using Self = TrxStreamWriter;
  using Superclass = Object;
  using Pointer = SmartPointer<Self>;
  using ConstPointer = SmartPointer<const Self>;

  itkNewMacro(Self);
  itkOverrideGetNameOfClassMacro(TrxStreamWriter);

  using MatrixType = Matrix<double, 4, 4>;
  using DimensionsType = FixedArray<uint16_t, 3>;
  using PointType = itk::Point<double, 3>;
  using StreamlineType = std::vector<PointType>;

  void
  SetFileName(const std::string & filename);

  const std::string &
  GetFileName() const;

  itkSetMacro(UseCompression, bool);
  itkGetConstMacro(UseCompression, bool);
  itkBooleanMacro(UseCompression);

  /** Optional buffer size (bytes) for streaming positions. */
  itkSetMacro(PositionsBufferMaxBytes, size_t);
  itkGetConstMacro(PositionsBufferMaxBytes, size_t);

  void
  SetVoxelToRasMatrix(const MatrixType & matrix);

  void
  SetVoxelToLpsMatrix(const MatrixType & matrix);

  void
  SetDimensions(const DimensionsType & dims);

  /** Register a per-streamline field (DPS). */
  void
  RegisterDpsField(const std::string & name, const std::string & dtype);

  /** Register a per-vertex field (DPV). */
  void
  RegisterDpvField(const std::string & name, const std::string & dtype);

  /** Add a streamline with per-streamline and per-vertex values. */
  void
  PushStreamline(const StreamlineType &                                       points,
                 const std::map<std::string, double> &                        dpsValues = {},
                 const std::map<std::string, std::vector<double>> &           dpvValues = {},
                 const std::vector<std::string> &                             groupNames = {});

  /** Add a streamline provided as an N-by-3 matrix of LPS coordinates. */
  void
  PushStreamline(const vnl_matrix<double> &                                   points,
                 const std::map<std::string, double> &                        dpsValues = {},
                 const std::map<std::string, std::vector<double>> &           dpvValues = {},
                 const std::vector<std::string> &                             groupNames = {});

  /** Finalize and write TRX file. */
  void
  Finalize();

protected:
  TrxStreamWriter();
  ~TrxStreamWriter() override = default;

private:
  struct FieldSpec
  {
    std::string       dtype;
    std::vector<double> values;
  };

  void
  EnsureStream();

  void
  ValidateDpsValues(const std::map<std::string, double> & dpsValues) const;

  void
  ValidateDpvValues(const std::map<std::string, std::vector<double>> & dpvValues,
                    size_t                                            pointCount) const;

  std::vector<float>
  FlattenPointsToRas(const StreamlineType & points) const;

  std::vector<float>
  FlattenPointsToRas(const vnl_matrix<double> & points) const;

  std::string m_FileName{};
  bool        m_UseCompression{ false };
  size_t      m_PositionsBufferMaxBytes{ 0 };

  bool       m_HasVoxelToRas{ false };
  bool       m_HasVoxelToLps{ false };
  bool       m_HasDimensions{ false };
  MatrixType m_VoxelToRasMatrix{};
  MatrixType m_VoxelToLpsMatrix{};
  DimensionsType m_Dimensions{ { 0, 0, 0 } };

  std::unique_ptr<trx::TrxStream> m_Stream;
  size_t                          m_StreamlineCount{ 0 };
  size_t                          m_VertexCount{ 0 };
  bool                            m_Finalized{ false };

  std::map<std::string, FieldSpec>                m_Dps;
  std::map<std::string, FieldSpec>                m_Dpv;
  std::map<std::string, std::vector<uint32_t>>    m_Groups;
};
} // end namespace itk

#endif
