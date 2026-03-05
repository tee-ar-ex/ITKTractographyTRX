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
#include "itkTrxGroup.h"

#include "itkTrxStreamlineData.h"

namespace itk
{

std::vector<std::string>
TrxGroup::GetDpgFieldNames() const
{
  std::vector<std::string> names;
  names.reserve(m_DpgFields.size());
  for (const auto & kv : m_DpgFields)
  {
    names.push_back(kv.first);
  }
  return names;
}

bool
TrxGroup::HasDpgField(const std::string & name) const
{
  return m_DpgFields.find(name) != m_DpgFields.end();
}

std::vector<float>
TrxGroup::GetDpgField(const std::string & name) const
{
  auto it = m_DpgFields.find(name);
  if (it == m_DpgFields.end())
  {
    return {};
  }
  return it->second;
}

void
TrxGroup::SetName(const std::string & name)
{
  m_Name = name;
}

void
TrxGroup::SetStreamlineIndices(std::vector<uint32_t> indices)
{
  m_StreamlineIndices = std::move(indices);
}

void
TrxGroup::SetDpgFields(std::map<std::string, std::vector<float>> dpgFields)
{
  m_DpgFields = std::move(dpgFields);
}

SmartPointer<TrxStreamlineData>
TrxGroup::GetStreamlines(SmartPointer<const TrxStreamlineData> parent) const
{
  if (!parent)
  {
    return nullptr;
  }
  return parent->SubsetStreamlinesLazy(m_StreamlineIndices);
}

void
TrxGroup::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "Name: " << m_Name << '\n';
  os << indent << "StreamlineCount: " << m_StreamlineIndices.size() << '\n';
  os << indent << "Color: [" << m_Color[0] << ", " << m_Color[1] << ", " << m_Color[2] << "]\n";
  os << indent << "Visible: " << (m_Visible ? "true" : "false") << '\n';
  os << indent << "DpgFields:";
  for (const auto & kv : m_DpgFields)
  {
    os << " " << kv.first;
  }
  os << '\n';
}

} // namespace itk
