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
#include "itkTrxStreamlineData.h"
#include "itkTrxStreamWriter.h"

#include "itkCommand.h"

#include "itksys/SystemTools.hxx"

#include <trx/trx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

static void
IncrementCounter(const itk::Object *, const itk::EventObject &, void * data)
{
  (*static_cast<int *>(data))++;
}

bool
EnsureDir(const std::string & path)
{
  if (path.empty())
  {
    return true;
  }
  return itksys::SystemTools::MakeDirectory(path).IsSuccess();
}

void
CleanupPath(const std::string & path)
{
  if (itksys::SystemTools::FileIsDirectory(path))
  {
    itksys::SystemTools::RemoveADirectory(path);
    return;
  }
  if (itksys::SystemTools::FileExists(path, true))
  {
    itksys::SystemTools::RemoveFile(path);
  }
}

// Build a simple identity RAS matrix
itk::TrxStreamWriter::MatrixType
MakeIdentityRas()
{
  itk::TrxStreamWriter::MatrixType ras;
  ras.SetIdentity();
  return ras;
}

// Build simple dimensions
itk::TrxStreamWriter::DimensionsType
MakeDims()
{
  itk::TrxStreamWriter::DimensionsType dims;
  dims[0] = 10;
  dims[1] = 10;
  dims[2] = 10;
  return dims;
}

// Make a simple streamline of n points starting at (base, base, base)
itk::TrxStreamWriter::StreamlineType
MakeStreamline(int base, int nPoints)
{
  itk::TrxStreamWriter::StreamlineType pts;
  pts.reserve(static_cast<size_t>(nPoints));
  for (int i = 0; i < nPoints; ++i)
  {
    itk::Point<double, 3> p;
    p[0] = base + i;
    p[1] = base + i;
    p[2] = base + i;
    pts.push_back(p);
  }
  return pts;
}

