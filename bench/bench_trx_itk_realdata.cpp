// Benchmark ITKTractographyTRX wrappers against raw trx-cpp access.
#include <benchmark/benchmark.h>
#include <trx/trx.h>

#include "itkTrxFileReader.h"
#include "itkTrxGroupTdiMapper.h"
#include "itkTrxParcellationLabeler.h"
#include "itkTrxStreamWriter.h"
#include "itkTrxStreamlineData.h"
#include "itkTranslationTransform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace {
using Eigen::half;

std::string g_reference_trx_path;
size_t g_reference_streamline_count = 0;
bool g_reference_has_dpv = false;

// Parcellation benchmark inputs (populated from --bench-data-dir or individual flags).
struct ParcellationFiles {
  std::string niftiPath;
  std::string labelPath;
  std::string prefix;
};
std::vector<ParcellationFiles> g_parcellations;

itk::TrxStreamlineData::Pointer g_itk_reference;
std::unique_ptr<trx::TrxFile<half>> g_raw_reference;
std::unordered_map<size_t, std::string> g_subset_trx_artifact_paths;
std::unordered_map<size_t, std::string> g_subset_trx_with_group_paths;
std::mutex g_subset_trx_artifact_mutex;
constexpr const char * kBenchTdiGroupName = "BenchGroup";

constexpr float kSlabThicknessMm = 5.0f;
constexpr size_t kSlabCount = 20;

struct Fov {
  float min_x;
  float max_x;
  float min_y;
  float max_y;
  float min_z;
  float max_z;
};

constexpr Fov kFov{-70.0f, 70.0f, -108.0f, 79.0f, -60.0f, 75.0f};

std::string make_temp_path(const std::string &prefix) {
  static std::atomic<uint64_t> counter{0};
  const auto id = counter.fetch_add(1, std::memory_order_relaxed);
  const auto dir = std::filesystem::temp_directory_path();
#if defined(__unix__) || defined(__APPLE__)
  const auto pid = static_cast<uint64_t>(getpid());
#else
  const auto pid = static_cast<uint64_t>(0);
#endif
  return (dir / (prefix + "_" + std::to_string(pid) + "_" + std::to_string(id) + ".trx")).string();
}

double get_current_rss_kb() {
#if defined(__APPLE__)
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0.0;
  }
  return static_cast<double>(info.resident_size) / 1024.0;
#elif defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      const auto pos = line.find_first_of("0123456789");
      if (pos != std::string::npos) {
        return static_cast<double>(std::stoull(line.substr(pos)));
      }
    }
  }
  return 0.0;
#else
  return 0.0;
#endif
}

size_t parse_env_size(const char *name, size_t default_value) {
  const char *raw = std::getenv(name);
  if (!raw || raw[0] == '\0') {
    return default_value;
  }
  char *end = nullptr;
  const unsigned long long value = std::strtoull(raw, &end, 10);
  if (end == raw) {
    return default_value;
  }
  return static_cast<size_t>(value);
}

bool parse_env_bool(const char *name, bool default_value) {
  const char *raw = std::getenv(name);
  if (!raw || raw[0] == '\0') {
    return default_value;
  }
  return std::string(raw) != "0";
}

std::vector<size_t> streamlines_for_benchmarks() {
  const size_t only = parse_env_size("TRX_BENCH_ONLY_STREAMLINES", 0);
  if (only > 0) {
    return {only};
  }
  const size_t max_val = parse_env_size("TRX_BENCH_MAX_STREAMLINES", 10000000);
  std::vector<size_t> counts = {100000, 500000, 1000000, 5000000, 10000000};
  counts.erase(std::remove_if(counts.begin(), counts.end(),
                              [&](size_t v) { return v > max_val; }),
               counts.end());
  if (counts.empty()) {
    counts.push_back(max_val);
  }
  return counts;
}

std::vector<uint32_t> build_prefix_ids(size_t num_streamlines) {
  std::vector<uint32_t> ids;
  ids.reserve(num_streamlines);
  for (size_t i = 0; i < num_streamlines; ++i) {
    ids.push_back(static_cast<uint32_t>(i));
  }
  return ids;
}

struct Slab {
  std::array<float, 3> min_corner;
  std::array<float, 3> max_corner;
};

