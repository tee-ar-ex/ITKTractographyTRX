#!/usr/bin/env bash
set -euo pipefail

reference=""
reference_user_set=false
out_dir="bench"
build_dir="build-release"
bench_data_dir=""
bench_data_user_set=false
query_cap="${TRX_BENCH_MAX_QUERY_STREAMLINES:-500}"
extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --reference)
      reference="$2"
      reference_user_set=true
      shift 2
      ;;
    --out-dir)
      out_dir="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --bench-data-dir)
      bench_data_dir="$2"
      bench_data_user_set=true
      shift 2
      ;;
    --query-cap)
      query_cap="$2"
      shift 2
      ;;
    --help|-h)
      echo "Usage: $0 [--reference /path/to/reference.trx] [--bench-data-dir DIR] [--query-cap N] [--out-dir DIR] [--build-dir DIR] [-- <benchmark args>]"
      echo "If --reference is not provided, uses TractographyTRX_BENCH_REFERENCE_TRX from ${build_dir}/CMakeCache.txt."
      echo "If --bench-data-dir is not provided, uses TractographyTRX_BENCH_DATA_DIR from ${build_dir}/CMakeCache.txt."
      exit 0
      ;;
    --)
      shift
      extra_args+=("$@")
      break
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

resolve_reference_trx() {
  local cache_file="${build_dir}/CMakeCache.txt"
  local fallback_path="${build_dir}/_trx_bench_data/10milHCP_dps-sift2.trx"
  local cmake_path=""

  if [[ -f "${cache_file}" ]]; then
    cmake_path="$(awk -F= '/^TractographyTRX_BENCH_REFERENCE_TRX:/{print $2; exit}' "${cache_file}")"
  fi

  if [[ -n "${cmake_path}" ]]; then
    echo "${cmake_path}"
  else
    echo "${fallback_path}"
  fi
}

resolve_bench_data_dir() {
  local cache_file="${build_dir}/CMakeCache.txt"
  local fallback_path="${build_dir}/_trx_bench_data"
  local cmake_path=""

  if [[ -f "${cache_file}" ]]; then
    cmake_path="$(awk -F= '/^TractographyTRX_BENCH_DATA_DIR:/{print $2; exit}' "${cache_file}")"
  fi

  if [[ -n "${cmake_path}" ]]; then
    echo "${cmake_path}"
  else
    echo "${fallback_path}"
  fi
}

resolve_bench_binary() {
  local candidate1="${build_dir}/bench/bench_trx_itk_realdata"
  local candidate2="${build_dir}/bin/bench_trx_itk_realdata"
  local cache_file="${build_dir}/CMakeCache.txt"
  local itk_dir=""
  local itk_prefix=""
  local candidate3=""
  local candidate4="../itk-build/bin/bench_trx_itk_realdata"

  if [[ -x "${candidate1}" ]]; then
    echo "${candidate1}"
    return
  fi
  if [[ -x "${candidate2}" ]]; then
    echo "${candidate2}"
    return
  fi
  if [[ -f "${cache_file}" ]]; then
    itk_dir="$(awk -F= '/^ITK_DIR:/{print $2; exit}' "${cache_file}")"
    if [[ -n "${itk_dir}" ]]; then
      itk_prefix="${itk_dir}"
      itk_prefix="${itk_prefix%/lib/cmake/*}"
      candidate3="${itk_prefix}/bin/bench_trx_itk_realdata"
      if [[ -x "${candidate3}" ]]; then
        echo "${candidate3}"
        return
      fi
    fi
  fi
  if [[ -x "${candidate4}" ]]; then
    echo "${candidate4}"
    return
  fi
  echo "${candidate1}"
}

if [[ "${reference_user_set}" != "true" ]]; then
  reference="$(resolve_reference_trx)"
fi
if [[ "${bench_data_user_set}" != "true" ]]; then
  bench_data_dir="$(resolve_bench_data_dir)"
fi

if [[ "${query_cap}" -le 0 ]]; then
  echo "Warning: --query-cap <= 0 is unsupported for publication runs; forcing query cap to 500." >&2
  query_cap=500
fi
export TRX_BENCH_MAX_QUERY_STREAMLINES="${query_cap}"

cmake --build "${build_dir}" --target bench_trx_itk_realdata

if [[ ! -f "${reference}" ]]; then
  echo "Error: Reference TRX file not found: ${reference}" >&2
  echo "Configure with TractographyTRX_DOWNLOAD_BENCH_DATA=ON, or use --reference to specify the path." >&2
  exit 1
fi

mkdir -p "${out_dir}"
bench_out="${out_dir}/bench_trx_itk_realdata.json"
translate_out="${out_dir}/bench_trx_itk_realdata_translate.json"
query_out="${out_dir}/bench_trx_itk_realdata_query.json"
parcellate_out="${out_dir}/bench_trx_itk_realdata_parcellate.json"
group_tdi_out="${out_dir}/bench_trx_itk_realdata_group_tdi.json"
case_tmp_dir="${out_dir}/.bench_case_runs"

