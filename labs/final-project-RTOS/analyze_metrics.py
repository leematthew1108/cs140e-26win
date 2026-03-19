#!/usr/bin/env python3
import argparse
import os
import re
from dataclasses import dataclass

import matplotlib.pyplot as plt
import seaborn as sns


@dataclass
class TaskStats:
    jitter_min: int
    jitter_max: int
    jitter_sum: int
    jitter_sumsq: int | None
    jitter_samples: int
    jitter_hist: list[int] | None = None
    lock_lat_min: int | None = None
    lock_lat_max: int | None = None
    lock_lat_sum: int | None = None
    lock_lat_sumsq: int | None = None
    lock_lat_samples: int | None = None
    lock_hist: list[int] | None = None

    def jitter_avg(self) -> float:
        return self.jitter_sum / max(1, self.jitter_samples)

    def jitter_stddev(self) -> float | None:
        if self.jitter_sumsq is None or self.jitter_samples <= 0:
            return None
        n = self.jitter_samples
        mean = self.jitter_sum / n
        ex2 = self.jitter_sumsq / n
        var = max(0.0, ex2 - mean * mean)
        return var ** 0.5

    def lock_lat_avg(self) -> float | None:
        if self.lock_lat_sum is None or self.lock_lat_samples is None:
            return None
        return self.lock_lat_sum / max(1, self.lock_lat_samples)

    def lock_lat_stddev(self) -> float | None:
        if (
            self.lock_lat_sumsq is None
            or self.lock_lat_sum is None
            or self.lock_lat_samples is None
            or self.lock_lat_samples <= 0
        ):
            return None
        n = self.lock_lat_samples
        mean = self.lock_lat_sum / n
        ex2 = self.lock_lat_sumsq / n
        var = max(0.0, ex2 - mean * mean)
        return var ** 0.5


def parse_one_log(text: str) -> dict:
    # Identify which block we're in.
    label = None
    if "RTOS stats" in text:
        label = "RTOS"
    if "BASELINE stats" in text:
        # If both exist, treat them separately by scanning line-by-line below.
        label = None

    timer_re = re.compile(r"^timer:\s+tick_cnt=(\d+)\s+base_us=(\d+)\s*$")
    h_re = re.compile(
        r"^H jitter:\s+min=(\d+)\s+max=(\d+)\s+sum=(\d+)\s+"
        r"(?:sumsq_hi=(\d+)\s+sumsq_lo=(\d+)\s+)?samples=(\d+)\s+"
        r"lock_lat:\s+min=(\d+)\s+max=(\d+)\s+sum=(\d+)\s+"
        r"(?:sumsq_hi=(\d+)\s+sumsq_lo=(\d+)\s+)?samples=(\d+)\s*$"
    )
    m_re = re.compile(
        r"^M jitter:\s+min=(\d+)\s+max=(\d+)\s+sum=(\d+)\s+"
        r"(?:sumsq_hi=(\d+)\s+sumsq_lo=(\d+)\s+)?samples=(\d+)\s*$"
    )
    l_re = re.compile(
        r"^L jitter:\s+min=(\d+)\s+max=(\d+)\s+sum=(\d+)\s+"
        r"(?:sumsq_hi=(\d+)\s+sumsq_lo=(\d+)\s+)?samples=(\d+)\s+"
        r"lock_lat:\s+min=(\d+)\s+max=(\d+)\s+sum=(\d+)\s+"
        r"(?:sumsq_hi=(\d+)\s+sumsq_lo=(\d+)\s+)?samples=(\d+)\s*$"
    )

    hist_counts_re = re.compile(r"^(H|M|L)\s+(jitter|lock)_hist_counts:\s+(.*)$")

    out: dict[str, dict] = {}

    current_label = None
    cur_timer = None
    cur_tasks: dict[str, TaskStats] = {}

    def flush_current():
        nonlocal cur_timer, cur_tasks, current_label
        if current_label and cur_timer and cur_tasks:
            out[current_label] = {"timer": cur_timer, "tasks": cur_tasks}
        cur_timer = None
        cur_tasks = {}
        current_label = None

    for line in text.splitlines():
        line = line.strip()
        if line.startswith("RTOS stats"):
            flush_current()
            current_label = "RTOS"
            continue
        if line.startswith("BASELINE stats"):
            flush_current()
            current_label = "BASELINE"
            continue

        if not current_label:
            continue

        m = timer_re.match(line)
        if m:
            tick_cnt = int(m.group(1))
            base_us = int(m.group(2))
            cur_timer = {"tick_cnt": tick_cnt, "base_us": base_us}
            continue

        m = h_re.match(line)
        if m:
            j_hi = m.group(4)
            j_lo = m.group(5)
            j_sumsq = None if j_hi is None else ((int(j_hi) << 32) | int(j_lo))

            l_hi = m.group(10)
            l_lo = m.group(11)
            l_sumsq = None if l_hi is None else ((int(l_hi) << 32) | int(l_lo))

            cur_tasks["H"] = TaskStats(
                jitter_min=int(m.group(1)),
                jitter_max=int(m.group(2)),
                jitter_sum=int(m.group(3)),
                jitter_sumsq=j_sumsq,
                jitter_samples=int(m.group(6)),
                lock_lat_min=int(m.group(7)),
                lock_lat_max=int(m.group(8)),
                lock_lat_sum=int(m.group(9)),
                lock_lat_sumsq=l_sumsq,
                lock_lat_samples=int(m.group(12)),
            )
            continue

        m = m_re.match(line)
        if m:
            j_hi = m.group(4)
            j_lo = m.group(5)
            j_sumsq = None if j_hi is None else ((int(j_hi) << 32) | int(j_lo))
            cur_tasks["M"] = TaskStats(
                jitter_min=int(m.group(1)),
                jitter_max=int(m.group(2)),
                jitter_sum=int(m.group(3)),
                jitter_sumsq=j_sumsq,
                jitter_samples=int(m.group(6)),
            )
            continue

        m = l_re.match(line)
        if m:
            j_hi = m.group(4)
            j_lo = m.group(5)
            j_sumsq = None if j_hi is None else ((int(j_hi) << 32) | int(j_lo))

            l_hi = m.group(10)
            l_lo = m.group(11)
            l_sumsq = None if l_hi is None else ((int(l_hi) << 32) | int(l_lo))

            cur_tasks["L"] = TaskStats(
                jitter_min=int(m.group(1)),
                jitter_max=int(m.group(2)),
                jitter_sum=int(m.group(3)),
                jitter_sumsq=j_sumsq,
                jitter_samples=int(m.group(6)),
                lock_lat_min=int(m.group(7)),
                lock_lat_max=int(m.group(8)),
                lock_lat_sum=int(m.group(9)),
                lock_lat_sumsq=l_sumsq,
                lock_lat_samples=int(m.group(12)),
            )
            continue

        m = hist_counts_re.match(line)
        if m:
            t = m.group(1)
            kind = m.group(2)
            counts = [int(x) for x in m.group(3).split()]
            if t in cur_tasks:
                if kind == "jitter":
                    cur_tasks[t].jitter_hist = counts
                else:
                    cur_tasks[t].lock_hist = counts
            continue

    flush_current()
    return out


