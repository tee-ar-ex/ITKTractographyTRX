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
#ifndef itkTrxStreamlineData_hxx
#define itkTrxStreamlineData_hxx

#include "itkPoint.h"

#include <type_traits>

namespace itk
{
template <typename TTransform>
void
TrxStreamlineData::TransformInPlace(const TTransform * transform)
{
  if (transform == nullptr)
  {
    itkExceptionMacro("Transform is null.");
  }

  this->EnsurePositionsLoaded();

  using PointType = itk::Point<double, 3>;

  auto apply = [&](auto & positions) {
    for (SizeValueType index = 0; index < m_NumberOfVertices; ++index)
    {
      const SizeValueType base = index * 3;
      PointType           point;
      point[0] = static_cast<double>(positions[base]);
      point[1] = static_cast<double>(positions[base + 1]);
      point[2] = static_cast<double>(positions[base + 2]);
      const PointType transformed = transform->TransformPoint(point);
      positions[base] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[0]);
      positions[base + 1] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[1]);
      positions[base + 2] = static_cast<std::remove_reference_t<decltype(positions[0])>>(transformed[2]);
    }
  };

  std::visit(apply, m_Positions);
}
} // end namespace itk

#endif
