#!/usr/bin/env Rscript
#
# plot_bench.R - publication-grade plots for ITKTractographyTRX benchmarks
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
  run_type <- "iteration"
  strict <- TRUE

  i <- 1
  while (i <= length(args)) {
    if (args[i] == "--bench-dir" && i + 1 <= length(args)) {
      bench_dir <- args[i + 1]
      i <- i + 2
    } else if (args[i] == "--out-dir" && i + 1 <= length(args)) {
      out_dir <- args[i + 1]
      i <- i + 2
    } else if (args[i] == "--run-type" && i + 1 <= length(args)) {
      run_type <- args[i + 1]
      i <- i + 2
    } else if (args[i] == "--strict" && i + 1 <= length(args)) {
      strict <- as.logical(args[i + 1])
      i <- i + 2
    } else if (args[i] == "--help" || args[i] == "-h") {
      cat("Usage: Rscript bench/plot_bench.R [--bench-dir DIR] [--out-dir DIR] [--run-type iteration|aggregate] [--strict TRUE|FALSE]\n\n")
      quit(status = 0)
    } else {
      i <- i + 1
    }
  }

  if (!run_type %in% c("iteration", "aggregate")) {
    stop("--run-type must be one of: iteration, aggregate")
  }

  list(bench_dir = bench_dir, out_dir = out_dir, run_type = run_type, strict = strict)
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

backend_label <- function(x) {
  dplyr::case_when(
    is.na(x) ~ "unknown",
    abs(x - 0) < 1e-9 ~ "ITK",
    abs(x - 1) < 1e-9 ~ "raw trx-cpp",
    abs(x - 2) < 1e-9 ~ "ITK (vnl buffer)",
    TRUE ~ paste0("backend=", as.character(x))
  )
}

pretty_streamline_labels <- function(levels_numeric) {
  scales::label_number(scale = 1e-6, suffix = "M")(levels_numeric)
}

pub_theme <- function() {
  theme_bw(base_size = 11) +
    theme(
      panel.grid.minor = element_blank(),
      panel.grid.major.x = element_blank(),
      legend.position = "bottom",
      axis.text.x = element_text(angle = 45, hjust = 1),
      strip.background = element_rect(fill = "grey95")
    )
}

