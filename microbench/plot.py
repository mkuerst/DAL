import os
import re
from io import StringIO
import glob
import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.lines as mlines            


CLIENT_COLS = [
"tid", "loop_in_cs", "lock_acquires", 
"lock_hold", "total_duration", 
"wait_acq", "wait_rel", 
"lwait_acq", "lwait_rel",
"gwait_acq", "gwait_rel",
"array_size", "client_id",
"run"
]
SERVER_COLS = [
    "tid", "wait_acq", "wait_rel","client_id","run"
]
MICROBENCHES = ["empty_cs", "lat", "mem_2nodes", "mem_1node"]

def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0

def to_pd(DATA, RES_DIRS, server=False):
    COLS = SERVER_COLS if server else CLIENT_COLS
    for mb,dirs in RES_DIRS.items():
        DATA[mb] = {}
        for dir in dirs:
            impl = Path(dir).parent.name.removeprefix("lib")
            csv_dirs = glob.glob(dir+"nclients*_nthreads*.csv")
            DATA[mb][impl] = {}
            for csv_dir in csv_dirs:
                match = re.search(r"nclients(\d+)_nthreads(\d+)\.csv", os.path.basename(csv_dir))
                nclients = int(match.group(1))
                nthreads = int(match.group(2))
                if not DATA[mb][impl].get(nclients):
                    DATA[mb][impl][nclients] = {}
                cleaned_lines = []
                with open(csv_dir, 'r') as file:
                    for line in file:
                        if line.startswith("---") or line.startswith("RUN") or line == "":
                            continue
                        cleaned_lines.append(line) 

                cleaned_data = StringIO("".join(cleaned_lines))
                DATA[mb][impl][nclients][nthreads] = pd.read_csv(cleaned_data, skiprows=1, names=COLS)
    

def read_data(DATA, RES_DIRS):
    DATA["client"] = {}
    DATA["server"] = {}
    DATA["orig"] = {}
    to_pd(DATA["client"], RES_DIRS["client"])
    to_pd(DATA["server"], RES_DIRS["server"], True)

def plots_emptycs(DATA):
    CD = DATA["client"]["empty_cs"]
    fig_whisk, ax_whisk = plt.subplots(figsize=(10, 6))
    fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
    fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
    position = 0
    x_positions = []
    x_labels = []
    for impl in CD:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for nclients in CD[impl]:
            for nthreads, values in CD[impl][nclients].items():
                duration = values["total_duration"][0] / 1e6

                ax_whisk.boxplot(values["lock_acquires"] / duration, positions=[position], widths=0.6, patch_artist=True)
                ax_whisk.text(position, np.average(values["lock_acquires"] / duration)+1e-6, f"{nclients}.{nthreads}", ha="center", va="bottom")

    ax_whisk.set_xticks(x_positions)
    ax_whisk.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_whisk.set_xlabel("Implementation")
    ax_whisk.set_ylabel("Throughput (lock acquisitions/us)")
    # ax_whisk.set_yscale('log')
    ax_whisk.set_title("Empty CS Throughput")
    ax_whisk.grid(linestyle="--", alpha=0.7)
    output_path = file_dir+f"/plots/tp_empty_cs.png"
    fig_whisk.savefig(output_path, dpi=300, bbox_inches='tight')

        # if SERVER_DATA:
        #     ax_bar.set_xticks(x_positions)
        #     ax_bar.set_xticklabels(x_labels, rotation=45, ha='right')
        #     ax_bar.set_xlabel("Implementation")
        #     ax_bar.set_ylabel("Time (s)")
        #     ax_bar.set_title("Median Latency Across Implementations")
        #     ax_bar.grid(linestyle="--", alpha=0.7)
        #     legend_comm = mlines.Line2D([], [], color='green', marker='o', linestyle='None', markersize=8, label="Network Communication")
        #     legend_impl = mlines.Line2D([], [], color='orange', marker='o', linestyle='None', markersize=8, label="Lock Implementation")
        #     ax_bar.legend(handles=[legend_comm, legend_impl])
        #     output_path = file_dir+f"/plots/latency_{nthreads}.png"
        #     fig_bar.savefig(output_path, dpi=300, bbox_inches='tight')

        # ax_fair.set_xticks(x_positions)
        # ax_fair.set_xticklabels(x_labels, rotation=45, ha='right')
        # ax_fair.set_xlabel("Implementation")
        # ax_fair.set_ylabel("Jain's Fairness Index")
        # ax_fair.set_title("Fairness Comparison Across Implementations "+orig)
        # ax_fair.grid(axis="y", linestyle="--", alpha=0.7)
        # # ax_fair.legend(loc="upper right")
        # output_path = file_dir+f"/plots/fairness_{nthreads}{orig}.png"
        # fig_fair.savefig(output_path, dpi=300, bbox_inches='tight')

