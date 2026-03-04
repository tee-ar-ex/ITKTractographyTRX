#!/usr/bin/env bash
set -euo pipefail

reference=""
reference_user_set=false
out_dir="bench"
build_dir="build-release"
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
    --help|-h)
      echo "Usage: $0 [--reference /path/to/reference.trx] [--out-dir DIR] [--build-dir DIR] [-- <benchmark args>]"
      echo "If --reference is not provided, uses TractographyTRX_BENCH_REFERENCE_TRX from ${build_dir}/CMakeCache.txt."
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

if [[ "${reference_user_set}" != "true" ]]; then
  reference="$(resolve_reference_trx)"
fi

cmake --build "${build_dir}" --target bench_trx_itk_realdata

if [[ ! -f "${reference}" ]]; then
  echo "Error: Reference TRX file not found: ${reference}" >&2
  echo "Configure with TractographyTRX_DOWNLOAD_BENCH_DATA=ON, or use --reference to specify the path." >&2
  exit 1
fi

mkdir -p "${out_dir}"
bench_out="${out_dir}/bench_trx_itk_realdata.json"

"${build_dir}/bench/bench_trx_itk_realdata" \
  --reference-trx "${reference}" \
  --benchmark_out="${bench_out}" \
  --benchmark_out_format=json \
  "${extra_args[@]}"

echo "Wrote benchmark results to ${bench_out}"