load_benchmarks <- function(bench_dir, run_type_filter) {
  json_files <- list.files(
    bench_dir,
    pattern = "\\.json$",
    full.names = TRUE,
    recursive = TRUE
  )
  if (length(json_files) == 0) {
    stop("No JSON files found under ", bench_dir)
  }

  rows <- list()
  for (json_file in json_files) {
    payload <- tryCatch(fromJSON(json_file, simplifyDataFrame = FALSE),
      error = function(e) {
        warning("Failed to parse ", json_file, ": ", e$message)
        NULL
      }
    )
    if (is.null(payload) || is.null(payload$benchmarks)) {
      next
    }
    for (bench in payload$benchmarks) {
      name <- bench$name %||% ""
      if (!grepl("^BM_", name)) {
        next
      }
      rows[[length(rows) + 1]] <- list(
        source_file = basename(json_file),
        source_path = json_file,
        source_mtime = as.numeric(file.info(json_file)$mtime),
        name = name,
        base = parse_base_name(name),
        run_type = bench$run_type %||% NA_character_,
        aggregate_name = bench$aggregate_name %||% NA_character_,
        real_time_s = time_to_seconds(bench),
        streamlines = as.numeric(bench$streamlines %||% NA_real_),
        backend = as.numeric(bench$backend %||% NA_real_),
        max_rss_kb = as.numeric(bench$max_rss_kb %||% NA_real_),
        rss_baseline_kb = as.numeric(bench$rss_baseline_kb %||% NA_real_),
        rss_peak_kb = as.numeric(bench$rss_peak_kb %||% NA_real_),
        max_phys_delta_kb = as.numeric(bench$max_phys_delta_kb %||% NA_real_),
        phys_baseline_kb = as.numeric(bench$phys_baseline_kb %||% NA_real_),
        phys_peak_kb = as.numeric(bench$phys_peak_kb %||% NA_real_),
        query_count = as.numeric(bench$query_count %||% NA_real_),
        slab_thickness_mm = as.numeric(bench$slab_thickness_mm %||% NA_real_),
        max_query_streamlines = as.numeric(bench$max_query_streamlines %||% NA_real_),
        dilation_radius = as.numeric(bench$dilation_radius %||% NA_real_),
        atlases = as.numeric(bench$atlases %||% NA_real_),
        group_streamlines = as.numeric(bench$group_streamlines %||% NA_real_),
        nonzero_voxels = as.numeric(bench$nonzero_voxels %||% NA_real_),
        query_slab_ms = list({
          vals <- numeric()
          for (i in 0:19) {
            k <- sprintf("query_slab_%02d_ms", i)
            v <- bench[[k]]
            if (!is.null(v)) vals <- c(vals, as.numeric(v))
          }
          vals
        }),
        query_slab_count = list({
          vals <- numeric()
          for (i in 0:19) {
            k <- sprintf("query_slab_%02d_count", i)
            v <- bench[[k]]
            if (!is.null(v)) vals <- c(vals, as.numeric(v))
          }
          vals
        }),
        pre_group_file_bytes = as.numeric(bench$pre_group_file_bytes %||% NA_real_),
        group_overhead_bytes = as.numeric(bench$group_overhead_bytes %||% NA_real_),
        output_file_bytes = as.numeric(bench$output_file_bytes %||% bench$output_bytes %||% NA_real_)
      )
    }
  }

  if (length(rows) == 0) {
    stop("No BM_* rows found in JSON files under ", bench_dir)
  }

  df <- bind_rows(rows) %>%
    filter(run_type == run_type_filter) %>%
    mutate(
      backend_label = backend_label(backend),
      max_rss_gb = max_rss_kb / (1024 * 1024),
      rss_baseline_gb = rss_baseline_kb / (1024 * 1024),
      rss_peak_gb = rss_peak_kb / (1024 * 1024),
      max_phys_delta_gb = max_phys_delta_kb / (1024 * 1024),
      phys_baseline_gb = phys_baseline_kb / (1024 * 1024),
      phys_peak_gb = phys_peak_kb / (1024 * 1024),
      pre_group_file_mb = pre_group_file_bytes / (1024 * 1024),
      group_overhead_mb = group_overhead_bytes / (1024 * 1024),
      output_file_mb = output_file_bytes / (1024 * 1024)
    )

  # If multiple JSON sources contain the same benchmark case, keep the newest row.
  # This avoids stale rows from historical outputs/backups contaminating plots.
  df <- df %>%
    arrange(desc(source_mtime)) %>%
    group_by(name) %>%
    slice_head(n = 1) %>%
    ungroup()

  # drop synthetic aggregate rows that encode variance-only counters as zeros
  if (run_type_filter == "aggregate") {
    df <- df %>% filter(!(is.na(streamlines) | streamlines <= 0))
  }

  valid_streamlines <- sort(unique(df$streamlines[!is.na(df$streamlines) & df$streamlines > 0]))
  if (length(valid_streamlines) == 0) {
    stop("No positive streamline counts found after filtering.")
  }
  df <- df %>%
    mutate(
      streamlines_f = factor(
        streamlines,
        levels = valid_streamlines,
        labels = pretty_streamline_labels(valid_streamlines)
      )
    )

  cat("Loaded", nrow(df), "rows (run_type=", run_type_filter, ") from", length(unique(df$source_path)), "JSON file(s)\n", sep = "")
  cat("Families:\n")
  for (b in sort(unique(df$base))) {
    cat("  -", b, "(", sum(df$base == b), "rows)\n")
  }
  df
}

check_query_cap <- function(df, strict = TRUE) {
  q <- df %>% filter(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) %>% filter(!is.na(max_query_streamlines))
  if (nrow(q) == 0) {
    return(df)
  }
  positive_caps <- sort(unique(q$max_query_streamlines[q$max_query_streamlines > 0]))

  if (length(positive_caps) == 0) {
    msg <- "No positive query cap found in query rows."
    if (strict) stop(msg)
    warning(msg)
    return(df)
  }

  if (length(positive_caps) > 1) {
    msg <- paste0(
      "Expected one positive query cap shared across backends; observed positive caps: ",
      paste(positive_caps, collapse = ", ")
    )
    if (strict) {
      stop(msg)
    }
    warning(msg)
  }
  cap_to_keep <- positive_caps[1]

  # Drop uncapped/mismatched query rows while keeping all non-query rows.
  filtered <- df %>%
    filter(!(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) |
             (!is.na(max_query_streamlines) & max_query_streamlines == cap_to_keep))
  filtered
}