bench_data_args=()
if [[ -d "${bench_data_dir}" ]]; then
  bench_data_args+=(--bench-data-dir "${bench_data_dir}")
else
  echo "Warning: benchmark data directory not found (${bench_data_dir}); parcellation and GroupTdi benchmarks may be skipped." >&2
fi

bench_bin="$(resolve_bench_binary)"
if [[ ! -x "${bench_bin}" ]]; then
  echo "Error: benchmark binary not found: ${bench_bin}" >&2
  exit 1
fi

cmd=("${bench_bin}"
  --reference-trx "${reference}")

if ((${#bench_data_args[@]})); then
  cmd+=("${bench_data_args[@]}")
fi
if ((${#extra_args[@]})); then
  cmd+=("${extra_args[@]}")
fi

rm -rf "${case_tmp_dir}"
mkdir -p "${case_tmp_dir}"
rm -f "${bench_out}" "${translate_out}" "${query_out}" "${parcellate_out}" "${group_tdi_out}"

list_output="$("${cmd[@]}" --benchmark_list_tests)"
case_names=()
while IFS= read -r line; do
  [[ -z "${line}" ]] && continue
  [[ "${line}" == \[* ]] && continue
  if [[ "${line}" == BM_* ]]; then
    case_names+=("${line}")
  fi
done <<< "${list_output}"

if ((${#case_names[@]} == 0)); then
  echo "Error: no benchmark cases discovered from --benchmark_list_tests." >&2
  exit 1
fi

translate_parts=()
query_parts=()
parcellate_parts=()
group_tdi_parts=()

run_case() {
  local case_name="$1"
  local output_path="$2"
  local -a case_cmd=("${cmd[@]}"
    --benchmark_filter="^${case_name}$"
    --benchmark_out="${output_path}"
    --benchmark_out_format=json)
  "${case_cmd[@]}"
}

total_cases=${#case_names[@]}
for idx in "${!case_names[@]}"; do
  case_name="${case_names[$idx]}"
  seq_num=$((idx + 1))
  part_file="${case_tmp_dir}/case_$(printf '%03d' "${seq_num}").json"
  echo "Running benchmark case ${seq_num}/${total_cases}: ${case_name}"
  run_case "${case_name}" "${part_file}"

  case "${case_name}" in
    BM_Itk_TranslateWrite*|BM_Raw_TranslateWrite*)
      translate_parts+=("${part_file}")
      ;;
    BM_Itk_QueryAabb*|BM_Raw_QueryAabb*)
      query_parts+=("${part_file}")
      ;;
    BM_Parcellate*)
      parcellate_parts+=("${part_file}")
      ;;
    BM_GroupTdi*)
      group_tdi_parts+=("${part_file}")
      ;;
    *)
      echo "Warning: unclassified benchmark case '${case_name}', including only in merged JSON." >&2
      ;;
  esac
done

merge_json() {
  local merged_out="$1"
  shift
  if (($# == 0)); then
    return
  fi
  python - <<'PY' "${merged_out}" "$@"
import json
import sys
from pathlib import Path

merged_out = Path(sys.argv[1])
parts = [Path(p) for p in sys.argv[2:]]

all_benchmarks = []
context = None
for p in parts:
    if not p.exists():
        continue
    with p.open() as f:
        payload = json.load(f)
    if context is None:
        context = payload.get("context", {})
    all_benchmarks.extend(payload.get("benchmarks", []))

result = {"context": context or {}, "benchmarks": all_benchmarks}
with merged_out.open("w") as f:
    json.dump(result, f, indent=2)
    f.write("\n")
PY
}

if ((${#translate_parts[@]})); then
  merge_json "${translate_out}" "${translate_parts[@]}"
fi
if ((${#query_parts[@]})); then
  merge_json "${query_out}" "${query_parts[@]}"
fi
if ((${#parcellate_parts[@]})); then
  merge_json "${parcellate_out}" "${parcellate_parts[@]}"
fi
if ((${#group_tdi_parts[@]})); then
  merge_json "${group_tdi_out}" "${group_tdi_parts[@]}"
fi

all_parts=()
if ((${#translate_parts[@]})); then all_parts+=("${translate_parts[@]}"); fi
if ((${#query_parts[@]})); then all_parts+=("${query_parts[@]}"); fi
if ((${#parcellate_parts[@]})); then all_parts+=("${parcellate_parts[@]}"); fi
if ((${#group_tdi_parts[@]})); then all_parts+=("${group_tdi_parts[@]}"); fi
merge_json "${bench_out}" "${all_parts[@]}"

echo "Wrote benchmark results to ${bench_out}"
