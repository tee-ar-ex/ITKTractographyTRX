#!/usr/bin/env Rscript
#
# plot_bench.R - Plot ITKTractographyTRX benchmark results with ggplot2
#
# Usage:
#   Rscript bench/plot_bench.R [--bench-dir DIR] [--out-dir DIR] [--help]
#
# This script reads Google Benchmark JSON output files and generates plots for:
#   - Translate + write runtime and max RSS (ITK, ITK-vnl, raw trx-cpp)
#   - AABB query total runtime and per-query p50/p95
#   - Parcellation runtime (if BM_Parcellate results are present)
#

suppressPackageStartupMessages({
  library(jsonlite)
  library(ggplot2)
  library(dplyr)
  library(tidyr)
  library(scales)
})

`%||%` <- function(x, y) if (is.null(x)) y else x

parse_args <- function() {
  args <- commandArgs(trailingOnly = TRUE)
  bench_dir <- "bench/results"
  out_dir <- "bench/results/plots"

  i <- 1
  while (i <= length(args)) {
    if (args[i] == "--bench-dir" && i + 1 <= length(args)) {
      bench_dir <- args[i + 1]
      i <- i + 2
    } else if (args[i] == "--out-dir" && i + 1 <= length(args)) {
      out_dir <- args[i + 1]
      i <- i + 2
    } else if (args[i] == "--help" || args[i] == "-h") {
      cat("Usage: Rscript bench/plot_bench.R [--bench-dir DIR] [--out-dir DIR]\n\n")
      cat("Options:\n")
      cat("  --bench-dir DIR   Directory containing benchmark JSON files (default: bench/results)\n")
      cat("  --out-dir DIR     Output directory for PNG plots (default: bench/results/plots)\n")
      cat("  --help, -h        Show this help message\n")
      quit(status = 0)
    } else {
      i <- i + 1
    }
  }

  list(bench_dir = bench_dir, out_dir = out_dir)
}

time_to_seconds <- function(bench) {
  value <- bench$real_time %||% NA_real_
  unit <- bench$time_unit %||% "ns"
  mult <- switch(unit,
                 "ns" = 1e-9,
                 "us" = 1e-6,
                 "ms" = 1e-3,
                 "s" = 1,
                 1e-9)
  value * mult
}

parse_base_name <- function(name) {
  sub("/.*", "", name)
}

streamline_label <- function(x) {
  label_number(scale = 1e-6, suffix = "M")(x)
}

backend_label <- function(x) {
  dplyr::case_when(
    is.na(x) ~ "unknown",
    abs(x - 0) < 1e-9 ~ "ITK",
    abs(x - 1) < 1e-9 ~ "raw trx-cpp",
    abs(x - 2) < 1e-9 ~ "ITK (vnl buffer)",
    TRUE ~ paste0("backend=", as.character(x))
  )
}

load_benchmarks <- function(bench_dir) {
  json_files <- list.files(
    bench_dir,
    pattern = "\\.json$",
    full.names = TRUE
  )

  if (length(json_files) == 0) {
    stop("No JSON files found in ", bench_dir)
  }

  rows <- list()
  for (json_file in json_files) {
    data <- tryCatch(
      fromJSON(json_file, simplifyDataFrame = FALSE),
      error = function(e) {
        warning("Failed to parse ", json_file, ": ", e$message)
        NULL
      }
    )
    if (is.null(data)) {
      next
    }
    benches <- data$benchmarks
    if (is.null(benches) || length(benches) == 0) {
      next
    }

    for (bench in benches) {
      name <- bench$name %||% ""
      if (!grepl("^BM_", name)) {
        next
      }
      rows[[length(rows) + 1]] <- list(
        source_file = basename(json_file),
        name = name,
        base = parse_base_name(name),
        real_time_s = time_to_seconds(bench),
        streamlines = as.numeric(bench$streamlines %||% NA_real_),
        backend = as.numeric(bench$backend %||% NA_real_),
        max_rss_kb = as.numeric(bench$max_rss_kb %||% NA_real_),
        query_p50_ms = as.numeric(bench$query_p50_ms %||% NA_real_),
        query_p95_ms = as.numeric(bench$query_p95_ms %||% NA_real_),
        query_count = as.numeric(bench$query_count %||% NA_real_),
        slab_thickness_mm = as.numeric(bench$slab_thickness_mm %||% NA_real_),
        max_query_streamlines = as.numeric(bench$max_query_streamlines %||% NA_real_),
        dilation_radius = as.numeric(bench$dilation_radius %||% NA_real_),
        atlases = as.numeric(bench$atlases %||% NA_real_),
        pre_group_file_bytes = as.numeric(
          bench$pre_group_file_bytes %||% NA_real_
        ),
        group_overhead_bytes = as.numeric(
          bench$group_overhead_bytes %||% NA_real_
        ),
        output_file_bytes = as.numeric(
          bench$output_file_bytes %||% bench$output_bytes %||% NA_real_
        )
      )
    }
  }

  if (length(rows) == 0) {
    stop("No BM_* benchmark rows found in JSON files under ", bench_dir)
  }

  df <- bind_rows(rows) %>%
    mutate(
      backend_label = backend_label(backend),
      streamlines_f = factor(
        streamlines,
        levels = sort(unique(streamlines)),
        labels = streamline_label(sort(unique(streamlines)))
      ),
      max_rss_gb = max_rss_kb / (1024 * 1024),
      pre_group_file_mb = pre_group_file_bytes / (1024 * 1024),
      group_overhead_mb = group_overhead_bytes / (1024 * 1024),
      output_file_mb = output_file_bytes / (1024 * 1024)
    )

  cat("Loaded", nrow(df), "benchmark rows from", length(unique(df$source_file)), "file(s)\n")
  cat("Benchmark families:\n")
  for (b in sort(unique(df$base))) {
    cat("  -", b, "(", sum(df$base == b), "rows)\n")
  }

  df
}