plot_translate_runtime <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_TranslateWrite", "BM_Itk_TranslateWrite_Vnl", "BM_Raw_TranslateWrite")) %>%
    filter(!is.na(streamlines), !is.na(real_time_s))
  if (nrow(sub_df) == 0) return(invisible(NULL))

  p <- ggplot(sub_df, aes(streamlines_f, real_time_s, color = backend_label, group = backend_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "Translate and write runtime scaling",
      x = "Streamlines",
      y = "Runtime (s, log scale)",
      color = "Backend"
    ) +
    pub_theme()

  ggsave(file.path(out_dir, "translate_write_runtime.svg"), p, width = 9.5, height = 4.8)
}

plot_translate_rss <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_TranslateWrite", "BM_Itk_TranslateWrite_Vnl", "BM_Raw_TranslateWrite")) %>%
    filter(!is.na(streamlines))
  sub_df_mem <- sub_df %>%
    mutate(memory_peak_gb = dplyr::coalesce(phys_peak_gb, rss_peak_gb)) %>%
    filter(!is.na(memory_peak_gb))
  sub_df_rss <- sub_df %>% filter(!is.na(rss_peak_gb))
  if (nrow(sub_df_mem) == 0 && nrow(sub_df_rss) == 0) return(invisible(NULL))

  if (nrow(sub_df_mem) > 0) {
    p_mem <- ggplot(sub_df_mem, aes(streamlines_f, memory_peak_gb, fill = backend_label)) +
      geom_col(position = position_dodge(width = 0.8), width = 0.75) +
      labs(
        title = "Translate and write peak memory footprint",
        x = "Streamlines",
        y = "Peak memory footprint (GiB)",
        fill = "Backend"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "translate_write_memory_footprint.svg"), p_mem, width = 9.5, height = 4.8)
  }

  if (nrow(sub_df_rss) > 0) {
    p_rss <- ggplot(sub_df_rss, aes(streamlines_f, rss_peak_gb, fill = backend_label)) +
      geom_col(position = position_dodge(width = 0.8), width = 0.75) +
      labs(
        title = "Translate and write peak RSS",
        x = "Streamlines",
        y = "Peak RSS (GiB)",
        fill = "Backend"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "translate_write_rss.svg"), p_rss, width = 9.5, height = 4.8)
  }
}

plot_query_total_runtime <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) %>%
    filter(!is.na(streamlines), !is.na(real_time_s))
  if (nrow(sub_df) == 0) return(invisible(NULL))

  p <- ggplot(sub_df, aes(streamlines_f, real_time_s, color = backend_label, group = backend_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "AABB query workload runtime (20 slabs)",
      x = "Streamlines",
      y = "Total runtime (s, log scale)",
      color = "Backend"
    ) +
    pub_theme()

  ggsave(file.path(out_dir, "query_total_runtime.svg"), p, width = 9.5, height = 4.8)
}

plot_query_runtime_distribution <- function(df, out_dir) {
  sub_df <- df %>%
    filter(base %in% c("BM_Itk_QueryAabb", "BM_Raw_QueryAabb")) %>%
    filter(!is.na(streamlines)) %>%
    filter(lengths(query_slab_ms) > 0) %>%
    select(streamlines_f, backend_label, query_slab_ms) %>%
    unnest_longer(query_slab_ms, values_to = "value_ms")
  if (nrow(sub_df) == 0) return(invisible(NULL))

  p <- ggplot(sub_df, aes(streamlines_f, value_ms, fill = backend_label)) +
    geom_boxplot(
      position = position_dodge(width = 0.8),
      width = 0.65,
      outlier.alpha = 0.35,
      outlier.size = 1.0
    ) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "AABB per-slab latency distribution (20 slabs)",
      x = "Streamlines",
      y = "Latency (ms, log scale)",
      fill = "Backend"
    ) +
    pub_theme()

  ggsave(file.path(out_dir, "query_runtime_distribution.svg"), p, width = 9.5, height = 6.2)
}