std::vector<Slab> build_slabs_ras() {
  std::vector<Slab> slabs;
  slabs.reserve(kSlabCount);
  const float z_range = kFov.max_z - kFov.min_z;
  for (size_t i = 0; i < kSlabCount; ++i) {
    const float t = (kSlabCount == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(kSlabCount - 1);
    const float center_z = kFov.min_z + t * z_range;
    const float min_z = std::max(kFov.min_z, center_z - kSlabThicknessMm * 0.5f);
    const float max_z = std::min(kFov.max_z, center_z + kSlabThicknessMm * 0.5f);
    slabs.push_back({{kFov.min_x, kFov.min_y, min_z}, {kFov.max_x, kFov.max_y, max_z}});
  }
  return slabs;
}

std::vector<Slab> build_slabs_lps() {
  auto slabs = build_slabs_ras();
  for (auto &slab : slabs) {
    const std::array<float, 3> min_ras = slab.min_corner;
    const std::array<float, 3> max_ras = slab.max_corner;
    const std::array<float, 3> min_lps = {-min_ras[0], -min_ras[1], min_ras[2]};
    const std::array<float, 3> max_lps = {-max_ras[0], -max_ras[1], max_ras[2]};
    slab.min_corner = {std::min(min_lps[0], max_lps[0]),
                       std::min(min_lps[1], max_lps[1]),
                       std::min(min_lps[2], max_lps[2])};
    slab.max_corner = {std::max(min_lps[0], max_lps[0]),
                       std::max(min_lps[1], max_lps[1]),
                       std::max(min_lps[2], max_lps[2])};
  }
  return slabs;
}

struct ItkDataset {
  itk::TrxStreamlineData::Pointer data;
};

struct RawDataset {
  std::unique_ptr<trx::TrxFile<half>> data;
};

const ItkDataset &get_itk_subset(size_t streamlines) {
  using Key = size_t;
  static std::unordered_map<Key, ItkDataset> cache;
  const auto found = cache.find(streamlines);
  if (found != cache.end()) {
    return found->second;
  }
  auto ids = build_prefix_ids(streamlines);
  ItkDataset dataset{g_itk_reference->SubsetStreamlines(ids, false)};
  return cache.emplace(streamlines, std::move(dataset)).first->second;
}

const RawDataset &get_raw_subset(size_t streamlines) {
  using Key = size_t;
  static std::unordered_map<Key, RawDataset> cache;
  const auto found = cache.find(streamlines);
  if (found != cache.end()) {
    return found->second;
  }
  auto ids = build_prefix_ids(streamlines);
  RawDataset dataset{g_raw_reference->subset_streamlines(ids, false)};
  return cache.emplace(streamlines, std::move(dataset)).first->second;
}

std::string get_subset_trx_artifact(size_t streamlines) {
  std::lock_guard<std::mutex> lock(g_subset_trx_artifact_mutex);
  const auto found = g_subset_trx_artifact_paths.find(streamlines);
  if (found != g_subset_trx_artifact_paths.end()) {
    return found->second;
  }

  const auto &dataset = get_raw_subset(streamlines);
  const std::string subset_path = make_temp_path("parcellate_subset_input");
  dataset.data->save(subset_path, ZIP_CM_STORE);
  g_subset_trx_artifact_paths.emplace(streamlines, subset_path);
  return subset_path;
}

std::vector<uint32_t> build_bench_group_members(size_t streamlines) {
  std::vector<uint32_t> members;
  if (streamlines == 0) {
    return members;
  }
  const size_t stride = std::max<size_t>(1, streamlines / 10);
  members.reserve((streamlines + stride - 1) / stride);
  for (size_t i = 0; i < streamlines; i += stride) {
    members.push_back(static_cast<uint32_t>(i));
  }
  if (members.empty()) {
    members.push_back(0);
  }
  return members;
}

std::string get_subset_trx_artifact_with_group(size_t streamlines) {
  {
    std::lock_guard<std::mutex> lock(g_subset_trx_artifact_mutex);
    const auto found = g_subset_trx_with_group_paths.find(streamlines);
    if (found != g_subset_trx_with_group_paths.end()) {
      return found->second;
    }
  }

  const std::string grouped_input_path = get_subset_trx_artifact(streamlines);
  const std::string grouped_path = make_temp_path("group_tdi_subset_input");
  std::filesystem::copy(grouped_input_path, grouped_path, std::filesystem::copy_options::overwrite_existing);

  std::map<std::string, std::vector<uint32_t>> groups;
  groups[kBenchTdiGroupName] = build_bench_group_members(streamlines);
  trx::append_groups_to_zip(grouped_path, groups);

  std::lock_guard<std::mutex> lock(g_subset_trx_artifact_mutex);
  g_subset_trx_with_group_paths[streamlines] = grouped_path;
  return grouped_path;
}

void run_itk_translate_write(const itk::TrxStreamlineData::Pointer &data,
                             const std::string &out_path,
                             itk::TrxStreamlineData::StreamlineType &buffer) {
  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(out_path);
  writer->SetUseCompression(false);
  size_t buffer_bytes = parse_env_size("TRX_BENCH_CHUNK_BYTES", 0);
  if (buffer_bytes == 0) {
    buffer_bytes = 256ULL * 1024ULL * 1024ULL;
  }
  buffer_bytes = static_cast<size_t>(buffer_bytes * 1.5);
  if (buffer_bytes > 0) {
    writer->SetPositionsBufferMaxBytes(buffer_bytes);
  }
  auto transform = itk::TranslationTransform<double, 3>::New();
  itk::TranslationTransform<double, 3>::OutputVectorType offset;
  offset[0] = 1.0;
  offset[1] = 1.0;
  offset[2] = 1.0;
  transform->Translate(offset);

  data->TransformToWriterChunkedReuseBuffer(transform.GetPointer(), writer, buffer);
  writer->Finalize();
}

void run_itk_translate_write_vnl(const itk::TrxStreamlineData::Pointer &data,
                                 const std::string &out_path,
                                 vnl_matrix<double> &buffer) {
  auto writer = itk::TrxStreamWriter::New();
  writer->SetFileName(out_path);
  writer->SetUseCompression(false);
  size_t buffer_bytes = parse_env_size("TRX_BENCH_CHUNK_BYTES", 0);
  if (buffer_bytes == 0) {
    buffer_bytes = 256ULL * 1024ULL * 1024ULL;
  }
  buffer_bytes = static_cast<size_t>(buffer_bytes * 1.5);
  if (buffer_bytes > 0) {
    writer->SetPositionsBufferMaxBytes(buffer_bytes);
  }
  auto transform = itk::TranslationTransform<double, 3>::New();
  itk::TranslationTransform<double, 3>::OutputVectorType offset;
  offset[0] = 1.0;
  offset[1] = 1.0;
  offset[2] = 1.0;
  transform->Translate(offset);

  data->TransformToWriterChunkedReuseVnlBuffer(transform.GetPointer(), writer, buffer);
  writer->Finalize();
}

void run_raw_translate_write(const trx::TrxFile<half> &data,
                             size_t streamlines,
                             const std::string &out_path) {
  trx::TrxStream stream("float16");
  const size_t progress_every = parse_env_size("TRX_BENCH_LOG_PROGRESS_EVERY", 0);
  for (size_t i = 0; i < streamlines; ++i) {
    auto raw_points = data.get_streamline(i);
    std::vector<std::array<float, 3>> points;
    points.reserve(raw_points.size());
    for (const auto &p : raw_points) {
      points.push_back({static_cast<float>(p[0]) + 1.0f,
                        static_cast<float>(p[1]) + 1.0f,
                        static_cast<float>(p[2]) + 1.0f});
    }
    stream.push_streamline(points);
    if (progress_every > 0 && (i + 1) % progress_every == 0) {
      std::cerr << "[trx-itk-bench] progress raw translate streamlines=" << (i + 1)
                << " / " << streamlines << std::endl;
    }
  }
  stream.finalize(out_path, trx::TrxScalarType::Float16, ZIP_CM_STORE);
}

static void BM_Itk_TranslateWrite(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  const auto &dataset = get_itk_subset(streamlines);

  double max_rss_delta_kb = 0.0;
  itk::TrxStreamlineData::StreamlineType buffer;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    const auto start = std::chrono::steady_clock::now();
    const std::string out_path = make_temp_path("itk_translate");
    run_itk_translate_write(dataset.data, out_path, buffer);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    std::error_code ec;
    std::filesystem::remove_all(out_path, ec);
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["backend"] = 0.0; // 0 = ITK
  state.counters["positions_dtype"] = 16.0;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void BM_Itk_TranslateWrite_Vnl(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  const auto &dataset = get_itk_subset(streamlines);

  double max_rss_delta_kb = 0.0;
  vnl_matrix<double> buffer;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    const auto start = std::chrono::steady_clock::now();
    const std::string out_path = make_temp_path("itk_translate_vnl");
    run_itk_translate_write_vnl(dataset.data, out_path, buffer);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    std::error_code ec;
    std::filesystem::remove_all(out_path, ec);
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["backend"] = 2.0; // 2 = ITK (vnl buffer)
  state.counters["positions_dtype"] = 16.0;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void BM_Raw_TranslateWrite(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  const auto &dataset = get_raw_subset(streamlines);

  double max_rss_delta_kb = 0.0;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    const auto start = std::chrono::steady_clock::now();
    const std::string out_path = make_temp_path("raw_translate");
    run_raw_translate_write(*dataset.data, streamlines, out_path);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    std::error_code ec;
    std::filesystem::remove_all(out_path, ec);
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["backend"] = 1.0; // 1 = raw trx-cpp
  state.counters["positions_dtype"] = 16.0;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void BM_Itk_QueryAabb(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  const auto &dataset = get_itk_subset(streamlines);
  static const auto slabs = build_slabs_lps();
  const size_t max_query_streamlines = parse_env_size("TRX_BENCH_MAX_QUERY_STREAMLINES", 0);

  double max_rss_delta_kb = 0.0;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    std::vector<double> slab_times_ms;
    slab_times_ms.reserve(kSlabCount);
    const auto start = std::chrono::steady_clock::now();
    size_t total = 0;
    for (size_t i = 0; i < slabs.size(); ++i) {
      const auto &slab = slabs[i];
      itk::TrxStreamlineData::PointType min_corner;
      itk::TrxStreamlineData::PointType max_corner;
      min_corner[0] = slab.min_corner[0];
      min_corner[1] = slab.min_corner[1];
      min_corner[2] = slab.min_corner[2];
      max_corner[0] = slab.max_corner[0];
      max_corner[1] = slab.max_corner[1];
      max_corner[2] = slab.max_corner[2];
      const auto q_start = std::chrono::steady_clock::now();
      auto subset = dataset.data->QueryAabb(min_corner,
                                            max_corner,
                                            false,
                                            max_query_streamlines,
                                            static_cast<uint32_t>(i));
      const auto q_end = std::chrono::steady_clock::now();
      const std::chrono::duration<double, std::milli> q_elapsed = q_end - q_start;
      slab_times_ms.push_back(q_elapsed.count());
      total += subset->GetNumberOfStreamlines();
    }
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    benchmark::DoNotOptimize(total);

    auto sorted = slab_times_ms;
    std::sort(sorted.begin(), sorted.end());
    const auto p50 = sorted[sorted.size() / 2];
    const auto p95_idx = static_cast<size_t>(std::ceil(0.95 * sorted.size())) - 1;
    const auto p95 = sorted[std::min(p95_idx, sorted.size() - 1)];
    state.counters["query_p50_ms"] = p50;
    state.counters["query_p95_ms"] = p95;
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["backend"] = 0.0;
  state.counters["query_count"] = static_cast<double>(kSlabCount);
  state.counters["max_query_streamlines"] = static_cast<double>(max_query_streamlines);
  state.counters["slab_thickness_mm"] = kSlabThicknessMm;
  state.counters["positions_dtype"] = 16.0;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void BM_Raw_QueryAabb(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  const auto &dataset = get_raw_subset(streamlines);
  static const auto slabs = build_slabs_ras();
  const size_t max_query_streamlines = parse_env_size("TRX_BENCH_MAX_QUERY_STREAMLINES", 500);

  double max_rss_delta_kb = 0.0;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    std::vector<double> slab_times_ms;
    slab_times_ms.reserve(kSlabCount);
    const auto start = std::chrono::steady_clock::now();
    size_t total = 0;
    for (size_t i = 0; i < slabs.size(); ++i) {
      const auto &slab = slabs[i];
      const auto q_start = std::chrono::steady_clock::now();
      auto subset = dataset.data->query_aabb(slab.min_corner, slab.max_corner,
                                             nullptr, false, max_query_streamlines,
                                             static_cast<uint32_t>(i));
      const auto q_end = std::chrono::steady_clock::now();
      const std::chrono::duration<double, std::milli> q_elapsed = q_end - q_start;
      slab_times_ms.push_back(q_elapsed.count());
      total += subset->num_streamlines();
      subset->close();
    }
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    benchmark::DoNotOptimize(total);

    auto sorted = slab_times_ms;
    std::sort(sorted.begin(), sorted.end());
    const auto p50 = sorted[sorted.size() / 2];
    const auto p95_idx = static_cast<size_t>(std::ceil(0.95 * sorted.size())) - 1;
    const auto p95 = sorted[std::min(p95_idx, sorted.size() - 1)];
    state.counters["query_p50_ms"] = p50;
    state.counters["query_p95_ms"] = p95;
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["backend"] = 1.0;
  state.counters["query_count"] = static_cast<double>(kSlabCount);
  state.counters["max_query_streamlines"] = static_cast<double>(max_query_streamlines);
  state.counters["slab_thickness_mm"] = kSlabThicknessMm;
  state.counters["positions_dtype"] = 16.0;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void ApplyTranslateArgs(benchmark::internal::Benchmark *bench) {
  const auto counts = streamlines_for_benchmarks();
  for (const auto count : counts) {
    bench->Args({static_cast<int64_t>(count)});
  }
}

static void ApplyQueryArgs(benchmark::internal::Benchmark *bench) {
  const auto counts = streamlines_for_benchmarks();
  for (const auto count : counts) {
    bench->Args({static_cast<int64_t>(count)});
  }
  bench->Iterations(1);
}

BENCHMARK(BM_Itk_TranslateWrite)
    ->Apply(ApplyTranslateArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Itk_TranslateWrite_Vnl)
    ->Apply(ApplyTranslateArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Raw_TranslateWrite)
    ->Apply(ApplyTranslateArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Itk_QueryAabb)
    ->Apply(ApplyQueryArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Raw_QueryAabb)
    ->Apply(ApplyQueryArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Parcellation benchmarks
// ---------------------------------------------------------------------------

// Run one full parcellation labeling pass (labeling + write + group append).
// state.range(0) = number of streamlines; state.range(1) = dilation radius.
static void BM_Parcellate(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  const unsigned int dilation = static_cast<unsigned int>(state.range(1));

  if (g_parcellations.empty()) {
    state.SkipWithMessage("skipped: no parcellation files specified (use --bench-data-dir)");
    return;
  }
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }

  const auto &dataset = get_itk_subset(streamlines);
  const std::string subset_input_path = get_subset_trx_artifact(streamlines);

  double max_rss_delta_kb = 0.0;
  double max_output_file_bytes = 0.0;
  double max_pre_group_file_bytes = 0.0;
  double max_group_overhead_bytes = 0.0;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    const auto start = std::chrono::steady_clock::now();

    const std::string out_path = make_temp_path("parcellate");

    auto labeler = itk::TrxParcellationLabeler::New();
    labeler->SetInput(dataset.data);
    // Copy-through mode preserves source payload encoding for every subset size.
    labeler->SetInputFileName(subset_input_path);
    labeler->SetOutputFileName(out_path);
    labeler->SetDilationRadius(dilation);
    for (const auto &p : g_parcellations) {
      itk::TrxParcellationLabeler::ParcellationSpec spec;
      spec.niftiPath = p.niftiPath;
      spec.labelFilePath = p.labelPath;
      spec.groupPrefix = p.prefix;
      labeler->AddParcellation(spec);
    }
    labeler->Update();

    const double pre_group_file_bytes =
        static_cast<double>(labeler->GetPreGroupFileBytes());
    const double final_file_bytes =
        static_cast<double>(labeler->GetFinalFileBytes());
    max_pre_group_file_bytes = std::max(max_pre_group_file_bytes, pre_group_file_bytes);
    max_output_file_bytes = std::max(max_output_file_bytes, final_file_bytes);
    max_group_overhead_bytes =
        std::max(max_group_overhead_bytes, std::max(0.0, final_file_bytes - pre_group_file_bytes));

    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());

    std::error_code ec;
    std::filesystem::remove_all(out_path, ec);
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["atlases"] = static_cast<double>(g_parcellations.size());
  state.counters["dilation_radius"] = static_cast<double>(dilation);
  state.counters["max_rss_kb"] = max_rss_delta_kb;
  state.counters["pre_group_file_bytes"] = max_pre_group_file_bytes;
  state.counters["group_overhead_bytes"] = max_group_overhead_bytes;
  state.counters["output_file_bytes"] = max_output_file_bytes;
}

// Run one full group TDI pass into a reference NIfTI grid.
// state.range(0) = number of streamlines.
static void BM_GroupTdi(benchmark::State &state) {
  const size_t streamlines = static_cast<size_t>(state.range(0));
  if (streamlines > g_reference_streamline_count) {
    state.SkipWithMessage("skipped: streamlines exceeds reference file count");
    return;
  }
  if (g_parcellations.empty()) {
    state.SkipWithMessage("skipped: no reference parcellation NIfTI available (use --bench-data-dir)");
    return;
  }

  const std::string grouped_trx_path = get_subset_trx_artifact_with_group(streamlines);
  const std::string reference_nifti = g_parcellations.front().niftiPath;
  const size_t group_streamlines = build_bench_group_members(streamlines).size();

  double max_rss_delta_kb = 0.0;
  double max_nonzero_voxels = 0.0;
  for (auto _ : state) {
    const double rss_start = get_current_rss_kb();
    const auto start = std::chrono::steady_clock::now();

    auto mapper = itk::TrxGroupTdiMapper::New();
    mapper->SetInputFileName(grouped_trx_path);
    mapper->SetGroupName(kBenchTdiGroupName);
    mapper->SetReferenceImageFileName(reference_nifti);
    itk::TrxGroupTdiMapper::Options options;
    options.voxelStatistic = itk::TrxGroupTdiMapper::VoxelStatistic::Sum;
    mapper->SetOptions(options);
    mapper->Update();
    const auto *out = mapper->GetOutput();
    if (!out || !out->GetBufferPointer()) {
      state.SkipWithMessage("skipped: null TDI output");
      return;
    }

    const auto dims = out->GetLargestPossibleRegion().GetSize();
    const size_t nvox = static_cast<size_t>(dims[0]) * static_cast<size_t>(dims[1]) * static_cast<size_t>(dims[2]);
    size_t nonzero = 0;
    const auto *buffer = out->GetBufferPointer();
    for (size_t i = 0; i < nvox; ++i) {
      if (buffer[i] > 0.0f) {
        ++nonzero;
      }
    }
    max_nonzero_voxels = std::max(max_nonzero_voxels, static_cast<double>(nonzero));

    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    state.SetIterationTime(elapsed.count());
    max_rss_delta_kb = std::max(max_rss_delta_kb, get_current_rss_kb() - rss_start);
  }

  state.counters["streamlines"] = static_cast<double>(streamlines);
  state.counters["group_streamlines"] = static_cast<double>(group_streamlines);
  state.counters["nonzero_voxels"] = max_nonzero_voxels;
  state.counters["max_rss_kb"] = max_rss_delta_kb;
}

static void ApplyParcellateArgs(benchmark::internal::Benchmark *bench) {
  const auto counts = streamlines_for_benchmarks();
  // dilation radius 0 (no dilation) only — add more rows for additional radii.
  for (const auto count : counts) {
    bench->Args({static_cast<int64_t>(count), 0});
  }
  bench->Iterations(1);
}

static void ApplyGroupTdiArgs(benchmark::internal::Benchmark *bench) {
  const auto counts = streamlines_for_benchmarks();
  for (const auto count : counts) {
    bench->Args({static_cast<int64_t>(count)});
  }
  bench->Iterations(1);
}

BENCHMARK(BM_Parcellate)
    ->Apply(ApplyParcellateArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_GroupTdi)
    ->Apply(ApplyGroupTdiArgs)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

} // namespace

int main(int argc, char **argv) {
  bool show_help = false;
  std::string reference_trx;
  std::string bench_data_dir;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help-custom") {
      show_help = true;
    } else if (arg == "--reference-trx" && i + 1 < argc) {
      reference_trx = argv[i + 1];
      ++i;
    } else if (arg == "--bench-data-dir" && i + 1 < argc) {
      bench_data_dir = argv[i + 1];
      ++i;
    }
  }

  if (show_help) {
    std::cout << "\nCustom benchmark options:\n"
              << "  --reference-trx PATH   Path to reference TRX file (REQUIRED)\n"
              << "  --bench-data-dir DIR   Directory containing parcellation NIfTI and label files\n"
              << "                         (enables BM_Parcellate; expects the standard bench data\n"
              << "                         filenames: native_space_seg-Glasser_dseg.nii.gz, etc.)\n"
              << "  --help-custom          Show this help message\n"
              << "\nFor standard benchmark options, use --help\n"
              << std::endl;
    return 0;
  }

  if (reference_trx.empty()) {
    std::cerr << "Error: --reference-trx flag is required\n"
              << "Usage: " << argv[0] << " --reference-trx <path_to_trx_file> [benchmark_options]\n"
              << "Use --help-custom for more information\n" << std::endl;
    return 1;
  }

  // Resolve parcellation files from bench-data-dir if provided.
  if (!bench_data_dir.empty()) {
    const std::string sep =
        (bench_data_dir.back() == '/' || bench_data_dir.back() == '\\') ? "" : "/";

    const std::string glasser_nifti  = bench_data_dir + sep + "native_space_seg-Glasser_dseg.nii.gz";
    const std::string glasser_labels = bench_data_dir + sep + "native_space_seg-Glasser_dseg.txt";
    const std::string s4_nifti       = bench_data_dir + sep + "native_space_seg-4S456Parcels_dseg.nii.gz";
    const std::string s4_labels      = bench_data_dir + sep + "native_space_seg-4S456Parcels_dseg.txt";

    std::error_code ec;
    if (std::filesystem::exists(glasser_nifti, ec) &&
        std::filesystem::exists(glasser_labels, ec)) {
      g_parcellations.push_back({glasser_nifti, glasser_labels, "Glasser"});
      std::cerr << "[trx-itk-bench] Glasser parcellation found.\n";
    } else {
      std::cerr << "[trx-itk-bench] Warning: Glasser parcellation files not found in "
                << bench_data_dir << "\n";
    }
    if (std::filesystem::exists(s4_nifti, ec) &&
        std::filesystem::exists(s4_labels, ec)) {
      g_parcellations.push_back({s4_nifti, s4_labels, "4S456"});
      std::cerr << "[trx-itk-bench] 4S456 parcellation found.\n";
    } else {
      std::cerr << "[trx-itk-bench] Warning: 4S456 parcellation files not found in "
                << bench_data_dir << "\n";
    }
    if (g_parcellations.empty()) {
      std::cerr << "[trx-itk-bench] No parcellation files found; BM_Parcellate will be skipped.\n";
    }
  }

  std::error_code ec;
  if (!std::filesystem::exists(reference_trx, ec)) {
    std::cerr << "Error: Reference TRX file not found: " << reference_trx << std::endl;
    return 1;
  }

  g_reference_trx_path = reference_trx;
  std::cerr << "[trx-itk-bench] Using reference TRX: " << g_reference_trx_path << std::endl;

  {
    g_raw_reference = trx::load<half>(g_reference_trx_path);
    g_reference_streamline_count = g_raw_reference->num_streamlines();
    g_reference_has_dpv = !g_raw_reference->data_per_vertex.empty();
    g_raw_reference->get_or_build_streamline_aabbs();
  }

  {
    auto reader = itk::TrxFileReader::New();
    reader->SetFileName(g_reference_trx_path);
    reader->Update();
    g_itk_reference = reader->GetOutput();
    g_itk_reference->DisconnectPipeline();
  }

  if (parse_env_bool("TRX_BENCH_LOG", false)) {
    std::cerr << "[trx-itk-bench] Reference: " << g_reference_streamline_count
              << " streamlines, dpv=" << (g_reference_has_dpv ? "yes" : "no") << std::endl;
  }

  std::vector<char *> filtered_argv;
  filtered_argv.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help-custom") {
      continue;
    }
    if (arg == "--reference-trx") {
      ++i;
      continue;
    }
    if (arg == "--bench-data-dir") {
      ++i;
      continue;
    }
    filtered_argv.push_back(argv[i]);
  }
  int filtered_argc = static_cast<int>(filtered_argv.size());

  ::benchmark::Initialize(&filtered_argc, filtered_argv.data());
  if (::benchmark::ReportUnrecognizedArguments(filtered_argc, filtered_argv.data())) {
    return 1;
  }
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}