// -----------------------------------------------------------------------
// Test 1: Group names and indices round-trip
// -----------------------------------------------------------------------
bool
TestGroupNamesAndIndices(const std::string & basePath)
{
  const std::string outputPath = basePath + "_names.trx";
  CleanupPath(outputPath);
  struct Guard
  {
    std::string path;
    ~Guard() { CleanupPath(path); }
  } guard{ outputPath };

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outputPath);
  writer->SetUseCompression(true);
  writer->SetVoxelToRasMatrix(MakeIdentityRas());
  writer->SetDimensions(MakeDims());

  // streamlines 0,1,2 → GroupA; streamlines 3,4 → GroupB
  writer->PushStreamline(MakeStreamline(0, 3), {}, {}, { "GroupA" });
  writer->PushStreamline(MakeStreamline(10, 4), {}, {}, { "GroupA" });
  writer->PushStreamline(MakeStreamline(20, 2), {}, {}, { "GroupA" });
  writer->PushStreamline(MakeStreamline(30, 5), {}, {}, { "GroupB" });
  writer->PushStreamline(MakeStreamline(40, 3), {}, {}, { "GroupB" });
  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[NamesAndIndices] Failed to read output.\n";
    return false;
  }

  if (!data->HasGroups())
  {
    std::cerr << "[NamesAndIndices] HasGroups() returned false.\n";
    return false;
  }

  auto names = data->GetGroupNames();
  if (names.size() != 2)
  {
    std::cerr << "[NamesAndIndices] Expected 2 group names, got " << names.size() << "\n";
    return false;
  }

  // Verify GroupA
  auto groupA = data->GetGroup("GroupA");
  if (!groupA)
  {
    std::cerr << "[NamesAndIndices] GetGroup('GroupA') returned nullptr.\n";
    return false;
  }
  const auto & indicesA = groupA->GetStreamlineIndices();
  if (indicesA.size() != 3)
  {
    std::cerr << "[NamesAndIndices] GroupA expected 3 indices, got " << indicesA.size() << "\n";
    return false;
  }
  std::vector<uint32_t> expectedA = { 0, 1, 2 };
  auto                  sortedA = indicesA;
  std::sort(sortedA.begin(), sortedA.end());
  if (sortedA != expectedA)
  {
    std::cerr << "[NamesAndIndices] GroupA indices mismatch.\n";
    return false;
  }

  // Verify GroupB
  auto groupB = data->GetGroup("GroupB");
  if (!groupB)
  {
    std::cerr << "[NamesAndIndices] GetGroup('GroupB') returned nullptr.\n";
    return false;
  }
  const auto & indicesB = groupB->GetStreamlineIndices();
  if (indicesB.size() != 2)
  {
    std::cerr << "[NamesAndIndices] GroupB expected 2 indices, got " << indicesB.size() << "\n";
    return false;
  }
  std::vector<uint32_t> expectedB = { 3, 4 };
  auto                  sortedB = indicesB;
  std::sort(sortedB.begin(), sortedB.end());
  if (sortedB != expectedB)
  {
    std::cerr << "[NamesAndIndices] GroupB indices mismatch.\n";
    return false;
  }

  // Verify caching: second call should return same pointer
  auto groupA2 = data->GetGroup("GroupA");
  if (groupA.GetPointer() != groupA2.GetPointer())
  {
    std::cerr << "[NamesAndIndices] GetGroup cache miss on second call.\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 2: DPG field round-trip (uses trx-cpp directly to write DPG)
// -----------------------------------------------------------------------
bool
TestGroupDpgField(const std::string & basePath)
{
  const std::string writePath = basePath + "_dpg_base.trx";
  const std::string outputPath = basePath + "_dpg.trx";
  CleanupPath(writePath);
  CleanupPath(outputPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        CleanupPath(p);
    }
  } guard{ { writePath, outputPath } };

  // 1. Write TRX with groups via TrxStreamWriter
  {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(writePath);
    writer->SetUseCompression(true);
    writer->SetVoxelToRasMatrix(MakeIdentityRas());
    writer->SetDimensions(MakeDims());
    writer->PushStreamline(MakeStreamline(0, 3), {}, {}, { "GroupA" });
    writer->PushStreamline(MakeStreamline(10, 2), {}, {}, { "GroupB" });
    writer->Finalize();
  }

  // 2. Load with trx-cpp, add DPG "weight" field, save
  {
    auto trx = trx::load<float>(writePath);
    if (!trx)
    {
      std::cerr << "[DpgField] Failed to load with trx::load.\n";
      return false;
    }
    std::vector<float> wA = { 0.5f };
    std::vector<float> wB = { 0.8f };
    trx->add_dpg_from_vector("GroupA", "weight", "float32", wA, 1, 1);
    trx->add_dpg_from_vector("GroupB", "weight", "float32", wB, 1, 1);
    trx->save(outputPath);
  }

  // 3. Re-read via TrxFileReader and verify
  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[DpgField] Failed to read DPG output.\n";
    return false;
  }

  auto groupA = data->GetGroup("GroupA");
  if (!groupA)
  {
    std::cerr << "[DpgField] GetGroup('GroupA') returned nullptr.\n";
    return false;
  }
  if (!groupA->HasDpgField("weight"))
  {
    std::cerr << "[DpgField] GroupA missing DPG 'weight' field.\n";
    return false;
  }
  auto wA = groupA->GetDpgField("weight");
  if (wA.empty() || std::abs(wA[0] - 0.5f) > 1e-5f)
  {
    std::cerr << "[DpgField] GroupA 'weight' value mismatch: " << (wA.empty() ? 0.0f : wA[0]) << "\n";
    return false;
  }

  auto groupB = data->GetGroup("GroupB");
  if (!groupB)
  {
    std::cerr << "[DpgField] GetGroup('GroupB') returned nullptr.\n";
    return false;
  }
  auto wB = groupB->GetDpgField("weight");
  if (wB.empty() || std::abs(wB[0] - 0.8f) > 1e-5f)
  {
    std::cerr << "[DpgField] GroupB 'weight' value mismatch: " << (wB.empty() ? 0.0f : wB[0]) << "\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 3: Color from DPG field
// -----------------------------------------------------------------------
bool
TestGroupColorFromDpg(const std::string & basePath)
{
  // Write as an uncompressed directory so we can inject a multi-component
  // DPG color file directly into the known directory layout without relying
  // on trx-cpp internal members.
  const std::string writePath = basePath + "_color_base"; // directory TRX (no .trx)
  const std::string outputPath = basePath + "_color.trx";
  CleanupPath(writePath);
  CleanupPath(outputPath);

  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        CleanupPath(p);
    }
  } guard;
  guard.paths = { writePath, outputPath };

  {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(writePath);
    writer->SetUseCompression(false); // write as directory
    writer->SetVoxelToRasMatrix(MakeIdentityRas());
    writer->SetDimensions(MakeDims());
    writer->PushStreamline(MakeStreamline(0, 2), {}, {}, { "GroupA" });
    writer->Finalize();
  }

  const std::array<float, 3> expectedColor = { 0.1f, 0.2f, 0.3f };
  {
    // Inject multi-component DPG color field directly into the directory layout.
    // The TRX directory spec stores DPG under dpg/{group_name}/{field}.{ncols}.{dtype}
    const std::string dpgGroupDir = writePath + "/dpg/GroupA";
    itksys::SystemTools::MakeDirectory(dpgGroupDir);
    const std::string colorFile = dpgGroupDir + "/color.3.float32";
    std::ofstream ofs(colorFile, std::ios::binary);
    if (!ofs)
    {
      std::cerr << "[ColorFromDpg] Failed to open color file for writing.\n";
      return false;
    }
    ofs.write(reinterpret_cast<const char *>(expectedColor.data()), 3 * sizeof(float));
    ofs.close();

    // Re-save as a compressed archive.
    auto trx = trx::load<float>(writePath);
    if (!trx)
    {
      std::cerr << "[ColorFromDpg] Failed to load directory TRX.\n";
      return false;
    }
    trx->save(outputPath);
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[ColorFromDpg] Failed to read color output.\n";
    return false;
  }

  auto group = data->GetGroup("GroupA");
  if (!group)
  {
    std::cerr << "[ColorFromDpg] GetGroup('GroupA') returned nullptr.\n";
    return false;
  }

  const auto & color = group->GetColor();
  for (int i = 0; i < 3; ++i)
  {
    if (std::abs(color[i] - expectedColor[static_cast<size_t>(i)]) > 1e-5f)
    {
      std::cerr << "[ColorFromDpg] Color component " << i << " mismatch: " << color[i]
                << " expected " << expectedColor[static_cast<size_t>(i)] << "\n";
      return false;
    }
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 4: Auto color from palette (no DPG color field)
// -----------------------------------------------------------------------
bool
TestGroupAutoColor(const std::string & basePath)
{
  const std::string outputPath = basePath + "_autocolor.trx";
  CleanupPath(outputPath);
  struct Guard
  {
    std::string path;
    ~Guard() { CleanupPath(path); }
  } guard{ outputPath };

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outputPath);
  writer->SetUseCompression(true);
  writer->SetVoxelToRasMatrix(MakeIdentityRas());
  writer->SetDimensions(MakeDims());
  writer->PushStreamline(MakeStreamline(0, 2), {}, {}, { "GroupA" });
  writer->PushStreamline(MakeStreamline(10, 2), {}, {}, { "GroupB" });
  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[AutoColor] Failed to read output.\n";
    return false;
  }

  auto groupA = data->GetGroup("GroupA");
  auto groupB = data->GetGroup("GroupB");
  if (!groupA || !groupB)
  {
    std::cerr << "[AutoColor] One or both groups missing.\n";
    return false;
  }

  const auto & colorA = groupA->GetColor();
  const auto & colorB = groupB->GetColor();

  // Colors should differ from the default white {1,1,1} (palette is used)
  const bool aIsWhite = (colorA[0] == 1.0f && colorA[1] == 1.0f && colorA[2] == 1.0f);
  const bool bIsWhite = (colorB[0] == 1.0f && colorB[1] == 1.0f && colorB[2] == 1.0f);
  if (aIsWhite || bIsWhite)
  {
    std::cerr << "[AutoColor] Expected palette colors, got white.\n";
    return false;
  }

  // The two groups should have different palette entries
  if (colorA[0] == colorB[0] && colorA[1] == colorB[1] && colorA[2] == colorB[2])
  {
    std::cerr << "[AutoColor] Expected distinct palette colors for GroupA and GroupB.\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 5: Visibility observer
// -----------------------------------------------------------------------
bool
TestGroupVisibilityObserver(const std::string & /*basePath*/)
{
  auto group = itk::TrxGroup::New();

  int  callbackCount = 0;
  auto cmd = itk::CStyleCommand::New();
  cmd->SetClientData(&callbackCount);
  cmd->SetConstCallback(IncrementCounter);
  group->AddObserver(itk::ModifiedEvent(), cmd);

  group->SetVisible(false);
  if (callbackCount != 1)
  {
    std::cerr << "[VisibilityObserver] Expected 1 callback after SetVisible, got " << callbackCount << "\n";
    return false;
  }

  group->SetVisible(true);
  if (callbackCount != 2)
  {
    std::cerr << "[VisibilityObserver] Expected 2 callbacks after second SetVisible, got " << callbackCount << "\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 6: GetStreamlines returns the correct subset
// -----------------------------------------------------------------------
bool
TestGroupGetStreamlines(const std::string & basePath)
{
  const std::string outputPath = basePath + "_getstreamlines.trx";
  CleanupPath(outputPath);
  struct Guard
  {
    std::string path;
    ~Guard() { CleanupPath(path); }
  } guard{ outputPath };

  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(outputPath);
  writer->SetUseCompression(true);
  writer->SetVoxelToRasMatrix(MakeIdentityRas());
  writer->SetDimensions(MakeDims());
  writer->PushStreamline(MakeStreamline(0, 3), {}, {}, { "GroupA" });
  writer->PushStreamline(MakeStreamline(10, 2), {}, {}, { "GroupB" });
  writer->PushStreamline(MakeStreamline(20, 4), {}, {}, { "GroupA" });
  writer->Finalize();

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[GetStreamlines] Failed to read output.\n";
    return false;
  }

  auto groupA = data->GetGroup("GroupA");
  if (!groupA)
  {
    std::cerr << "[GetStreamlines] GetGroup('GroupA') returned nullptr.\n";
    return false;
  }

  const size_t expectedCount = groupA->GetStreamlineIndices().size();
  auto         subset = groupA->GetStreamlines(data);
  if (!subset)
  {
    std::cerr << "[GetStreamlines] GetStreamlines returned nullptr.\n";
    return false;
  }

  if (subset->GetNumberOfStreamlines() != static_cast<itk::TrxStreamlineData::SizeValueType>(expectedCount))
  {
    std::cerr << "[GetStreamlines] Subset has " << subset->GetNumberOfStreamlines() << " streamlines, expected "
              << expectedCount << "\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 7 (formerly 9): Comprehensive round-trip: 5 streamlines, 2 groups, DPS, DPV, DPG
// -----------------------------------------------------------------------
bool
TestGroupRoundTrip(const std::string & basePath)
{
  const std::string writePath = basePath + "_rt_base.trx";
  const std::string outputPath = basePath + "_rt.trx";
  CleanupPath(writePath);
  CleanupPath(outputPath);
  struct Guard
  {
    std::vector<std::string> paths;
    ~Guard()
    {
      for (const auto & p : paths)
        CleanupPath(p);
    }
  } guard{ { writePath, outputPath } };

  const std::vector<double> faValues = { 0.1, 0.2, 0.3, 0.7, 0.9 };
  // 5 streamlines: 2 pts each
  const size_t nStreamlines = 5;
  const size_t nVerticesPerSL = 2;

  // DPV signal: one value per vertex
  std::vector<std::vector<double>> sigValues;
  for (size_t i = 0; i < nStreamlines; ++i)
  {
    std::vector<double> v;
    for (size_t j = 0; j < nVerticesPerSL; ++j)
    {
      v.push_back(static_cast<double>(i) * 0.1 + static_cast<double>(j) * 0.01);
    }
    sigValues.push_back(v);
  }

  {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(writePath);
    writer->SetUseCompression(true);
    writer->SetVoxelToRasMatrix(MakeIdentityRas());
    writer->SetDimensions(MakeDims());
    writer->RegisterDpsField("FA", "float32");
    writer->RegisterDpvField("signal", "float32");

    for (size_t i = 0; i < nStreamlines; ++i)
    {
      const std::string grp = (i < 3) ? "GroupA" : "GroupB";
      writer->PushStreamline(MakeStreamline(static_cast<int>(i) * 10, static_cast<int>(nVerticesPerSL)),
                             { { "FA", faValues[i] } },
                             { { "signal", sigValues[i] } },
                             { grp });
    }
    writer->Finalize();
  }

  // Add DPG "weight" field via trx-cpp
  {
    auto trx = trx::load<float>(writePath);
    if (!trx)
    {
      std::cerr << "[RoundTrip] Failed to load intermediate file.\n";
      return false;
    }
    trx->add_dpg_from_vector("GroupA", "weight", "float32", std::vector<float>{ 0.42f }, 1, 1);
    trx->add_dpg_from_vector("GroupB", "weight", "float32", std::vector<float>{ 0.77f }, 1, 1);
    trx->save(outputPath);
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[RoundTrip] Failed to read final output.\n";
    return false;
  }

  // Verify streamline counts
  if (data->GetNumberOfStreamlines() != nStreamlines)
  {
    std::cerr << "[RoundTrip] Expected " << nStreamlines << " streamlines, got "
              << data->GetNumberOfStreamlines() << "\n";
    return false;
  }

  // Verify groups
  auto names = data->GetGroupNames();
  if (names.size() != 2)
  {
    std::cerr << "[RoundTrip] Expected 2 groups, got " << names.size() << "\n";
    return false;
  }
  auto groupA = data->GetGroup("GroupA");
  auto groupB = data->GetGroup("GroupB");
  if (!groupA || !groupB)
  {
    std::cerr << "[RoundTrip] Missing a group.\n";
    return false;
  }
  if (groupA->GetStreamlineIndices().size() != 3 || groupB->GetStreamlineIndices().size() != 2)
  {
    std::cerr << "[RoundTrip] Group index counts wrong.\n";
    return false;
  }

  // Verify DPS FA
  auto fa = data->GetDpsField("FA");
  if (fa.size() != nStreamlines)
  {
    std::cerr << "[RoundTrip] FA field size wrong: " << fa.size() << "\n";
    return false;
  }
  for (size_t i = 0; i < nStreamlines; ++i)
  {
    if (std::abs(fa[i] - static_cast<float>(faValues[i])) > 1e-5f)
    {
      std::cerr << "[RoundTrip] FA[" << i << "] mismatch.\n";
      return false;
    }
  }

  // Verify DPV signal length
  auto sig = data->GetDpvField("signal");
  const size_t expectedSigLen = nStreamlines * nVerticesPerSL;
  if (sig.size() != expectedSigLen)
  {
    std::cerr << "[RoundTrip] signal field size wrong: " << sig.size() << " expected " << expectedSigLen << "\n";
    return false;
  }

  // Verify DPG weight for GroupA
  if (!groupA->HasDpgField("weight"))
  {
    std::cerr << "[RoundTrip] GroupA missing DPG 'weight' field.\n";
    return false;
  }
  auto wA = groupA->GetDpgField("weight");
  if (wA.empty() || std::abs(wA[0] - 0.42f) > 1e-5f)
  {
    std::cerr << "[RoundTrip] GroupA weight mismatch.\n";
    return false;
  }

  // Verify DPG weight for GroupB
  auto wB = groupB->GetDpgField("weight");
  if (wB.empty() || std::abs(wB[0] - 0.77f) > 1e-5f)
  {
    std::cerr << "[RoundTrip] GroupB weight mismatch.\n";
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------
// Test 10: Group connectivity matrix (count and DPS-weighted)
// -----------------------------------------------------------------------
bool
TestGroupConnectivity(const std::string & basePath)
{
  const std::string outputPath = basePath + "_conn.trx";
  CleanupPath(outputPath);
  struct Guard
  {
    std::string path;
    ~Guard() { CleanupPath(path); }
  } guard{ outputPath };

  {
    auto writer = itk::TrxStreamWriter::New();
    writer->SetFileName(outputPath);
    writer->SetUseCompression(true);
    writer->SetVoxelToRasMatrix(MakeIdentityRas());
    writer->SetDimensions(MakeDims());
    writer->RegisterDpsField("weight", "float32");

    // 4 streamlines, overlapping memberships:
    // A: {0,1}, B: {1,2}, C: {2,3}
    writer->PushStreamline(MakeStreamline(0, 2), { { "weight", 1.0 } }, {}, { "A" });
    writer->PushStreamline(MakeStreamline(10, 2), { { "weight", 2.0 } }, {}, { "A", "B" });
    writer->PushStreamline(MakeStreamline(20, 2), { { "weight", 3.0 } }, {}, { "B", "C" });
    writer->PushStreamline(MakeStreamline(30, 2), { { "weight", 4.0 } }, {}, { "C" });
    writer->Finalize();
  }

  auto reader = itk::TrxFileReader::New();
  reader->SetFileName(outputPath);
  reader->Update();
  auto data = reader->GetOutput();
  if (!data)
  {
    std::cerr << "[Connectivity] Failed to read output.\n";
    return false;
  }

  const auto counts = data->ComputeGroupConnectivity("");
  if (counts.groupNames.size() != 3)
  {
    std::cerr << "[Connectivity] Expected 3 groups, got " << counts.groupNames.size() << "\n";
    return false;
  }
  if (counts.matrix.rows() != 3 || counts.matrix.cols() != 3)
  {
    std::cerr << "[Connectivity] Count matrix shape mismatch.\n";
    return false;
  }

  auto idxOf = [&](const std::string & name) -> int {
    for (size_t i = 0; i < counts.groupNames.size(); ++i)
    {
      if (counts.groupNames[i] == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  };
  const int ia = idxOf("A");
  const int ib = idxOf("B");
  const int ic = idxOf("C");
  if (ia < 0 || ib < 0 || ic < 0)
  {
    std::cerr << "[Connectivity] Missing expected groups A/B/C.\n";
    return false;
  }

  auto expectNear = [](double got, double expected, const char * what) -> bool {
    if (std::abs(got - expected) > 1e-6)
    {
      std::cerr << "[Connectivity] " << what << " mismatch: got " << got << " expected " << expected << "\n";
      return false;
    }
    return true;
  };

  // Count-mode expectations
  if (!expectNear(counts.matrix(ia, ia), 2.0, "count A,A"))
    return false;
  if (!expectNear(counts.matrix(ia, ib), 1.0, "count A,B"))
    return false;
  if (!expectNear(counts.matrix(ia, ic), 0.0, "count A,C"))
    return false;
  if (!expectNear(counts.matrix(ib, ib), 2.0, "count B,B"))
    return false;
  if (!expectNear(counts.matrix(ib, ic), 1.0, "count B,C"))
    return false;
  if (!expectNear(counts.matrix(ic, ic), 2.0, "count C,C"))
    return false;
  if (!expectNear(counts.matrix(ib, ia), counts.matrix(ia, ib), "count symmetry AB"))
    return false;

  // Weighted expectations using DPS "weight"
  const auto weighted = data->ComputeGroupConnectivity("weight");
  if (!expectNear(weighted.matrix(ia, ia), 3.0, "weight A,A"))
    return false;
  if (!expectNear(weighted.matrix(ia, ib), 2.0, "weight A,B"))
    return false;
  if (!expectNear(weighted.matrix(ia, ic), 0.0, "weight A,C"))
    return false;
  if (!expectNear(weighted.matrix(ib, ib), 5.0, "weight B,B"))
    return false;
  if (!expectNear(weighted.matrix(ib, ic), 3.0, "weight B,C"))
    return false;
  if (!expectNear(weighted.matrix(ic, ic), 7.0, "weight C,C"))
    return false;
  if (!expectNear(weighted.matrix(ic, ib), weighted.matrix(ib, ic), "weight symmetry BC"))
    return false;

  return true;
}

} // namespace

int
itkTrxGroupTest(int argc, char * argv[])
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " output_base_path" << std::endl;
    return EXIT_FAILURE;
  }

  const std::string basePath = argv[1];
  const std::string outputDir = itksys::SystemTools::GetFilenamePath(basePath);
  if (!EnsureDir(outputDir))
  {
    std::cerr << "Failed to create output directory: " << outputDir << std::endl;
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupNamesAndIndices" << std::endl;
  if (!TestGroupNamesAndIndices(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupDpgField" << std::endl;
  if (!TestGroupDpgField(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupColorFromDpg" << std::endl;
  if (!TestGroupColorFromDpg(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupAutoColor" << std::endl;
  if (!TestGroupAutoColor(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupVisibilityObserver" << std::endl;
  if (!TestGroupVisibilityObserver(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupGetStreamlines" << std::endl;
  if (!TestGroupGetStreamlines(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupRoundTrip" << std::endl;
  if (!TestGroupRoundTrip(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] TestGroupConnectivity" << std::endl;
  if (!TestGroupConnectivity(basePath))
  {
    return EXIT_FAILURE;
  }

  std::cerr << "[GroupTest] All tests passed" << std::endl;
  return EXIT_SUCCESS;
}