parcellation_annotate <- function(df) {
  df %>%
    mutate(
      dilation_label = paste0("dilation=", as.integer(dilation_radius)),
      atlas_label = paste0("atlases=", as.integer(atlases))
    )
}

plot_parcellation_runtime <- function(df, out_dir) {
  sub_df <- df %>% filter(base == "BM_Parcellate") %>% filter(!is.na(streamlines), !is.na(real_time_s))
  if (nrow(sub_df) == 0) return(invisible(NULL))
  sub_df <- parcellation_annotate(sub_df)

  p <- ggplot(sub_df, aes(streamlines_f, real_time_s, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "Parcellation runtime scaling",
      x = "Streamlines",
      y = "Runtime (s, log scale)",
      color = "Dilation radius"
    ) +
    pub_theme()

  ggsave(file.path(out_dir, "parcellation_runtime.svg"), p, width = 9.5, height = 6.0)
}

plot_parcellation_rss <- function(df, out_dir) {
  sub_df <- df %>% filter(base == "BM_Parcellate") %>% filter(!is.na(streamlines))
  sub_df_mem <- sub_df %>%
    mutate(memory_peak_gb = dplyr::coalesce(phys_peak_gb, rss_peak_gb)) %>%
    filter(!is.na(memory_peak_gb))
  sub_df_rss <- sub_df %>% filter(!is.na(rss_peak_gb))
  if (nrow(sub_df_mem) == 0 && nrow(sub_df_rss) == 0) return(invisible(NULL))

  if (nrow(sub_df_mem) > 0) {
    sub_mem <- parcellation_annotate(sub_df_mem)
    p_mem <- ggplot(sub_mem, aes(streamlines_f, memory_peak_gb, color = dilation_label, group = dilation_label)) +
      geom_line(linewidth = 1) +
      geom_point(size = 2) +
      facet_wrap(~atlas_label, ncol = 1) +
      labs(
        title = "Parcellation peak memory footprint",
        x = "Streamlines",
        y = "Peak memory footprint (GiB)",
        color = "Dilation radius"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "parcellation_memory_footprint.svg"), p_mem, width = 9.5, height = 6.0)
  }

  if (nrow(sub_df_rss) > 0) {
    sub_rss <- parcellation_annotate(sub_df_rss)
    p_rss <- ggplot(sub_rss, aes(streamlines_f, rss_peak_gb, color = dilation_label, group = dilation_label)) +
      geom_line(linewidth = 1) +
      geom_point(size = 2) +
      facet_wrap(~atlas_label, ncol = 1) +
      labs(
        title = "Parcellation peak RSS",
        x = "Streamlines",
        y = "Peak RSS (GiB)",
        color = "Dilation radius"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "parcellation_rss.svg"), p_rss, width = 9.5, height = 6.0)
  }
}

