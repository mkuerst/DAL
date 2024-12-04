import os
import glob
# import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt

def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0

def read_data(DATA, res_dir):
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


def plots(CD, include_threads=[]):
    title_thr = "all" if len(include_threads) == 0 else "_".join(map(str, include_threads))
    fig_whisk, ax_whisk = plt.subplots(figsize=(10, 6))
    fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
    fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
    position = 0
    x_positions = []
    x_labels = []
    for impl in CLIENT_DATA:
        CD[impl] = {}
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for nthreads in CLIENT_DATA[impl]:
            CD[impl][nthreads] = {"acq": [], "holdtime": []} 
            num_runs = 0
            lock_acquisitions = []
            lock_holdtime = []
            lock_impltime = []
            fairness = []
            for run in CLIENT_DATA[impl][nthreads]:
                num_runs += 1
                for thread, values in CLIENT_DATA[impl][nthreads][run].items():
                    lock_acq = float(values[1])
                    lock_hold = float(values[2])
                    # Thread id off by one on server
                    lock_impl = float(SERVER_DATA[impl][nthreads][run][thread+1][0])
                    lock_acquisitions.append(lock_acq) 
                    lock_holdtime.append(lock_hold)
                    lock_impltime.append(lock_impl)

            np_lockacq = np.array(lock_acquisitions)
            np_holdtime = np.array(lock_holdtime)
            np_impltime = np.array(lock_impltime)
            fairness = jain_fairness_index(np_lockacq)
            non_comm_time = (np_holdtime+np_impltime) / 1000
            comm_time = DURATION - (non_comm_time)
            CD[impl][nthreads]["acq"] = np_lockacq
            CD[impl][nthreads]["holdtime"] = np_holdtime
            avg_comm_time = np.mean(comm_time)
            avg_noncomm_time = np.mean(non_comm_time)

            if (len(include_threads) == 0) or (nthreads in include_threads):
                ax_whisk.boxplot(np_lockacq / DURATION / 1e6, positions=[position], widths=0.6, patch_artist=True)
                ax_whisk.text(position, np.average(np_lockacq / DURATION / 1e6)+1e-6, f"{nthreads}", ha="center", va="bottom")

                ax_bar.bar(position, avg_comm_time, width=0.6, label="Communication Time", color="blue")
                ax_bar.bar(position, avg_noncomm_time, width=0.6, bottom=avg_comm_time,label="Non-Communication Time", color="orange")
                ax_bar.text(position, avg_comm_time+1, f"{avg_comm_time:.2f}", ha="center", va="bottom", fontsize=7)

                ax_fair.bar(position, fairness, width=0.6)


    ax_whisk.set_xticks(x_positions)
    ax_whisk.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_whisk.set_xlabel("Implementation")
    ax_whisk.set_ylabel("Throughput (lock acquisitions/us)")
    ax_whisk.set_title("Throughput Comparison Across Implementations")
    ax_whisk.grid(axis="y", linestyle="--", alpha=0.7)
    output_path = file_dir+f"/plots/throughput_comparison_{title_thr}.png"
    fig_whisk.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_bar.set_xticks(x_positions)
    ax_bar.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_bar.set_xlabel("Implementation")
    ax_bar.set_ylabel("Time (s)")
    ax_bar.set_title("Communication/Non-Communication Time Comparison Across Implementations")
    ax_bar.grid(axis="y", linestyle="--", alpha=0.7)
    # ax_bar.legend(loc="upper right")
    output_path = file_dir+f"/plots/comm_comparison_{title_thr}.png"
    fig_bar.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_fair.set_xticks(x_positions)
    ax_fair.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_fair.set_xlabel("Implementation")
    ax_fair.set_ylabel("Jain's Fairness Index")
    ax_fair.set_title("Fairness Comparison Across Implementations")
    ax_fair.grid(axis="y", linestyle="--", alpha=0.7)
    # ax_fair.legend(loc="upper right")
    output_path = file_dir+f"/plots/fairness_comparison_{title_thr}.png"
    fig_fair.savefig(output_path, dpi=300, bbox_inches='tight')

file_dir = os.path.dirname(os.path.realpath(__file__))
client_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/client/*/empty_cs/"
client_res_dir = glob.glob(client_res_dir)
server_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/server/*/empty_cs/"
server_res_dir = glob.glob(server_res_dir)

CLIENT_DATA = {}
SERVER_DATA = {}
DURATION = 20. # sec
CD = {}
include_threads = [29]

read_data(CLIENT_DATA, client_res_dir)
read_data(SERVER_DATA, server_res_dir)

plots(CD, include_threads)