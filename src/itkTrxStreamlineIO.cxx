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
#include "itkTrxStreamlineIO.h"

#include "itkTrxStreamlineData.h"

#include "itksys/SystemTools.hxx"

namespace itk
{
std::shared_ptr<TrxHandleBase>
LoadTrxHandle(const std::string & path);

bool
TrxStreamlineIO::CanReadFile(const char * filename)
{
  if (filename == nullptr || filename[0] == '\0')
  {
    return false;
  }

  const std::string name(filename);
  if (itksys::SystemTools::FileIsDirectory(name))
  {
    const std::string headerPath = itksys::SystemTools::CollapseFullPath("header.json", name);
    return itksys::SystemTools::FileExists(headerPath, true);
  }

  if (!itksys::SystemTools::FileExists(name, true))
  {
    return false;
  }

  const std::string ext = itksys::SystemTools::GetFilenameLastExtension(name);
  return ext == ".trx" || ext == ".zip";
}

bool
TrxStreamlineIO::CanWriteFile(const char * filename)
{
  if (filename == nullptr || filename[0] == '\0')
  {
    return false;
  }
  const std::string name(filename);
  if (itksys::SystemTools::FileIsDirectory(name))
  {
    return true;
  }
  const std::string ext = itksys::SystemTools::GetFilenameLastExtension(name);
  return ext.empty() || ext == ".trx" || ext == ".zip";
}

void
TrxStreamlineIO::Read()
{
  if (m_FileName.empty())
  {
    itkExceptionMacro("FileName is empty.");
  }
  if (!CanReadFile(m_FileName.c_str()))
  {
    itkExceptionMacro("Cannot read TRX file: " << m_FileName);
  }

  auto output = TrxStreamlineData::New();
  output->SetTrxHandle(LoadTrxHandle(m_FileName));
  this->SetOutput(output);
}

void
TrxStreamlineIO::Write()
{
  if (m_FileName.empty())
  {
    itkExceptionMacro("FileName is empty.");
  }
  const auto * input = this->GetInput();
  if (input == nullptr)
  {
    itkExceptionMacro("Input is null.");
  }
  input->Save(m_FileName, m_UseCompression);
}
} // end namespace itk