def load_file(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def print_tables(parsed: dict[str, dict]) -> None:
    tasks = ["H", "M", "L"]
    for label in ["RTOS", "BASELINE"]:
        if label not in parsed:
            continue
        print(f"\n=== {label} ===")
        timer = parsed[label]["timer"]
        print(f"timer: tick_cnt={timer['tick_cnt']} base_us={timer['base_us']}")

        for t in tasks:
            ts: TaskStats = parsed[label]["tasks"][t]
            j_sd = ts.jitter_stddev()
            print(
                f"{t} jitter: "
                f"min={ts.jitter_min} max={ts.jitter_max} "
                f"sum={ts.jitter_sum} samples={ts.jitter_samples} "
                f"avg={ts.jitter_avg():.2f}us"
                + ("" if j_sd is None else f" stddev={j_sd:.2f}us")
            )
            lat_avg = ts.lock_lat_avg()
            lat_sd = ts.lock_lat_stddev()
            if lat_avg is not None:
                print(
                    f"{t} lock_lat: "
                    f"min={ts.lock_lat_min} max={ts.lock_lat_max} "
                    f"sum={ts.lock_lat_sum} samples={ts.lock_lat_samples} "
                    f"avg={lat_avg:.2f}us"
                    + ("" if lat_sd is None else f" stddev={lat_sd:.2f}us")
                )


def plot_comparison(parsed: dict[str, dict], outdir: str) -> None:
    os.makedirs(outdir, exist_ok=True)

    labels = [k for k in ["RTOS", "BASELINE"] if k in parsed]
    if len(labels) < 2:
        return

    sns.set_theme(style="whitegrid", context="talk")

    # These must match the kernel histogram configuration.
    JIT_BIN_US = 50
    JIT_MAX_US = 3000
    LOCK_BIN_US = 20
    LOCK_MAX_US = 1000

    def expand_hist(counts: list[int], *, bin_us: int, max_us: int, has_neg: bool) -> list[float]:
        out: list[float] = []
        if has_neg:
            # bucket 0 is "negative"; use a representative negative midpoint
            out.extend([-0.5 * bin_us] * counts[0])
            idx0 = 1
        else:
            idx0 = 0

        # regular buckets
        n_regular = (max_us // bin_us)
        for b in range(n_regular):
            idx = idx0 + b
            if idx >= len(counts) - 1:
                break
            mid = (b + 0.5) * bin_us
            out.extend([mid] * counts[idx])

        # overflow bucket (last)
        overflow = counts[-1]
        if overflow:
            out.extend([max_us + 0.5 * bin_us] * overflow)
        return out

    def overlay_hist_with_boxplot(
        *,
        xs_rtos: list[float],
        xs_base: list[float],
        bins: list[float],
        title: str,
        xlabel: str,
        outpath: str,
    ) -> None:
        fig, (ax_h, ax_b) = plt.subplots(
            nrows=2,
            figsize=(11, 6.5),
            gridspec_kw={"height_ratios": [3, 1]},
            sharex=True,
        )

        sns.histplot(xs_rtos, bins=bins, stat="count", element="step", fill=True, alpha=0.35, label="RTOS", ax=ax_h)
        sns.histplot(xs_base, bins=bins, stat="count", element="step", fill=True, alpha=0.35, label="BASELINE", ax=ax_h)
        ax_h.set_title(title)
        ax_h.set_ylabel("Count")
        ax_h.legend()

        # Stacked boxplots for distribution comparison.
        sns.boxplot(
            data=[xs_rtos, xs_base],
            orient="h",
            ax=ax_b,
            palette=["#4C72B0", "#DD8452"],
            showfliers=False,
        )
        ax_b.set_yticks([0, 1])
        ax_b.set_yticklabels(["RTOS", "BASELINE"])
        ax_b.set_xlabel(xlabel)
        ax_b.set_ylabel("")

        fig.tight_layout()
        fig.savefig(outpath)
        plt.close(fig)

    # Generate jitter overlays for H/M/L. (Lock latency stays numerical in README.)
    rH = parsed["RTOS"]["tasks"]["H"]
    bH = parsed["BASELINE"]["tasks"]["H"]
    rM = parsed["RTOS"]["tasks"]["M"]
    bM = parsed["BASELINE"]["tasks"]["M"]
    rL = parsed["RTOS"]["tasks"]["L"]
    bL = parsed["BASELINE"]["tasks"]["L"]

    if rH.jitter_hist and bH.jitter_hist:
        rxs = expand_hist(rH.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bxs = expand_hist(bH.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bins = [-JIT_BIN_US] + [i * JIT_BIN_US for i in range(0, (JIT_MAX_US // JIT_BIN_US) + 2)]
        overlay_hist_with_boxplot(
            xs_rtos=rxs,
            xs_base=bxs,
            bins=bins,
            title="H release/start jitter distribution (overlay)",
            xlabel="Jitter (us)",
            outpath=os.path.join(outdir, "overlay_h_jitter.png"),
        )

    if rM.jitter_hist and bM.jitter_hist:
        rxs = expand_hist(rM.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bxs = expand_hist(bM.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bins = [-JIT_BIN_US] + [i * JIT_BIN_US for i in range(0, (JIT_MAX_US // JIT_BIN_US) + 2)]
        overlay_hist_with_boxplot(
            xs_rtos=rxs,
            xs_base=bxs,
            bins=bins,
            title="M release/start jitter distribution (overlay)",
            xlabel="Jitter (us)",
            outpath=os.path.join(outdir, "overlay_m_jitter.png"),
        )

    if rL.jitter_hist and bL.jitter_hist:
        rxs = expand_hist(rL.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bxs = expand_hist(bL.jitter_hist, bin_us=JIT_BIN_US, max_us=JIT_MAX_US, has_neg=True)
        bins = [-JIT_BIN_US] + [i * JIT_BIN_US for i in range(0, (JIT_MAX_US // JIT_BIN_US) + 2)]
        overlay_hist_with_boxplot(
            xs_rtos=rxs,
            xs_base=bxs,
            bins=bins,
            title="L release/start jitter distribution (overlay)",
            xlabel="Jitter (us)",
            outpath=os.path.join(outdir, "overlay_l_jitter.png"),
        )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--preemptive", help="Text file containing the RTOS stats block")
    ap.add_argument("--baseline", help="Text file containing the BASELINE stats block")
    ap.add_argument("--outdir", default="analysis_out", help="Output directory for plots")
    args = ap.parse_args()

    merged: dict[str, dict] = {}
    if args.preemptive:
        parsed = parse_one_log(load_file(args.preemptive))
        merged.update(parsed)
    if args.baseline:
        parsed = parse_one_log(load_file(args.baseline))
        merged.update(parsed)

    if not merged:
        raise SystemExit("No RTOS/BASELINE stats found in provided inputs.")

    print_tables(merged)
    plot_comparison(merged, args.outdir)
    print(f"\nWrote plots to: {args.outdir}/overlay_[h|m|l]_jitter.png (if histograms present)")


if __name__ == "__main__":
    main()

