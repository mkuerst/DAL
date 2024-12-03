import os
import glob
import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt

CLIENT_DATA = {}
SERVER_DATA = {}
DURATION = 20. # sec

file_dir = os.path.dirname(os.path.realpath(__file__))
client_res_dir = os.path.dirname(os.path.realpath(__file__))+"/results/disaggregated/client/*/empty_cs/"
client_res_dir = glob.glob(client_res_dir)
for dir in client_res_dir:
    impl = Path(dir).parent.name.removeprefix("lib")
    print(impl)
    csv_dirs = glob.glob(dir+"nthread_*.csv")
    CLIENT_DATA[impl] = {}
    for csv_dir in csv_dirs:
        nthreads = int(os.path.basename(csv_dir).removeprefix("nthread_").removesuffix(".csv"))
        CLIENT_DATA[impl][nthreads] = {}
        with open(csv_dir, 'r') as file:
            current_run = None
            for line in file:
                line = line.strip() 
                if line.startswith("RUN"):
                    current_run = int(line.split()[1])
                    CLIENT_DATA[impl][nthreads][current_run] = {} 
                elif line.startswith("---") or line == "" or line.startswith('tid'):
                    continue
                else:
                    values = line.split(',')
                    thread = int(values[0])
                    CLIENT_DATA[impl][nthreads][current_run][thread] = values[1:]

CD = {}
fig, ax = plt.subplots(figsize=(10, 6))
position = 1
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
        for run in CLIENT_DATA[impl][nthreads]:
            num_runs += 1
            for thread, values in CLIENT_DATA[impl][nthreads][run].items():
                lock_acq = float(values[1])
                lock_hold = float(values[2])
                lock_acquisitions.append(lock_acq) 
                lock_holdtime.append(lock_hold)

        np_lockacq = np.array(lock_acquisitions) / DURATION / 1e6
        np_holdtime = np.array(lock_holdtime)
        CD[impl][nthreads]["acq"] = np_lockacq
        CD[impl][nthreads]["holdtime"] = np_holdtime

        if nthreads == 29:
            ax.boxplot(np_lockacq, positions=[position], widths=0.6, patch_artist=True)
            ax.text(position, np.average(np_lockacq)+1e-6, f"{nthreads}", ha="center", va="bottom")
            title_thr = nthreads


ax.set_xticks(x_positions)
ax.set_xticklabels(x_labels, rotation=45, ha='right')

# Add labels and title
ax.set_xlabel("Implementation")
ax.set_ylabel("Throughput (lock acquisitions/us)")
ax.set_title("Throughput Comparison Across Implementations")
ax.grid(axis="y", linestyle="--", alpha=0.7)

output_path = file_dir+f"/throughput_comparison_{title_thr}.png"
plt.savefig(output_path, dpi=300, bbox_inches='tight')