plot_translate_time <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_TranslateWrite", "BM_Itk_TranslateWrite_Vnl", "BM_Raw_TranslateWrite")) %>%
    filter(!is.na(streamlines), !is.na(real_time_s))

  if (nrow(sub_df) == 0) {
    cat("No translate/write rows found, skipping translate runtime plot\n")
    return(invisible(NULL))
  }

  p <- ggplot(sub_df, aes(x = streamlines_f, y = real_time_s, color = backend_label, group = backend_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "Translate + write runtime",
      x = "Streamlines",
      y = "Runtime (s, log scale)",
      color = "Backend"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1)
    )

  out_path <- file.path(out_dir, "translate_write_runtime.png")
  ggsave(out_path, p, width = 10, height = 5, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_translate_rss <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_TranslateWrite", "BM_Itk_TranslateWrite_Vnl", "BM_Raw_TranslateWrite")) %>%
    filter(!is.na(streamlines), !is.na(max_rss_gb))

  if (nrow(sub_df) == 0) {
    cat("No translate/write RSS rows found, skipping translate RSS plot\n")
    return(invisible(NULL))
  }

  p <- ggplot(sub_df, aes(x = streamlines_f, y = max_rss_gb, fill = backend_label)) +
    geom_col(position = "dodge") +
    labs(
      title = "Translate + write memory usage",
      x = "Streamlines",
      y = "Max RSS delta (GB)",
      fill = "Backend"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1)
    )

  out_path <- file.path(out_dir, "translate_write_rss.png")
  ggsave(out_path, p, width = 10, height = 5, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_query_total <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) %>%
    filter(!is.na(streamlines), !is.na(real_time_s))

  if (nrow(sub_df) == 0) {
    cat("No query rows found, skipping query total runtime plot\n")
    return(invisible(NULL))
  }

  p <- ggplot(sub_df, aes(x = streamlines_f, y = real_time_s, color = backend_label, group = backend_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "AABB query workload runtime (20 slabs)",
      x = "Streamlines",
      y = "Total runtime (s, log scale)",
      color = "Backend"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1)
    )

  out_path <- file.path(out_dir, "query_total_runtime.png")
  ggsave(out_path, p, width = 10, height = 5, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_query_percentiles <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) %>%
    filter(!is.na(streamlines), !is.na(query_p50_ms), !is.na(query_p95_ms)) %>%
    select(streamlines_f, backend_label, query_p50_ms, query_p95_ms) %>%
    pivot_longer(
      cols = c(query_p50_ms, query_p95_ms),
      names_to = "metric",
      values_to = "value_ms"
    ) %>%
    mutate(metric = recode(metric,
                           query_p50_ms = "p50",
                           query_p95_ms = "p95"))

  if (nrow(sub_df) == 0) {
    cat("No query percentile rows found, skipping p50/p95 plot\n")
    return(invisible(NULL))
  }

  p <- ggplot(sub_df, aes(x = streamlines_f, y = value_ms, color = backend_label, group = backend_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~metric, ncol = 1, scales = "free_y") +
    labs(
      title = "AABB per-slab query percentiles",
      x = "Streamlines",
      y = "Per-slab latency (ms)",
      color = "Backend"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_path <- file.path(out_dir, "query_p50_p95.png")
  ggsave(out_path, p, width = 10, height = 7, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_parcellate <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base == "BM_Parcellate") %>%
    filter(!is.na(streamlines), !is.na(real_time_s))

  if (nrow(sub_df) == 0) {
    cat("No BM_Parcellate rows found, skipping parcellation plot\n")
    return(invisible(NULL))
  }

  sub_df <- sub_df %>%
    mutate(
      dilation_label = paste0("dilation=", as.integer(dilation_radius %||% 0)),
      atlas_label = paste0("atlases=", as.integer(atlases %||% 0))
    )

  p <- ggplot(sub_df, aes(x = streamlines_f, y = real_time_s, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "Parcellation runtime",
      x = "Streamlines",
      y = "Runtime (s, log scale)",
      color = "Dilation radius"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_path <- file.path(out_dir, "parcellation_runtime.png")
  ggsave(out_path, p, width = 10, height = 6, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_parcellate_rss <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base == "BM_Parcellate") %>%
    filter(!is.na(streamlines), !is.na(max_rss_gb))

  if (nrow(sub_df) == 0) {
    cat("No BM_Parcellate RSS rows found, skipping parcellation RSS plot\n")
    return(invisible(NULL))
  }

  sub_df <- sub_df %>%
    mutate(
      dilation_label = paste0("dilation=", as.integer(dilation_radius %||% 0)),
      atlas_label = paste0("atlases=", as.integer(atlases %||% 0))
    )

  p <- ggplot(sub_df, aes(x = streamlines_f, y = max_rss_gb, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    labs(
      title = "Parcellation memory usage",
      x = "Streamlines",
      y = "Max RSS delta (GB)",
      color = "Dilation radius"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_path <- file.path(out_dir, "parcellation_rss.png")
  ggsave(out_path, p, width = 10, height = 6, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_parcellate_output_size <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base == "BM_Parcellate") %>%
    filter(!is.na(streamlines), !is.na(output_file_mb))

  if (nrow(sub_df) == 0) {
    cat("No BM_Parcellate output size rows found, skipping parcellation size plot\n")
    return(invisible(NULL))
  }

  sub_df <- sub_df %>%
    mutate(
      dilation_label = paste0("dilation=", as.integer(dilation_radius %||% 0)),
      atlas_label = paste0("atlases=", as.integer(atlases %||% 0))
    )

  p <- ggplot(sub_df, aes(x = streamlines_f, y = output_file_mb, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    labs(
      title = "Parcellation output size",
      x = "Streamlines",
      y = "Output TRX size (MiB)",
      color = "Dilation radius"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_path <- file.path(out_dir, "parcellation_output_size.png")
  ggsave(out_path, p, width = 10, height = 6, dpi = 160)
  cat("Saved:", out_path, "\n")
}

plot_parcellate_group_overhead <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base == "BM_Parcellate") %>%
    filter(!is.na(streamlines), !is.na(pre_group_file_mb), !is.na(output_file_mb))

  if (nrow(sub_df) == 0) {
    cat("No BM_Parcellate pre/post size rows found, skipping group-overhead plots\n")
    return(invisible(NULL))
  }

  sub_df <- sub_df %>%
    mutate(
      dilation_label = paste0("dilation=", as.integer(dilation_radius %||% 0)),
      atlas_label = paste0("atlases=", as.integer(atlases %||% 0)),
      delta_mb = pmax(0, output_file_mb - pre_group_file_mb)
    )

  long_df <- sub_df %>%
    select(streamlines_f, dilation_label, atlas_label, pre_group_file_mb, output_file_mb) %>%
    pivot_longer(
      cols = c(pre_group_file_mb, output_file_mb),
      names_to = "stage",
      values_to = "size_mb"
    ) %>%
    mutate(
      stage = recode(stage,
                     pre_group_file_mb = "before groups",
                     output_file_mb = "after groups")
    )

  p_stage <- ggplot(long_df, aes(x = streamlines_f, y = size_mb, color = stage, group = stage)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_grid(atlas_label ~ dilation_label) +
    labs(
      title = "Parcellation output size before vs after group append",
      x = "Streamlines",
      y = "TRX size (MiB)",
      color = "Stage"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_stage <- file.path(out_dir, "parcellation_size_before_after_groups.png")
  ggsave(out_stage, p_stage, width = 10, height = 6, dpi = 160)
  cat("Saved:", out_stage, "\n")

  p_delta <- ggplot(sub_df, aes(x = streamlines_f, y = delta_mb, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    labs(
      title = "Parcellation group-only size overhead",
      x = "Streamlines",
      y = "Added size from groups (MiB)",
      color = "Dilation radius"
    ) +
    theme_bw() +
    theme(
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey90")
    )

  out_delta <- file.path(out_dir, "parcellation_group_overhead.png")
  ggsave(out_delta, p_delta, width = 10, height = 6, dpi = 160)
  cat("Saved:", out_delta, "\n")
}

main <- function() {
  args <- parse_args()
  dir.create(args$out_dir, recursive = TRUE, showWarnings = FALSE)

  cat("\n=== ITKTractographyTRX Benchmark Plotting ===\n\n")
  cat("Benchmark directory:", args$bench_dir, "\n")
  cat("Output directory:", args$out_dir, "\n\n")

  df <- load_benchmarks(args$bench_dir)

  cat("\n--- Generating plots ---\n\n")
  plot_translate_time(df, args$out_dir)
  plot_translate_rss(df, args$out_dir)
  plot_query_total(df, args$out_dir)
  plot_query_percentiles(df, args$out_dir)
  plot_parcellate(df, args$out_dir)
  plot_parcellate_rss(df, args$out_dir)
  plot_parcellate_output_size(df, args$out_dir)
  plot_parcellate_group_overhead(df, args$out_dir)

  cat("\nDone. Plots saved to:", args$out_dir, "\n")
}

if (!interactive()) {
  main()
}
