import os
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
"array_size", "lat_lock_hold",
"lat_wait_acq", "lat_wait_rel"
]
SERVER_COLS = [
    "tid", "wait_acq", "wait_rel"
]

def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0

def read_data_emptycs(DATA, res_dir):
    for dir in res_dir:
        impl = Path(dir).parent.name.removeprefix("lib")
        print(impl)
        csv_dirs = glob.glob(dir+"nthread_*.csv")
        DATA[impl] = {}
        for csv_dir in csv_dirs:
            nthreads = int(os.path.basename(csv_dir).removeprefix("nthread_").removesuffix(".csv"))
            DATA[impl][nthreads] = {}
            with open(csv_dir, 'r') as file:
                current_run = None
                for line in file:
                    line = line.strip() 
                    if line.startswith("RUN"):
                        current_run = int(line.split()[1])
                        DATA[impl][nthreads][current_run] = {} 
                    elif line.startswith("---") or line == "" or line.startswith('tid'):
                        continue
                    else:
                        values = line.split(',')
                        thread = int(values[0])
                        DATA[impl][nthreads][current_run][thread] = values[1:]

def read_data(DATA, res_dir, server_data=False):
    COLS = SERVER_COLS if server_data else CLIENT_COLS
    for dir in res_dir:
        impl = Path(dir).parent.name.removeprefix("lib")
        print(impl)
        csv_dirs = glob.glob(dir+"nthread_*.csv")
        DATA[impl] = {}
        for csv_dir in csv_dirs:
            nthreads = int(os.path.basename(csv_dir).removeprefix("nthread_").removesuffix(".csv"))
            cleaned_lines = []
            with open(csv_dir, 'r') as file:
                for line in file:
                    if line.startswith("---") or line.startswith("RUN") or line == "":
                        continue
                    cleaned_lines.append(line) 

            cleaned_data = StringIO("".join(cleaned_lines))
            DATA[impl][nthreads] = pd.read_csv(cleaned_data, skiprows=1, names=COLS)

def plots_emptycs(CLIENT_DATA, SERVER_DATA={},  include_threads=[], DURATION=20):
    for nthreads in include_threads:
        fig_whisk, ax_whisk = plt.subplots(figsize=(10, 6))
        fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
        fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
        position = 0
        x_positions = []
        x_labels = []
        for impl in CLIENT_DATA:
            # CD[impl] = {}
            position += 1
            x_positions.append(position)
            x_labels.append(impl)
            # CD[impl][nthreads] = {"acq": [], "holdtime": []} 
            lock_acquisitions = []
            lock_holdtime = []
            lock_impltime = []
            wait_acq = []
            wait_rel = []
            fairness = []
            for run in CLIENT_DATA[impl][nthreads]:
                for thread, values in CLIENT_DATA[impl][nthreads][run].items():
                    lock_acq = float(values[1])
                    lock_hold = float(values[2])
                    wait_l = float(values[4])
                    wait_r = float(values[5])
                    # Thread id off by one on server
                    if SERVER_DATA:
                        lock_impl = float(SERVER_DATA[impl][nthreads][run][thread+1][0])
                        lock_impltime.append(lock_impl)
                    lock_acquisitions.append(lock_acq) 
                    lock_holdtime.append(lock_hold)
                    wait_acq.append(wait_l) 
                    wait_rel.append(wait_r)


            np_lockacq = np.array(lock_acquisitions)
            np_holdtime = np.array(lock_holdtime)
            np_impltime = np.array(lock_impltime)
            np_wait_acq = np.array(wait_acq)
            np_wait_rel = np.array(wait_rel)
            fairness = jain_fairness_index(np_lockacq)
            if SERVER_DATA:
                non_comm_time = (np_holdtime+np_impltime) / 1000
                comm_time = DURATION - (non_comm_time)
                mode_comm_time = np.median(comm_time)
                mode_noncomm_time = np.median(non_comm_time)
                mode_impl_time = np.median(np_impltime) / 1000
            # CD[impl][nthreads]["acq"] = np_lockacq
            # CD[impl][nthreads]["holdtime"] = np_holdtime

            ax_whisk.boxplot(np_lockacq / DURATION / 1e6, positions=[position], widths=0.6, patch_artist=True)
            ax_whisk.text(position, np.average(np_lockacq / DURATION / 1e6)+1e-6, f"{nthreads}", ha="center", va="bottom")

            if SERVER_DATA and len(lock_impltime):
                bar_width = 0.4
                offset = bar_width / 2
                pos_comm = position-offset
                pos_impl = position+offset
                ax_bar.bar(pos_comm, mode_comm_time, width=bar_width, label="Network Communication", color="green")
                ax_bar.plot(pos_comm, np.max(comm_time), marker='o', color="green")
                ax_bar.bar(pos_impl, mode_impl_time, width=bar_width, label="Lock Implementation", color="orange")
                ax_bar.plot(pos_impl, np.max(np_impltime) / 1000, marker='o', color="orange")
                ax_bar.text(pos_comm, 0.3, f"{mode_comm_time:.2f}", ha="center", va="bottom", fontsize=7)
                ax_bar.text(pos_impl, mode_impl_time+1, f"{mode_impl_time:.2f}", ha="center", va="bottom", fontsize=7)

            ax_fair.bar(position, fairness, width=0.6)


        orig = "" if SERVER_DATA else "(ORIG)"

        ax_whisk.set_xticks(x_positions)
        ax_whisk.set_xticklabels(x_labels, rotation=45, ha='right')
        ax_whisk.set_xlabel("Implementation")
        ax_whisk.set_ylabel("Throughput (lock acquisitions/us)")
        ax_whisk.set_title("Throughput Comparison Across Implementations "+orig)
        ax_whisk.grid(linestyle="--", alpha=0.7)
        output_path = file_dir+f"/plots/tp_{nthreads}{orig}.png"
        fig_whisk.savefig(output_path, dpi=300, bbox_inches='tight')

        if SERVER_DATA:
            ax_bar.set_xticks(x_positions)
            ax_bar.set_xticklabels(x_labels, rotation=45, ha='right')
            ax_bar.set_xlabel("Implementation")
            ax_bar.set_ylabel("Time (s)")
            ax_bar.set_title("Median Latency Across Implementations")
            ax_bar.grid(linestyle="--", alpha=0.7)
            legend_comm = mlines.Line2D([], [], color='green', marker='o', linestyle='None', markersize=8, label="Network Communication")
            legend_impl = mlines.Line2D([], [], color='orange', marker='o', linestyle='None', markersize=8, label="Lock Implementation")
            ax_bar.legend(handles=[legend_comm, legend_impl])
            output_path = file_dir+f"/plots/latency_{nthreads}.png"
            fig_bar.savefig(output_path, dpi=300, bbox_inches='tight')

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

