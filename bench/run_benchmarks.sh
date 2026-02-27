#!/usr/bin/env bash
set -euo pipefail

reference=""
out_dir="bench"
build_dir="build-release"
extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --reference)
      reference="$2"
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
      echo "Usage: $0 --reference /path/to/reference.trx [--out-dir DIR] [--build-dir DIR] [-- <benchmark args>]"
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

if [[ -z "$reference" ]]; then
  echo "Error: --reference is required" >&2
  exit 1
fi

cmake --build "${build_dir}" --target bench_trx_itk_realdata

mkdir -p "${out_dir}"
bench_out="${out_dir}/bench_trx_itk_realdata.json"

"${build_dir}/bench/bench_trx_itk_realdata" \
  --reference-trx "${reference}" \
  --benchmark_out="${bench_out}" \
  --benchmark_out_format=json \
  "${extra_args[@]}"

echo "Wrote benchmark results to ${bench_out}"