plot_parcellation_size <- function(df, out_dir) {
  sub_df <- df %>% filter(base == "BM_Parcellate") %>%
    filter(!is.na(streamlines), !is.na(pre_group_file_mb), !is.na(output_file_mb))
  if (nrow(sub_df) == 0) return(invisible(NULL))
  sub_df <- parcellation_annotate(sub_df) %>%
    mutate(delta_mb = pmax(0, output_file_mb - pre_group_file_mb))

  long_df <- sub_df %>%
    select(streamlines_f, atlas_label, dilation_label, pre_group_file_mb, output_file_mb) %>%
    pivot_longer(c(pre_group_file_mb, output_file_mb), names_to = "stage", values_to = "size_mb") %>%
    mutate(stage = recode(stage, pre_group_file_mb = "before groups", output_file_mb = "after groups"))

  p_stage <- ggplot(long_df, aes(streamlines_f, size_mb, color = stage, group = stage)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_grid(atlas_label ~ dilation_label) +
    labs(
      title = "Parcellation output size before and after groups",
      x = "Streamlines",
      y = "TRX size (MiB)",
      color = "Stage"
    ) +
    pub_theme()
  ggsave(file.path(out_dir, "parcellation_size_before_after_groups.svg"), p_stage, width = 10.5, height = 6.3)

  p_delta <- ggplot(sub_df, aes(streamlines_f, delta_mb, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    labs(
      title = "Parcellation group storage overhead",
      x = "Streamlines",
      y = "Added size (MiB)",
      color = "Dilation radius"
    ) +
    pub_theme()
  ggsave(file.path(out_dir, "parcellation_group_overhead.svg"), p_delta, width = 9.5, height = 6.0)

  p_out <- ggplot(sub_df, aes(streamlines_f, output_file_mb, color = dilation_label, group = dilation_label)) +
    geom_line(linewidth = 1) +
    geom_point(size = 2) +
    facet_wrap(~atlas_label, ncol = 1) +
    labs(
      title = "Parcellation final output size",
      x = "Streamlines",
      y = "Output TRX size (MiB)",
      color = "Dilation radius"
    ) +
    pub_theme()
  ggsave(file.path(out_dir, "parcellation_output_size.svg"), p_out, width = 9.5, height = 6.0)
}

plot_group_tdi <- function(df, out_dir) {
  sub_df <- df %>% filter(base == "BM_GroupTdi") %>% filter(!is.na(streamlines), !is.na(real_time_s))
  if (nrow(sub_df) == 0) return(invisible(NULL))

  p_runtime <- ggplot(sub_df, aes(streamlines_f, real_time_s, group = 1)) +
    geom_line(linewidth = 1, color = "#1b6ca8") +
    geom_point(size = 2, color = "#1b6ca8") +
    scale_y_log10(labels = label_number()) +
    labs(
      title = "Group TDI runtime scaling",
      x = "Streamlines",
      y = "Runtime (s, log scale)"
    ) +
    pub_theme()
  ggsave(file.path(out_dir, "group_tdi_runtime.svg"), p_runtime, width = 9.5, height = 4.8)

  sub_mem <- sub_df %>%
    mutate(memory_peak_gb = dplyr::coalesce(phys_peak_gb, rss_peak_gb)) %>%
    filter(!is.na(memory_peak_gb))
  if (nrow(sub_mem) > 0) {
    p_mem <- ggplot(sub_mem, aes(streamlines_f, memory_peak_gb, group = 1)) +
      geom_line(linewidth = 1, color = "#5a3fc0") +
      geom_point(size = 2, color = "#5a3fc0") +
      labs(
        title = "Group TDI peak memory footprint",
        x = "Streamlines",
        y = "Peak memory footprint (GiB)"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "group_tdi_memory_footprint.svg"), p_mem, width = 9.5, height = 4.8)
  }

  sub_rss <- sub_df %>% filter(!is.na(rss_peak_gb))
  if (nrow(sub_rss) > 0) {
    p_rss <- ggplot(sub_rss, aes(streamlines_f, rss_peak_gb, group = 1)) +
      geom_line(linewidth = 1, color = "#7a1f5c") +
      geom_point(size = 2, color = "#7a1f5c") +
      labs(
        title = "Group TDI peak RSS",
        x = "Streamlines",
        y = "Peak RSS (GiB)"
      ) +
      pub_theme()
    ggsave(file.path(out_dir, "group_tdi_rss.svg"), p_rss, width = 9.5, height = 4.8)
  }
}

main <- function() {
  args <- parse_args()
  dir.create(args$out_dir, recursive = TRUE, showWarnings = FALSE)

  cat("\n=== ITKTractographyTRX Benchmark Plotting ===\n")
  cat("bench_dir:", args$bench_dir, "\n")
  cat("out_dir  :", args$out_dir, "\n")
  cat("run_type :", args$run_type, "\n")
  cat("strict   :", args$strict, "\n\n")

  df <- load_benchmarks(args$bench_dir, args$run_type)
  df <- check_query_cap(df, args$strict)

  plot_translate_runtime(df, args$out_dir)
  plot_translate_rss(df, args$out_dir)
  plot_query_total_runtime(df, args$out_dir)
  plot_query_runtime_distribution(df, args$out_dir)
  plot_parcellation_runtime(df, args$out_dir)
  plot_parcellation_rss(df, args$out_dir)
  plot_parcellation_size(df, args$out_dir)
  plot_group_tdi(df, args$out_dir)

  cat("Done. Plots saved to:", args$out_dir, "\n")
}

if (!interactive()) main()
