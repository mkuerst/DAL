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

COMM_PROTOCOLS = ["rdma", "tcp"]

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

IMPL = [
"alockepfl_original",
"backoff_original",
"cbomcs_spin_then_park",
"cbomcs_spinlock",
"clh_spin_then_park",
"clh_spinlock",
"cna_spinlock",
"concurrency_original",
"cptltkt_original",
"ctkttkt_original",
"fns_spinlock",
"fnm_spin_then_park",
"hmcs_original",
"htlockepfl_original",
"hyshmcs_original",
"malthusian_spin_then_park",
"malthusian_spinlock",
"mcs_spin_then_park",
"mcs_spinlock",
"partitioned_original",
"pthreadinterpose_original",
"spinlock_original",
"ticket_original",
"ttas_original",
]

MICROBENCHES = ["empty_cs1n", "empty_cs2n", "lat", "mem1n", "mem2n"]


def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0


def plots_SC(DATA):
    fig_emptycs, ax_emptycs = plt.subplots(figsize=(10, 6))
    fig_lat, ax_lat = plt.subplots(figsize=(10, 6))
    fig_mem2n_lat, ax_mem2n_lat = plt.subplots(figsize=(10, 6))
    fig_mem2n_tp, ax_mem2n_tp = plt.subplots(figsize=(10, 6))
    lat_bar_width = 0.3  # Width of each bar
    comm_prot_colors = {"tcp": "blue", "rdma": "green", "orig": "red"}
    comm_prot_offset = {"orig": -lat_bar_width, "rdma": 0, "tcp": lat_bar_width}
    lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "gold", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red"}
    nthreads_markers = {1: "o", 16: "x", 32: "^", 8: "D", 16: "P"}
    # fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
    # fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
    position = 0
    x_positions = []
    x_labels = []
    legend_nthreads = {}
    legend_commprot = {}
    legend_lat = {}
    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for comm_prot in DATA:
            color = comm_prot_colors.get(comm_prot, "gray")

            for remote_lock in DATA[comm_prot]:
                # DE1N = DATA[comm_prot][remote_lock]["client"]["empty_cs1n"]
                DE2N = DATA[comm_prot][remote_lock]["client"]["empty_cs2n"]
                DCL = DATA[comm_prot][remote_lock]["client"]["lat"]
                # DCM1N = DATA[comm_prot][remote_lock]["client"]["mem_1n"]
                DCM2N = DATA[comm_prot][remote_lock]["client"]["mem2n"]
                # DSL = DATA[comm_prot][remote_lock]["server"]["lat"]
                if impl in DE2N.keys():
                    for nclients in DE2N[impl]:
                        if nclients == 1:
                            ################################ EMPTY CS 2N############################################
                            for nthreads, values in DE2N[impl][nclients].items():
                                duration = values["total_duration"][0] * 1e6

                                marker = nthreads_markers.get(nthreads, "x")
                                if nthreads not in legend_nthreads:
                                    legend_nthreads[nthreads] = mlines.Line2D([], [],color="black",marker=marker,
                                        markersize=8, linestyle="None", label=f"{nthreads} threads")

                                ax_emptycs.scatter(
                                    [position],
                                    [values["lock_acquires"].mean() / duration], 
                                    color=color,
                                    marker=marker,
                                    label=comm_prot if position == 1 else "", 
                                    s=55,
                                    edgecolor="black", 
                                    alpha=0.5,
                                )
                            ######################################################################################


                            ################################ LAT ############################################
                            if impl in DCL.keys():
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
                            if impl in DCM2N.keys():
                                for nthreads, values in DCM2N[impl][nclients].items():
                                    if nthreads == 32:
                                        max_array_size = values["array_size"].max()
                                        FACTOR = 1e3
                                        values = values[values["array_size"] == max_array_size]
                                        lock_acquires = values["lock_acquires"]
                                        gwait_acq = ((values["gwait_acq"] / lock_acquires).mean() / FACTOR) if ("gwait_acq" in values) else 0
                                        ax_mem2n_lat.bar(position+comm_prot_offset[comm_prot], gwait_acq, width=lat_bar_width,
                                            color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_acq = (values["lwait_acq"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat.bar(position+comm_prot_offset[comm_prot], lwait_acq, width=lat_bar_width, bottom=gwait_acq,
                                            color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        gwait_rel = (values["gwait_rel"] / lock_acquires).mean() / FACTOR if ("gwait_rel" in values) else 0
                                        ax_mem2n_lat.bar(position+comm_prot_offset[comm_prot], gwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq,
                                            color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_rel = (values["lwait_rel"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat.bar(position+comm_prot_offset[comm_prot], lwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                                            color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lock_hold = (values["lock_hold"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat.bar(position+comm_prot_offset[comm_prot], lock_hold, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                                            color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                                            edgecolor="black"
                                        )

                                        

                                        duration = values["total_duration"].max() * 1e6
                                        ax_mem2n_tp.boxplot(
                                            values["lock_acquires"] / duration,
                                            positions=[position+comm_prot_offset[comm_prot]],
                                            widths=0.3,  # Make boxplots narrower
                                            patch_artist=True,  # Enable box color customization
                                            boxprops=dict(facecolor=comm_prot_colors.get(comm_prot, "gray")),
                                        )

                            ######################################################################################
            if comm_prot not in legend_commprot:
                legend_commprot[comm_prot] = mlines.Line2D([], [], color=color, marker="o",
                    markersize=2, linestyle="None",label=comm_prot)
                legend_nthreads[comm_prot] = mlines.Line2D([], [], color=color, marker="o",
                    markersize=2, linestyle="None",label=comm_prot)

    ax_emptycs.set_xticks(x_positions)
    ax_emptycs.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_emptycs.set_xlabel("Implementation")
    ax_emptycs.set_ylabel("Throughput (lock acquisitions/us)")
    ax_emptycs.set_yscale('log')
    ax_emptycs.set_title("EmptyCS TP 2N 1C")
    ax_emptycs.grid(linestyle="--", alpha=0.7)
    ax_emptycs.legend(legend_nthreads.values(), [entry.get_label() for entry in legend_nthreads.values()],
              title="", loc="upper left", bbox_to_anchor=(1.05, 1))
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
    output_path = file_dir+f"/plots/lat_emptycs_1C_32T.png"
    fig_lat.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_mem2n_lat.set_xticks(x_positions)
    ax_mem2n_lat.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_mem2n_lat.set_xlabel("Implementation")
    ax_mem2n_lat.set_ylabel("Latency Means (us)")
    ax_mem2n_lat.set_yscale('log')
    ax_mem2n_lat.set_title("Memory Latency 2N 1C 32T")
    ax_mem2n_lat.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax_mem2n_lat.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/avglat_mem2n_1C_ 32T.png"
    fig_mem2n_lat.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_mem2n_tp.set_xticks(x_positions)
    ax_mem2n_tp.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_mem2n_tp.set_xlabel("Implementation")
    ax_mem2n_tp.set_ylabel("Throughput (lock acquisitions/us)")
    ax_mem2n_tp.set_yscale('log')
    ax_mem2n_tp.set_title("Memory TP 2N 1C 32T")
    ax_mem2n_tp.grid(linestyle="--", alpha=0.7)
    ax_mem2n_tp.legend(legend_commprot.values(), [entry.get_label() for entry in legend_commprot.values()],
              title="Communication Protocol", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_mem2n_1C_32T.png"
    fig_mem2n_tp.savefig(output_path, dpi=300, bbox_inches='tight')


    # ax_fair.set_xticks(x_positions)
    # ax_fair.set_xticklabels(x_labels, rotation=45, ha='right')
    # ax_fair.set_xlabel("Implementation")
    # ax_fair.set_ylabel("Jain's Fairness Index")
    # ax_fair.set_title("Fairness")
    # ax_fair.grid(linestyle="--", alpha=0.7)
    # # ax_fair.legend(loc="upper right")
    # output_path = file_dir+f"/plots/fairness_{nthreads}{orig}.png"
    # fig_fair.savefig(output_path, dpi=300, bbox_inches='tight')

    
    
def plots_1v2N(DATA):
    fig_emptycs1v2n, ax_emptycs1v2n = plt.subplots(figsize=(10, 6))
    fig_tpmem1v2n, ax_tpmem1v2n = plt.subplots(figsize=(10, 6))
    fig_latmem1v2n, ax_latmem1v2n = plt.subplots(figsize=(10, 6))

    bar_width = 0.4
    offset = bar_width / 2
    position = 0
    x_positions = []
    x_labels = []
    legend_empty = {}
    legend_lat = {}

    comm_prots = ["orig"]
    lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "gold", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red"}
    empty_colors = {"empty_cs1n": "gray", "empty_cs2n": "black"}
    mem_colors = {"mem1n": "gray", "mem2n": "black"}
    mem_offset = {"mem1n": -offset, "mem2n": offset}
    empty_offset = {"empty_cs1n": -offset, "empty_cs2n": offset}

    empty_benches = ["empty_cs1n", "empty_cs2n"]
    mem_becnhes = ["mem1n", "mem2n"]

    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for comm_prot in comm_prots:
            ################################ EMPTY CS 1N vs 2N ############################################
            for empty_bench in empty_benches:
                DE1N = DATA[comm_prot]["none"]["client"][empty_bench]
                for nthreads, values in DE1N[impl][1].items():
                    duration = values["total_duration"][0] * 1e6
                    if nthreads == 16:
                        ax_emptycs1v2n.boxplot(
                            values["lock_acquires"] / duration,
                            positions=[position+empty_offset[empty_bench]],
                            widths=bar_width, 
                            patch_artist=True,
                            boxprops=dict(facecolor=empty_colors.get(empty_bench, "gray")),
                        )

            ######################################################################################


            ################################ MEM_TP | MEM_LAT 1N vs 2N############################################
            for mem_bench in mem_becnhes:
                DM1N = DATA[comm_prot]["none"]["client"][mem_bench]
                for nthreads, values in DM1N[impl][1].items():
                    if nthreads == 16:
                        max_array_size = values["array_size"].max()
                        FACTOR = 1e3
                        values = values[values["array_size"] == max_array_size]
                        lock_acquires = values["lock_acquires"]
                        gwait_acq = ((values["gwait_acq"] / lock_acquires).mean() / FACTOR) if ("gwait_acq" in values) else 0
                        ax_latmem1v2n.bar(position+mem_offset[mem_bench], gwait_acq, width=bar_width,
                            color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lwait_acq = (values["lwait_acq"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n.bar(position+mem_offset[mem_bench], lwait_acq, width=bar_width, bottom=gwait_acq,
                            color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                            edgecolor="black"
                        )
                        gwait_rel = (values["gwait_rel"] / lock_acquires).mean() / FACTOR if ("gwait_rel" in values) else 0
                        ax_latmem1v2n.bar(position+mem_offset[mem_bench], gwait_rel, width=bar_width, bottom=gwait_acq+lwait_acq,
                            color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lwait_rel = (values["lwait_rel"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n.bar(position+mem_offset[mem_bench], lwait_rel, width=bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                            color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lock_hold = (values["lock_hold"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n.bar(position+mem_offset[mem_bench], lock_hold, width=bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                            color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                            edgecolor="black"
                        )

                        

                        duration = values["total_duration"].max() * 1e6
                        ax_tpmem1v2n.boxplot(
                            values["lock_acquires"] / duration,
                            positions=[position+mem_offset[mem_bench]],
                            widths=0.3,  # Make boxplots narrower
                            patch_artist=True,  # Enable box color customization
                            boxprops=dict(facecolor=mem_colors.get(mem_bench, "gray")),
                        )
            ######################################################################################


    legend_empty[0] = mlines.Line2D([], [],color="gray",marker="o",
        markersize=8, linestyle="None", label=f"1 Node")
    legend_empty[1] = mlines.Line2D([], [],color="black",marker="o",
        markersize=8, linestyle="None", label=f"2 Nodes")

    ax_emptycs1v2n.set_xticks(x_positions)
    ax_emptycs1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_emptycs1v2n.set_xlabel("Implementation")
    ax_emptycs1v2n.set_ylabel("Throughput (lock acquisitions/us)")
    ax_emptycs1v2n.set_yscale('log')
    ax_emptycs1v2n.set_title("TP Empty CS 1v2N 16 Threads") 
    ax_emptycs1v2n.grid(linestyle="--", alpha=0.7)
    ax_emptycs1v2n.legend(legend_empty.values(), [entry.get_label() for entry in legend_empty.values()],
              title="Node Pinning", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_empty_cs1v2n_1C_16T.png"
    fig_emptycs1v2n.savefig(output_path, dpi=300, bbox_inches='tight')


    ax_latmem1v2n.set_xticks(x_positions)
    ax_latmem1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_latmem1v2n.set_xlabel("Implementation")
    ax_latmem1v2n.set_ylabel("Latency Means (us)")
    ax_latmem1v2n.set_yscale('log')
    ax_latmem1v2n.set_title("Latency Memory 1v2N")
    ax_latmem1v2n.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax_latmem1v2n.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/avglat_mem1v2n_1C_16T.png"
    fig_latmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_tpmem1v2n.set_xticks(x_positions)
    ax_tpmem1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_tpmem1v2n.set_xlabel("Implementation")
    ax_tpmem1v2n.set_ylabel("Throughput (lock acquisitions/us)")
    ax_tpmem1v2n.set_yscale('log')
    ax_tpmem1v2n.set_title("TP Memory 1v2N 1 Client 16 Threads")
    ax_tpmem1v2n.grid(linestyle="--", alpha=0.7)
    ax_tpmem1v2n.legend(legend_empty.values(), [entry.get_label() for entry in legend_empty.values()],
              title="Node Pinning", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_mem1v2n_1C_16T.png"
    fig_tpmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')


#################################################################################################################################
#################################################################################################################################
#################################################################################################################################

def to_pd(DATA, RES_DIRS, COLS):
    for mb,dirs in RES_DIRS.items():
        DATA[mb] = {}
        for dir in dirs:
            impl = Path(dir).parent.name.removeprefix("lib")
            csv_dirs = glob.glob(dir+"nclients*_nthreads*.csv")
            DATA[mb][impl] = {}
            for csv_dir in csv_dirs:
                match = re.search(r"nclients(\d+)_nthreads(\d+).csv", os.path.basename(csv_dir))
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
    to_pd(DATA["orig"]["none"]["client"], RES_DIRS["orig"], CLIENT_RDMA_COLS)

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

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)
plots_SC(DATA)
plots_1v2N(DATA)