def plots_mem(CLIENT_DATA, SERVER_DATA, incl_threads):
    for nthreads in incl_threads:
        fig, ax = plt.subplots(figsize=(10, 6))
        bar_width = 0.3
        offset = 1 / 3
        position = 0
        x_positions = []
        x_labels = []

        for impl in CLIENT_DATA:
            df = CLIENT_DATA[impl][nthreads]
            if df.empty:
                continue
            position += 1
            x_positions.append(position)
            x_labels.append(impl)
            max_dur = df.groupby("array_size")["total_duration"].max() * 1e6
            avg_ops = df.groupby("array_size")["loop_in_cs"].mean()
            tp = avg_ops / max_dur
            szs = df['array_size'].unique()
            ax.bar(position-offset, tp[szs[0]], width=bar_width, edgecolor='black', color="orange")
            ax.bar(position, tp[szs[1]], width=bar_width, edgecolor='black', color="green")
            ax.bar(position+offset, tp[szs[2]], width=bar_width, edgecolor='black', color="blue")
            # ax.bar(position, tp[szs[3]], width=bar_width, edgecolor='black', color="red")
        
        orig = "" if SERVER_DATA else "(ORIG)"

        ax.set_xticks(x_positions)
        ax.set_xticklabels(x_labels, rotation=45, ha='right')
        ax.set_xlabel("Implementation")
        ax.set_ylabel("TP (ops/ns)")
        ax.set_title("Access Array w/ Varying Size TP"+orig)
        # legend1 = mlines.Line2D([], [], color='orange', markersize=4, label=f"{szs[0] / 1024} KB")
        # legend2 = mlines.Line2D([], [], color='green', markersize=4, label=f"{szs[1] / 1024**2} MB")
        # legend3 = mlines.Line2D([], [], color='blue', markersize=4, label=f"{szs[2] / 1024**2} MB")
        # legend4 = mlines.Line2D([], [], color='red', markersize=4, label=f"{szs[3] / 1024**2} MB")
        # ax.legend(handles=[legend1, legend2, legend3, legend4])
        ax.grid(linestyle="--", alpha=0.7)
        output_path = file_dir+f"/plots/memtp_{nthreads}{orig}.png"
        fig.savefig(output_path, dpi=300, bbox_inches='tight')

def plots_lat(CLIENT_DATA, SERVER_DATA, incl_threads):
    FACTOR = 1e3
    for nthreads in incl_threads:
        fig, ax = plt.subplots(figsize=(10, 6))
        bar_width = 0.4
        # offset = 1 / 3
        position = 0
        x_positions = []
        x_labels = []

        for impl in CLIENT_DATA:
            df = CLIENT_DATA[impl][nthreads]
            if df.empty:
                continue
            position += 1
            x_positions.append(position)
            x_labels.append(impl)
            max_acq = df["lat_wait_acq"].max() / FACTOR
            max_rel = df["lat_wait_rel"].max() / FACTOR
            med_acq = df["lat_wait_acq"].median() / FACTOR
            med_rel = df["lat_wait_rel"].median() / FACTOR
            mean_acq = df["lat_wait_acq"].mean() / FACTOR
            mean_rel = df["lat_wait_rel"].mean() / FACTOR
            med_hold = df["lat_lock_hold"].median() / FACTOR
            if SERVER_DATA:
                df_s = SERVER_DATA[impl][nthreads]
                mean_impl_acq = df_s["wait_acq"].mean() / FACTOR
                mean_impl_rel = df_s["wait_rel"].mean() / FACTOR
                mean_comm = (mean_acq - mean_impl_acq) + (mean_rel - mean_impl_rel)
                mean_acq = mean_impl_acq
                mean_rel = mean_impl_rel
                ax.bar(position, mean_comm, width=bar_width, bottom=mean_acq+mean_rel, edgecolor='black', color="red")

            ax.bar(position, mean_acq, width=bar_width, edgecolor='black', color="orange")
            ax.bar(position, mean_rel, width=bar_width, bottom=mean_acq, edgecolor='black', color="blue")
            # ax.plot(position, max_acq, marker="x", color="red")
            # ax.plot(position, max_rel, marker="x", color="black")
            # ax.bar(position, tp[szs[3]], width=bar_width, edgecolor='black', color="red")
        
        orig = "" if SERVER_DATA else "(ORIG)"

        ax.set_xticks(x_positions)
        ax.set_xticklabels(x_labels, rotation=45, ha='right')
        ax.set_xlabel("Implementation")
        ax.set_ylabel("Latency (us)")
        ax.set_title(f"Latencies w/ {nthreads} Threads"+orig)
        legend1 = mlines.Line2D([], [], color='orange', markersize=4, label=f"Avg Acq Time")
        legend2 = mlines.Line2D([], [], color='blue', markersize=4, label=f"AvgMedian Rel Time")
        legend3 = mlines.Line2D([], [], color='red', markersize=4, label=f"Avg Comm Time")
        legend4 = mlines.Line2D([], [], color='red', marker='x', markersize=4, label=f"Max Acq Time")
        legend5 = mlines.Line2D([], [], color='black', marker='x', markersize=4, label=f"Max Rel Time")
        ax.legend(handles=[legend1, legend2, legend3])
        ax.grid(linestyle="--", alpha=0.7)
        output_path = file_dir+f"/plots/lat_{nthreads}{orig}.png"
        fig.savefig(output_path, dpi=300, bbox_inches='tight')