file_dir = os.path.dirname(os.path.realpath(__file__))

client_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/client/*/empty_cs/"
client_res_dir = glob.glob(client_res_dir)
server_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/server/*/empty_cs/"
server_res_dir = glob.glob(server_res_dir)
orig_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/non_disaggregated/*/empty_cs/"
orig_res_dir = glob.glob(orig_res_dir)

client_res_dir_mem = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/client/*/mem/"
client_res_dir_mem = glob.glob(client_res_dir_mem)
server_res_dir_mem = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/server/*/mem/"
server_res_dir_mem = glob.glob(server_res_dir_mem)
orig_res_dir_mem = os.path.dirname(os.path.realpath(__file__))+"/results/non_disaggregated/*/mem/"
orig_res_dir_mem = glob.glob(orig_res_dir_mem)

client_res_dir_lat = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/client/*/lat/"
client_res_dir_lat = glob.glob(client_res_dir_lat)
server_res_dir_lat = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/server/*/lat/"
server_res_dir_lat = glob.glob(server_res_dir_lat)
orig_res_dir_lat = os.path.dirname(os.path.realpath(__file__))+"/results/non_disaggregated/*/lat/"
orig_res_dir_lat = glob.glob(orig_res_dir_lat)

CLIENT_DATA_EMPTYCS = {}
SERVER_DATA_EMPTYCS = {}
ORIG_DATA_EMPTYCS = {}

CLIENT_DATA_MEM = {}
SERVER_DATA_MEM = {}
ORIG_DATA_MEM = {}

CLIENT_DATA_LAT = {}
SERVER_DATA_LAT = {}
ORIG_DATA_LAT = {}

CD = {}
DURATION = 30. # sec
inc_disa = [1, 8, 16]
inc_disa_mem = [16]
inc_orig = [1, 16, 32]
inc_orig_mem = [32]
inc_orig_lat = [1,16,32]
inc_disa_lat = [1, 8, 16]

read_data_emptycs(CLIENT_DATA_EMPTYCS, client_res_dir)
read_data_emptycs(SERVER_DATA_EMPTYCS, server_res_dir)
read_data_emptycs(ORIG_DATA_EMPTYCS, orig_res_dir)

read_data(CLIENT_DATA_MEM, client_res_dir_mem)
read_data(SERVER_DATA_MEM, server_res_dir_mem, True)
read_data(ORIG_DATA_MEM, orig_res_dir_mem)

read_data(CLIENT_DATA_LAT, client_res_dir_lat)
read_data(SERVER_DATA_LAT, server_res_dir_lat, True)
read_data(ORIG_DATA_LAT, orig_res_dir_lat)

# plots_emptycs(CLIENT_DATA=CLIENT_DATA_EMPTYCS, SERVER_DATA=SERVER_DATA_EMPTYCS, include_threads=inc_disa, DURATION=DURATION)
# plots_emptycs(CLIENT_DATA=ORIG_DATA_EMPTYCS, SERVER_DATA={}, include_threads=inc_orig, DURATION=DURATION)

plots_mem(CLIENT_DATA_MEM, SERVER_DATA_MEM, inc_disa_mem)
# plots_mem(ORIG_DATA_MEM, {}, inc_orig_mem)

# plots_lat(CLIENT_DATA_LAT, SERVER_DATA_LAT, inc_disa_lat)
# plots_lat(ORIG_DATA_LAT, {}, inc_lat_orig)
# plots_lat_both(CLIENT_DATA_LAT, SERVER_DATA_LAT, ORIG_DATA_LAT, [1,16])