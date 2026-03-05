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
#include "itkTrxFileReader.h"

#include "itkMacro.h"
#include "itkMath.h"
#include "itksys/SystemTools.hxx"

#include "itkTrxStreamlineIOFactory.h"

namespace itk
{
TrxFileReader::TrxFileReader()
{
  this->SetNumberOfRequiredOutputs(1);
  this->SetNthOutput(0, TrxStreamlineData::New());
}

void
TrxFileReader::SetFileName(const std::string & filename)
{
  if (m_FileName == filename)
  {
    return;
  }
  m_FileName = filename;
  this->Modified();
}

const std::string &
TrxFileReader::GetFileName() const
{
  return m_FileName;
}

TrxStreamlineData *
TrxFileReader::GetOutput()
{
  return itkDynamicCastInDebugMode<TrxStreamlineData *>(this->Superclass::GetOutput(0));
}

const TrxStreamlineData *
TrxFileReader::GetOutput() const
{
  return itkDynamicCastInDebugMode<const TrxStreamlineData *>(this->Superclass::GetOutput(0));
}

DataObject::Pointer
TrxFileReader::MakeOutput(DataObjectPointerArraySizeType itkNotUsed(idx))
{
  return TrxStreamlineData::New();
}

void
TrxFileReader::GenerateData()
{
  if (m_FileName.empty())
  {
    itkExceptionMacro("FileName is empty.");
  }
  if (!itksys::SystemTools::FileIsDirectory(m_FileName) && !itksys::SystemTools::FileExists(m_FileName, true))
  {
    itkExceptionMacro("File does not exist: " << m_FileName);
  }

  auto io = TrxStreamlineIOFactory::CreateTrxStreamlineIO(m_FileName.c_str(), IOFileModeEnum::ReadMode);
  if (!io)
  {
    itkExceptionMacro("No TRX IO available for: " << m_FileName);
  }
  io->SetFileName(m_FileName);
  io->Read();

  auto output = this->GetOutput();
  output->Graft(io->GetOutput());
}
} // end namespace itk