def plots_lat_both(CLIENT_DATA, SERVER_DATA, ORIG_DATA, incl_threads):
    FACTOR = 1e3
    for nthreads in incl_threads:
        fig, ax = plt.subplots(figsize=(10, 6))
        bar_width = 0.4
        offset = 1 / 4 
        position = 0
        x_positions = []
        x_labels = []

        for impl in CLIENT_DATA:
            df_c = CLIENT_DATA[impl][nthreads]
            df_o = ORIG_DATA[impl][nthreads]
            df_s = SERVER_DATA[impl][nthreads]

            if df_c.empty:
                continue

            position += 1
            x_positions.append(position)
            x_labels.append(impl)

            max_acq_c = df_c["lat_wait_acq"].max() / FACTOR
            max_rel_c = df_c["lat_wait_rel"].max() / FACTOR
            med_acq_c = df_c["lat_wait_acq"].median() / FACTOR
            med_rel_c = df_c["lat_wait_rel"].median() / FACTOR
            mean_acq_c = df_c["lat_wait_acq"].mean() / FACTOR
            mean_rel_c = df_c["lat_wait_rel"].mean() / FACTOR
            med_hold_c = df_c["lat_lock_hold"].median() / FACTOR

            max_acq_o = df_o["lat_wait_acq"].max() / FACTOR
            max_rel_o = df_o["lat_wait_rel"].max() / FACTOR
            med_acq_o = df_o["lat_wait_acq"].median() / FACTOR
            med_rel_o = df_o["lat_wait_rel"].median() / FACTOR
            mean_acq_o = df_o["lat_wait_acq"].mean() / FACTOR
            mean_rel_o = df_o["lat_wait_rel"].mean() / FACTOR
            med_hold_o = df_o["lat_lock_hold"].median() / FACTOR

            mean_impl_acq = df_s["wait_acq"].mean() / FACTOR
            mean_impl_rel = df_s["wait_rel"].mean() / FACTOR
            mean_comm = (mean_acq_c - mean_impl_acq) + (mean_rel_c - mean_impl_rel)

            ax.bar(position-offset, mean_impl_acq, width=bar_width, edgecolor='black', color="orange")
            ax.bar(position-offset, mean_impl_rel, width=bar_width, bottom=mean_impl_acq, edgecolor='black', color="blue")
            ax.bar(position-offset, mean_comm, width=bar_width, bottom=mean_impl_acq+mean_impl_rel, edgecolor='black', color="red")

            ax.bar(position+offset, mean_acq_o, width=bar_width, edgecolor='black', color="orange")
            ax.bar(position+offset, mean_rel_o, width=bar_width, bottom=mean_acq_o, edgecolor='black', color="blue")
        
        ax.set_xticks(x_positions)
        ax.set_xticklabels(x_labels, rotation=45, ha='right')
        ax.set_xlabel("Implementation")
        ax.set_ylabel("Latency (us)")
        ax.set_title(f"Latencies w/ {nthreads} Threads")
        legend1 = mlines.Line2D([], [], color='orange', markersize=4, label=f"Avg Acq Time")
        legend2 = mlines.Line2D([], [], color='blue', markersize=4, label=f"AvgMedian Rel Time")
        legend3 = mlines.Line2D([], [], color='red', markersize=4, label=f"Avg Comm Time")
        legend4 = mlines.Line2D([], [], color='red', marker='x', markersize=4, label=f"Max Acq Time")
        legend5 = mlines.Line2D([], [], color='black', marker='x', markersize=4, label=f"Max Rel Time")
        ax.legend(handles=[legend1, legend2, legend3])
        ax.grid(linestyle="--", alpha=0.7)
        output_path = file_dir+f"/plots/lat__both_{nthreads}.png"
        fig.savefig(output_path, dpi=300, bbox_inches='tight')

        
def prep_res_dirs(RES_DIRS):
    file_dir = os.path.dirname(os.path.realpath(__file__))
    RES_DIRS["client"] = {}
    RES_DIRS["server"] = {}
    RES_DIRS["orig"] = {}
    for mb in MICROBENCHES:
        client_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/disaggregated/client/*/{mb}/"
        client_res_dir = glob.glob(client_res_dir)
        server_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/disaggregated/server/*/{mb}/"
        server_res_dir = glob.glob(server_res_dir)
        orig_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/non_disaggregated/*/{mb}/"
        orig_res_dir = glob.glob(orig_res_dir)

        RES_DIRS["client"][mb] = client_res_dir
        RES_DIRS["server"][mb] = server_res_dir
        RES_DIRS["orig"][mb] = orig_res_dir

        
file_dir = os.path.dirname(os.path.realpath(__file__))
        
RES_DIRS = {}
DATA = {}

CD = {}
inc_disa = [1, 8, 16]
inc_disa_mem = [16]
inc_orig = [1, 16, 32]
inc_orig_mem = [32]
inc_orig_lat = [1,16,32]
inc_disa_lat = [1, 8, 16]

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)
plots_emptycs(DATA)
pass