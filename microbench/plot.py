import os
import re
from io import StringIO
import glob
import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.lines as mlines            

REMOTE_LOCKS = ["spinlock"]

COMM_PROTOCOLS = ["tcp", "rdma"]

ORIG_COLS = [
"tid", "loop_in_cs", "lock_acquires", 
"lock_hold", "total_duration", 
"lwait_acq", "lwait_rel", "array_size",
"lat_lock_hold", "lat_lwait_acq", "lat_lwait_rel",
]

CLIENT_TCP_COLS = [
"tid", "loop_in_cs", "lock_acquires", 
"lock_hold", "total_duration", 
"wait_acq", "wait_rel", 
"lwait_acq", "lwait_rel",
"gwait_acq", "gwait_rel",
"array_size", "client_id",
"run"
]
CLIENT_RDMA_COLS = [
"tid", "loop_in_cs", "lock_acquires", 
"lock_hold", "total_duration", 
"wait_acq", "wait_rel", 
"lwait_acq", "lwait_rel",
"gwait_acq", "gwait_rel",
"glock_tries",
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

def to_pd(DATA, RES_DIRS, COLS):
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
    for comm_prot in COMM_PROTOCOLS:
        CLIENT_COLS = CLIENT_TCP_COLS if comm_prot == "tcp" else CLIENT_RDMA_COLS
        for remote_lock in REMOTE_LOCKS:
            DATA[comm_prot] = {remote_lock: {"client": {}, "server": {}}}
            to_pd(DATA[comm_prot][remote_lock]["client"], RES_DIRS[comm_prot][remote_lock]["client"], CLIENT_COLS)
            to_pd(DATA[comm_prot][remote_lock]["server"], RES_DIRS[comm_prot][remote_lock]["server"], SERVER_COLS)

    DATA["orig"] = {"none": {"client": {}}}
    to_pd(DATA["orig"]["none"]["client"], RES_DIRS["orig"], ORIG_COLS)

def plots(DATA):
    CD = DATA["tcp"]["spinlock"]["client"]["empty_cs"]
    fig_emptycs, ax_emptycs = plt.subplots(figsize=(10, 6))
    fig_lat, ax_lat = plt.subplots(figsize=(10, 6))
    fig_mem2n, ax_mem2n = plt.subplots(figsize=(10, 6))
    lat_bar_width = 0.3  # Width of each bar
    comm_prot_colors = {"tcp": "blue", "rdma": "green", "orig": "red"}
    comm_prot_offset = {"orig": -lat_bar_width, "rdma": 0, "tcp": lat_bar_width}
    lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "pink", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red",
                      "lat_lwait_acq": "pink", "lat_lwait_rel": "green", "lat_lock_hold": "red"}
    nthreads_markers = {1: "o", 16: "x", 32: "^", 8: "D", 16: "P"}
    # fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
    # fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
    position = 0
    x_positions = []
    x_labels = []
    legend_nthreads = {}
    legend_commprot = {}
    legend_lat = {}
    for impl in CD:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for comm_prot in DATA:
            for remote_lock in DATA[comm_prot]:
                DE = DATA[comm_prot][remote_lock]["client"]["empty_cs"]
                DCL = DATA[comm_prot][remote_lock]["client"]["lat"]
                DCM = DATA[comm_prot][remote_lock]["client"]["mem_2nodes"]
                # DSL = DATA[comm_prot][remote_lock]["server"]["lat"]
                for nclients in DE[impl]:
                    ################################ EMPTY CS ############################################
                    for nthreads, values in DE[impl][nclients].items():
                        duration = values["total_duration"][0] / 1e6

                        # ax_whisk.plot(position, values["lock_acquires"].mean() / duration, label=comm_prot)
                        # ax_whisk.boxplot(values["lock_acquires"] / duration, positions=[position], label=comm_prot, widths=0.6)
                        # ax_whisk.text(position, np.average(values["lock_acquires"] / duration)+1e-6, f"{comm_prot[0]}.{nclients}.{nthreads}", ha="center", va="bottom")

                        # ax_whisk.boxplot(
                        #     values["lock_acquires"] / duration,
                        #     positions=[position],
                        #     widths=0.4,  # Make boxplots narrower
                        #     patch_artist=True,  # Enable box color customization
                        #     boxprops=dict(facecolor=comm_prot_colors.get(comm_prot, "gray")),
                        # )
                        color = comm_prot_colors.get(comm_prot, "gray")
                        marker = nthreads_markers.get(nthreads, "x")

                        if comm_prot not in legend_commprot:
                            legend_commprot[comm_prot] = mlines.Line2D([], [], color=color, marker="o",
                                markersize=2, linestyle="None",label=comm_prot)

                        if nthreads not in legend_nthreads:
                            legend_nthreads[nthreads] = mlines.Line2D([], [],color="black",marker=marker,
                                markersize=8, linestyle="None", label=f"{nthreads} threads")

                        ax_emptycs.scatter(
                            [position],  # X position
                            [values["lock_acquires"].mean() / duration],  # Y value (average throughput)
                            color=color,  # Color based on comm_prot
                            marker=marker,
                            label=comm_prot if position == 1 else "",  # Add legend only once per comm_prot
                            s=55,  # Size of the scatter point
                            edgecolor="black",  # Optional: add border to points
                            alpha=0.5,
                        )
                    ######################################################################################

                    ################################ LAT ############################################
                    for nthreads, values in DCL[impl][nclients].items():
                        if nthreads == 1:
                            FACTOR = 1e3
                            gwait_acq = (values["gwait_acq"].median() / FACTOR) if ("gwait_acq" in values) else 0
                            ax_lat.bar(position+comm_prot_offset[comm_prot], gwait_acq, width=lat_bar_width,
                                color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lwait_acq = values["lwait_acq"].median() / FACTOR if comm_prot != "orig" else values["lat_lwait_acq"].median() / FACTOR
                            ax_lat.bar(position+comm_prot_offset[comm_prot], lwait_acq, width=lat_bar_width, bottom=gwait_acq,
                                color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                                edgecolor="black"
                            )
                            gwait_rel = values["gwait_rel"].median() / FACTOR if ("gwait_rel" in values) else 0
                            ax_lat.bar(position+comm_prot_offset[comm_prot], gwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq,
                                color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lwait_rel = values["lwait_rel"].median() / FACTOR if comm_prot != "orig" else values["lat_lwait_rel"].median() / FACTOR
                            ax_lat.bar(position+comm_prot_offset[comm_prot], lwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                                color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lock_hold = values["lock_hold"].median() if comm_prot != "orig" else values["lat_lock_hold"].median() / FACTOR
                            ax_lat.bar(position+comm_prot_offset[comm_prot], lock_hold, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                                color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                                edgecolor="black"
                            )
                    ######################################################################################


                    ###################################### MEM 2NODES ################################################
                    for nthreads, values in DCM[impl][nclients].items():
                        if nthreads == 32:
                            max_array_size = values["array_size"].max()
                            FACTOR = 1e3
                            values = values[values["array_size"] == max_array_size]
                            gwait_acq = (values["gwait_acq"].mean() / FACTOR) if ("gwait_acq" in values) else 0
                            ax_mem2n.bar(position+comm_prot_offset[comm_prot], gwait_acq, width=lat_bar_width,
                                color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lwait_acq = values["lwait_acq"].mean() / FACTOR if comm_prot != "orig" else values["lat_lwait_acq"].mean() / FACTOR
                            ax_mem2n.bar(position+comm_prot_offset[comm_prot], lwait_acq, width=lat_bar_width, bottom=gwait_acq,
                                color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                                edgecolor="black"
                            )
                            gwait_rel = values["gwait_rel"].mean() / FACTOR if ("gwait_rel" in values) else 0
                            ax_mem2n.bar(position+comm_prot_offset[comm_prot], gwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq,
                                color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lwait_rel = values["lwait_rel"].mean() / FACTOR if comm_prot != "orig" else values["lat_lwait_rel"].mean() / FACTOR
                            ax_mem2n.bar(position+comm_prot_offset[comm_prot], lwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                                color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                                edgecolor="black"
                            )
                            lock_hold = values["lock_hold"].mean() if comm_prot != "orig" else values["lat_lock_hold"].mean() / FACTOR
                            ax_mem2n.bar(position+comm_prot_offset[comm_prot], lock_hold, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                                color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                                edgecolor="black"
                            )
                    ######################################################################################

    ax_emptycs.set_xticks(x_positions)
    ax_emptycs.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_emptycs.set_xlabel("Implementation")
    ax_emptycs.set_ylabel("Throughput (lock acquisitions/us)")
    ax_emptycs.set_yscale('log')
    ax_emptycs.set_title("Empty CS Throughput with 1 Client and 2 Node Pinning")
    ax_emptycs.grid(linestyle="--", alpha=0.7)
    #TODO: not visible
    ax_emptycs.legend(legend_nthreads.values(), [entry.get_label() for entry in legend_nthreads.values()],
              title="Number of Threads", loc="upper right")
    ax_emptycs.legend(legend_commprot.values(), [entry.get_label() for entry in legend_commprot.values()],
              title="Communication Protocol", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_empty_cs.png"
    fig_emptycs.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_lat.set_xticks(x_positions)
    ax_lat.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_lat.set_xlabel("Implementation")
    ax_lat.set_ylabel("Latency Medians (us)")
    # ax_lat.set_yscale('log')
    ax_lat.set_title("Latency")
    ax_lat.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax_lat.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/lat.png"
    fig_lat.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_mem2n.set_xticks(x_positions)
    ax_mem2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_mem2n.set_xlabel("Implementation")
    ax_mem2n.set_ylabel("Latency Means (us)")
    # ax_mem2n.set_yscale('log')
    ax_mem2n.set_title("Latency of Memory MB with 2 NUMA Nodes")
    ax_mem2n.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax_mem2n.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/mem_avglats.png"
    fig_lat.savefig(output_path, dpi=300, bbox_inches='tight')
        # ax_fair.set_xticks(x_positions)
        # ax_fair.set_xticklabels(x_labels, rotation=45, ha='right')
        # ax_fair.set_xlabel("Implementation")
        # ax_fair.set_ylabel("Jain's Fairness Index")
        # ax_fair.set_title("Fairness Comparison Across Implementations "+orig)
        # ax_fair.grid(axis="y", linestyle="--", alpha=0.7)
        # # ax_fair.legend(loc="upper right")
        # output_path = file_dir+f"/plots/fairness_{nthreads}{orig}.png"
        # fig_fair.savefig(output_path, dpi=300, bbox_inches='tight')

def prep_res_dirs(RES_DIRS):
    for comm_prot in COMM_PROTOCOLS:
        RES_DIRS[comm_prot] = {}
        for remote_lock in REMOTE_LOCKS:
            RES_DIRS[comm_prot][remote_lock] = {"client" : {}, "server": {}}
            for mb in MICROBENCHES:
                client_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/client/*/{mb}/"
                client_res_dir = glob.glob(client_res_dir)
                server_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/server/*/{mb}/"
                server_res_dir = glob.glob(server_res_dir)

                RES_DIRS[comm_prot][remote_lock]["client"][mb] = client_res_dir
                RES_DIRS[comm_prot][remote_lock]["server"][mb] = server_res_dir

    RES_DIRS["orig"] = {}
    for mb in MICROBENCHES:
        orig_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/orig/*/{mb}/"
        orig_res_dir = glob.glob(orig_res_dir)
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
plots(DATA)