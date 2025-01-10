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
"data_read", "data_write",
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
# "fns_spinlock",
# "fnm_spin_then_park",
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

# TODO: Add "single"
STATS = ["cum"]

lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "gold", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red"}
median_colors = {"gwait_acq": "silver", "lwait_acq": "orange", "gwait_rel": "violet", "lwait_rel": "cyan", "lock_hold": "purple"}
node_colors = {"empty_cs1n": "gray", "empty_cs2n": "black", "mem1n": "gray", "mem2n": "black"}

FIG_X = 10
FIG_Y = 6
BAR_WIDTH = 0.3
DURATION_FACTOR = 1e3
LAT_FACTOR = 1

def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0


def plots_SC(DATA):
    fig_emptycs, ax_emptycs = plt.subplots(figsize=(10, 6))
    fig_lat, ax_lat = plt.subplots(figsize=(10, 6))
    fig_mem2n_lat_comprot, ax_mem2n_lat_commprot = plt.subplots(figsize=(10, 6))
    ax_mem2n_lat_commprot2 = ax_mem2n_lat_commprot.twinx()
    fig_mem2n_tp, ax_mem2n_tp = plt.subplots(figsize=(10, 6))
    ax_mem2n_tp_fair = ax_mem2n_tp.twinx()
    lat_bar_width = 0.3  # Width of each bar
    comm_prot_colors = {"tcp": "blue", "rdma": "green", "orig": "red"}
    comm_prot_offset = {"orig": -lat_bar_width, "rdma": 0, "tcp": lat_bar_width}
    lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "gold", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red"}
    nthreads_markers = {1: "o", 16: "x", 32: "^", 8: "D", 16: "P"}

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
                DE2N = DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"]
                DCL = DATA[comm_prot][remote_lock]["client"]["cum"]["lat"]
                DCM2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"]
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
                                        FACTOR = 1
                                        values = values[values["array_size"] == max_array_size]
                                        lock_acquires = values["lock_acquires"]
                                        gwait_acq = ((values["gwait_acq"] / lock_acquires).mean() / FACTOR) if ("gwait_acq" in values) else 0
                                        ax_mem2n_lat_commprot.bar(position+comm_prot_offset[comm_prot], gwait_acq, width=lat_bar_width,
                                            color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_acq = 0 if comm_prot != "orig" else (values["wait_acq"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat_commprot.bar(position+comm_prot_offset[comm_prot], lwait_acq, width=lat_bar_width, bottom=gwait_acq,
                                            color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        gwait_rel = (values["gwait_rel"] / lock_acquires).mean() / FACTOR if ("gwait_rel" in values) else 0
                                        ax_mem2n_lat_commprot.bar(position+comm_prot_offset[comm_prot], gwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq,
                                            color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_rel = 0 if comm_prot != "orig" else (values["wait_rel"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat_commprot.bar(position+comm_prot_offset[comm_prot], lwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                                            color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lock_hold = (values["lock_hold"] / lock_acquires).mean() / FACTOR
                                        ax_mem2n_lat_commprot.bar(position+comm_prot_offset[comm_prot], lock_hold, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                                            color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        ax_mem2n_lat_commprot2.scatter([position+comm_prot_offset[comm_prot]], [lock_acquires.mean() / duration], marker="x", color="black")

                                        

                                        duration = values["total_duration"].max() * 1e6
                                        ax_mem2n_tp.boxplot(
                                            values["lock_acquires"] / duration,
                                            positions=[position+comm_prot_offset[comm_prot]],
                                            widths=0.3,
                                            patch_artist=True,
                                            boxprops=dict(facecolor=comm_prot_colors.get(comm_prot, "gray"),),
                                        )
                                        ax_mem2n_tp_fair.scatter([position+comm_prot_offset[comm_prot]], jain_fairness_index(lock_acquires), color=comm_prot_colors[comm_prot],
                                                                 marker='^', edgecolors="black")

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

    # ax_lat.set_xticks(x_positions)
    # ax_lat.set_xticklabels(x_labels, rotation=45, ha='right')
    # ax_lat.set_xlabel("Implementation")
    # ax_lat.set_ylabel("Latency Medians (us)")
    # # ax_lat.set_yscale('log')
    # ax_lat.set_title("Latency")
    # ax_lat.grid(linestyle="--", alpha=0.7)
    # for metric,color in lat_bar_colors.items():
    #     legend_lat[metric] = mlines.Line2D(
    #                             [], [],
    #                             color=color,
    #                             marker="o",
    #                             markersize=8,
    #                             linestyle="None",
    #                             label=metric)
    # ax_lat.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
    #           title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    # output_path = file_dir+f"/plots/lat_emptycs_1C_32T.png"
    # fig_lat.savefig(output_path, dpi=300, bbox_inches='tight')


    ax_mem2n_lat_commprot.set_xticks(x_positions)
    ax_mem2n_lat_commprot.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_mem2n_lat_commprot.set_xlabel("Implementation")
    ax_mem2n_lat_commprot.set_ylabel("Mean Latencies (ms)")
    ax_mem2n_lat_commprot2.set_ylabel("TP (ops/ms)")
    # ax_mem2n_lat_commprot.set_yscale('log')
    ax_mem2n_lat_commprot.set_title("Memory Waiting Latencies 2N 1C 32T")
    ax_mem2n_lat_commprot.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax_mem2n_lat_commprot.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/avglatwait_mem2n_1C_32T.png"
    fig_mem2n_lat_comprot.savefig(output_path, dpi=300, bbox_inches='tight')


    ax_mem2n_tp.set_xticks(x_positions)
    ax_mem2n_tp.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_mem2n_tp.set_xlabel("Implementation")
    ax_mem2n_tp.set_ylabel("Throughput (lock acquisitions/us)")
    ax_mem2n_tp_fair.set_ylabel("Jain's Fairness Index")
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

    
def plot_XvY(DATA, x, y):
    comm_prots = [x, y]
    fig, ax = plt.subplots(figsize=(10, 6))
    ax2 = ax.twinx()
    lat_bar_width = 0.4  # Width of each bar
    comm_prot_colors = {"tcp": "blue", "rdma": "green", "orig": "red"}
    offsets = {x: -lat_bar_width/2, y: lat_bar_width/2}
    lat_bar_colors = {"gwait_acq": "gray", "lwait_acq": "gold", "gwait_rel": "blue", "lwait_rel": "green", "lock_hold": "red"}
    nthreads_markers = {1: "o", 16: "x", 32: "^", 8: "D", 16: "P"}
    # fig_bar, ax_bar = plt.subplots(figsize=(10, 6))
    # fig_fair, ax_fair = plt.subplots(figsize=(10, 6))
    position = 0
    x_positions = []
    x_labels = []
    legend_lat = {}
    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for comm_prot in comm_prots:
            color = comm_prot_colors.get(comm_prot, "gray")
            for remote_lock in DATA[comm_prot]:
                DE2N = DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"]
                DCL = DATA[comm_prot][remote_lock]["client"]["cum"]["lat"]
                DCM2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"]
                if impl in DE2N.keys():
                    for nclients in DE2N[impl]:
                        if nclients == 1:
                            ###################################### MEM 2NODES ################################################
                            if impl in DCM2N.keys():
                                for nthreads, values in DCM2N[impl][nclients].items():
                                    if nthreads == 32:
                                        duration = values["total_duration"].max()
                                        max_array_size = values["array_size"].max()
                                        FACTOR = 1
                                        values = values[values["array_size"] == max_array_size]
                                        lock_acquires = values["lock_acquires"]
                                        gwait_acq = ((values["gwait_acq"] / lock_acquires).mean() / FACTOR) if ("gwait_acq" in values) else 0
                                        ax.bar(position+offsets[comm_prot], gwait_acq, width=lat_bar_width,
                                            color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_acq = 0 if comm_prot != "orig" else (values["wait_acq"] / lock_acquires).mean() / FACTOR
                                        ax.bar(position+offsets[comm_prot], lwait_acq, width=lat_bar_width, bottom=gwait_acq,
                                            color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        gwait_rel = (values["gwait_rel"] / lock_acquires).mean() / FACTOR if ("gwait_rel" in values) else 0
                                        ax.bar(position+offsets[comm_prot], gwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq,
                                            color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lwait_rel = 0 if comm_prot != "orig" else (values["wait_rel"] / lock_acquires).mean() / FACTOR
                                        ax.bar(position+offsets[comm_prot], lwait_rel, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                                            color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        lock_hold = (values["lock_hold"] / lock_acquires).mean() / FACTOR
                                        ax.bar(position+offsets[comm_prot], lock_hold, width=lat_bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                                            color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                                            edgecolor="black"
                                        )
                                        # ax2.scatter([position+offsets[comm_prot]], [lock_acquires.mean() / duration], marker="x", color="black")
                                        ax2.scatter([position+offsets[comm_prot]], [values["loop_in_cs"].mean() / duration], marker="x", color="black")
                            ######################################################################################

    ax.set_xticks(x_positions)
    ax.set_xticklabels(x_labels, rotation=45, ha='right')
    ax.set_xlabel("Implementation")
    ax.set_ylabel("Mean Latencies (ms)")
    ax.set_yscale("log")
    ax2.set_ylabel("TP (ops/ms)")
    ax.set_title(f"Memory Latencies 2N 1C 32T {x} vs. {y}")
    ax.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    ax.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="Latency", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/avglat_mem2n_1C_32T_{x}v{y}.png"
    fig.savefig(output_path, dpi=300, bbox_inches='tight')
    
def plots_1v2N(DATA, comm_prots=["orig"]):
    fig_emptycs1v2n, ax_emptycs1v2n = plt.subplots(figsize=(10, 6))
    ax_emptycs1v2n_fair = ax_emptycs1v2n.twinx()
    fig_tpmem1v2n, ax_tpmem1v2n = plt.subplots(figsize=(10, 6))
    ax_tpmem1v2n_fair = ax_tpmem1v2n.twinx()
    fig_latmem1v2n, ax_latmem1v2n = plt.subplots(figsize=(10, 6))
    fig_latmem1v2n_hold, ax_latmem1v2n_hold = plt.subplots(figsize=(10, 6))
    ax_latmem1v2n2 = ax_latmem1v2n.twinx()
    ax_latmem1v2n_hold2 = ax_latmem1v2n_hold.twinx()

    bar_width = 0.4
    offset = bar_width / 2
    position = 0
    x_positions = []
    x_labels = []
    legend_empty = {}
    legend_lat = {}

    offsets = {"mem1n": -offset, "mem2n": offset, "empty_cs1n": -offset, "empty_cs2n": offset}

    empty_benches = ["empty_cs1n", "empty_cs2n"]
    mem_benches = ["mem1n", "mem2n"]

    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        for comm_prot in comm_prots:
            ################################ EMPTY CS 1N vs 2N ############################################
            for empty_bench in empty_benches:
                DE1N = DATA[comm_prot]["none"]["client"]["cum"][empty_bench]
                for nthreads, values in DE1N[impl][1].items():
                    if nthreads == 16:
                        duration = values["total_duration"][0] * 1e6
                        ax_emptycs1v2n.boxplot(
                            values["lock_acquires"] / duration,
                            positions=[position+offsets[empty_bench]],
                            widths=bar_width, 
                            patch_artist=True,
                            boxprops=dict(facecolor=node_colors.get(empty_bench, "gray")),
                        )
                        ax_emptycs1v2n_fair.scatter([position+offsets[empty_bench]], jain_fairness_index(values["lock_acquires"]),
                                                    color="gold", marker="^", edgecolor="black")

            ######################################################################################


            ################################ MEM_TP | MEM_LAT 1N vs 2N############################################
            for mem_bench in mem_benches:
                DM1N = DATA[comm_prot]["none"]["client"][mem_bench]["cum"]
                for nthreads, values in DM1N[impl][1].items():
                    if nthreads == 16:
                        max_array_size = values["array_size"].max()
                        FACTOR = 1
                        values = values[values["array_size"] == max_array_size]
                        lock_acquires = values["lock_acquires"]
                        gwait_acq = ((values["gwait_acq"] / lock_acquires).mean() / FACTOR) if ("gwait_acq" in values) else 0
                        ax_latmem1v2n.bar(position+offsets[mem_bench], gwait_acq, width=bar_width,
                            color=lat_bar_colors["gwait_acq"], label=f"{comm_prot} (gwait_acq)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lwait_acq = (values["wait_acq"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n.bar(position+offsets[mem_bench], lwait_acq, width=bar_width, bottom=gwait_acq,
                            color=lat_bar_colors["lwait_acq"], label=f"{comm_prot} (lwait_acq)" if position == 1 else "",
                            edgecolor="black"
                        )
                        gwait_rel = (values["gwait_rel"] / lock_acquires).mean() / FACTOR if ("gwait_rel" in values) else 0
                        ax_latmem1v2n.bar(position+offsets[mem_bench], gwait_rel, width=bar_width, bottom=gwait_acq+lwait_acq,
                            color=lat_bar_colors["gwait_rel"], label=f"{comm_prot} (gwait_rel)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lwait_rel = (values["wait_rel"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n.bar(position+offsets[mem_bench], lwait_rel, width=bar_width, bottom=gwait_acq+lwait_acq+gwait_rel,
                            color=lat_bar_colors["lwait_rel"], label=f"{comm_prot} (lwait_rel)" if position == 1 else "",
                            edgecolor="black"
                        )
                        lock_hold = (values["lock_hold"] / lock_acquires).mean() / FACTOR
                        ax_latmem1v2n_hold.bar(position+offsets[mem_bench], lock_hold, width=bar_width,
                            color=node_colors[mem_bench], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                            edgecolor="black"
                        )
                        # ax_latmem1v2n.bar(position+mem_offset[mem_bench], lock_hold, width=bar_width, bottom=gwait_acq+lwait_acq+gwait_rel+lwait_rel,
                        #     color=lat_bar_colors["lock_hold"], label=f"{comm_prot} (lock_hold)" if position == 1 else "",
                        #     edgecolor="black"
                        # )
                        ax_latmem1v2n2.scatter([position+offsets[mem_bench]], [lock_acquires.mean() / duration], marker="x", color="black")
                        ax_latmem1v2n_hold2.scatter([position+offsets[mem_bench]], [lock_acquires.mean() / duration], marker="x", color="gold")

                        

                        duration = values["total_duration"].max() * 1e6
                        ax_tpmem1v2n.boxplot(
                            values["lock_acquires"] / duration,
                            positions=[position+offsets[mem_bench]],
                            widths=0.3,  # Make boxplots narrower
                            patch_artist=True,  # Enable box color customization
                            boxprops=dict(facecolor=node_colors.get(mem_bench, "gray")),
                        )
                        ax_tpmem1v2n_fair.scatter([position+offsets[mem_bench]], jain_fairness_index(lock_acquires),
                                                    marker="^", color="gold", edgecolor="black")
            ######################################################################################


    legend_empty[0] = mlines.Line2D([], [],color="gray",marker="o",
        markersize=8, linestyle="None", label=f"1 Node")
    legend_empty[1] = mlines.Line2D([], [],color="black",marker="o",
        markersize=8, linestyle="None", label=f"2 Nodes")

    ax_emptycs1v2n.set_xticks(x_positions)
    ax_emptycs1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_emptycs1v2n.set_xlabel("Implementation")
    ax_emptycs1v2n.set_ylabel("Throughput (lock acquisitions/us)")
    ax_emptycs1v2n_fair.set_ylabel("Jain's Fairness Index")
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
    ax_latmem1v2n.set_ylabel("Mean Latencies (ms)")
    ax_latmem1v2n2.set_ylabel("TP (ops/ms)")
    ax_latmem1v2n.set_yscale('log')
    ax_latmem1v2n.set_title("Latency Wait+TP Memory 1v2N")
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
    output_path = file_dir+f"/plots/avglatwait_mem1v2n_1C_16T.png"
    fig_latmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_latmem1v2n_hold.set_xticks(x_positions)
    ax_latmem1v2n_hold.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_latmem1v2n_hold.set_xlabel("Implementation")
    ax_latmem1v2n_hold.set_ylabel("Mean Latencies (ms)")
    ax_latmem1v2n_hold2.set_ylabel("TP (ops/ms)")
    # ax_latmem1v2n_hold.set_yscale('log')
    ax_latmem1v2n_hold.set_title("Latency Lock Hold+TP Memory 1v2N")
    ax_latmem1v2n_hold.grid(linestyle="--", alpha=0.7)
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
    output_path = file_dir+f"/plots/avglathold_mem1v2n_1C_16T.png"
    fig_latmem1v2n_hold.savefig(output_path, dpi=300, bbox_inches='tight')

    
    


    ax_tpmem1v2n.set_xticks(x_positions)
    ax_tpmem1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_tpmem1v2n.set_xlabel("Implementation")
    ax_tpmem1v2n.set_ylabel("Throughput (lock acquisitions/us)")
    ax_tpmem1v2n_fair.set_ylabel("Jain's Fairness Index")
    ax_tpmem1v2n.set_yscale('log')
    ax_tpmem1v2n.set_title("TP Memory 1v2N 1 Client 16 Threads")
    ax_tpmem1v2n.grid(linestyle="--", alpha=0.7)
    ax_tpmem1v2n.legend(legend_empty.values(), [entry.get_label() for entry in legend_empty.values()],
              title="Node Pinning", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_mem1v2n_1C_16T.png"
    fig_tpmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')

def add_lat(ax, ax2, values, position, comm_prot, bar_width, inc):
    duration = values["total_duration"].max() * DURATION_FACTOR
    lock_acquires = values["lock_acquires"]
    cum = 0

    for measurement in inc:
        mean = (values[measurement] / lock_acquires).mean() / LAT_FACTOR if (measurement in values) else 0
        median = (values[measurement] / lock_acquires).median() / LAT_FACTOR if (measurement in values) else 0
        ax.bar(position, mean, width=bar_width, bottom=cum,
            color=lat_bar_colors[measurement], label=f"{comm_prot} ({measurement})" if position == 1 else "",
            edgecolor="black"
        )
        ax.scatter([position], [cum + median], marker="x", color=median_colors[measurement])
        cum += mean
        
    ax2.scatter([position], [lock_acquires.mean() / duration], marker="x", color="black")

def add_box(ax1, ax2, position, values):
    duration = values["total_duration"].max() * DURATION_FACTOR
    lock_acquires = values["lock_acquires"]
    ax1.boxplot(
        values["lock_acquires"] / duration,
        positions=[position],
        widths=0.3,
        patch_artist=True,
    )
    ax2.scatter([position], jain_fairness_index(lock_acquires),
                                marker="^", color="gold", edgecolor="black")
    
def save_figs(ax1, ax2, ax3, ax4, fig1, fig2,
              x_positions, x_labels, 
              comm_prot="rdma", remote_lock="spinlock", bench="",
              client_mode ="MC", nthreads=16, log=1,
              ):
    legend_lat = {}
    ax1.set_xticks(x_positions)
    ax1.set_xticklabels(x_labels, rotation=45, ha='right')
    ax1.set_xlabel("Implementation")
    ax1.set_ylabel("Mean Latencies (ms)")
    ax2.set_ylabel("TP (ops/ms)")
    if log:
        ax1.set_yscale('log')
    ax1.set_title(f"Latency Wait+TP {bench} Multiple Clients {comm_prot} {remote_lock}")
    ax1.grid(linestyle="--", alpha=0.7)
    for metric,color in lat_bar_colors.items():
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="o",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    for metric,color in median_colors.items():
        metric = "median " + metric
        legend_lat[metric] = mlines.Line2D(
                                [], [],
                                color=color,
                                marker="x",
                                markersize=8,
                                linestyle="None",
                                label=metric)
    legend_lat["Avg TP"] = mlines.Line2D(
                            [], [],
                            color="black",
                            marker="x",
                            markersize=8,
                            linestyle="None",
                            label="Avg TP")
    ax1.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
              title="", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/cumlat_{comm_prot}_{remote_lock}_{bench}_{client_mode}_{nthreads}T_.png"
    fig1.savefig(output_path, dpi=300, bbox_inches='tight')

    if ax3 is not None and ax4 is not None:
        ax3.set_xticks(x_positions)
        ax3.set_xticklabels(x_labels, rotation=45, ha='right')
        ax3.set_xlabel("Implementation")
        ax3.set_ylabel("TP (lock acquistions/ms)")
        ax4.set_ylabel("Jain's Fairness Index")
        # ax3.set_yscale('log')
        ax3.set_title(f"TP+Fairness {bench} Multiple Clients {comm_prot} {remote_lock}")
        ax3.grid(linestyle="--", alpha=0.7)
        output_path = file_dir+f"/plots/tpfair_{comm_prot}_{remote_lock}_{bench}_{client_mode}_{nthreads}T_.png"
        fig2.savefig(output_path, dpi=300, bbox_inches='tight')
    
def make_ax_fig(x, y):
    fig, ax = plt.subplots(figsize=(x,y))
    ax2 = ax.twinx()
    return fig, ax, ax2

def plot_MC(DATA, comm_prot="rdma", remote_lock="spinlock", num_clients=3, nt=16):
    fig_empty_mc, ax_empty_mc, ax_empty_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_empty_mc_fair, ax_empty_mc_fair, ax_empty_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc, ax_mem_mc, ax_mem_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_mem_mc_fair, ax_mem_mc_fair, ax_mem_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc_h, ax_mem_mc_h, ax_mem_mc2_h = make_ax_fig(FIG_X, FIG_Y)
    
    maxs = [0] * num_clients
    maxsm = [0] * num_clients
    bar_width = BAR_WIDTH 
    offsets = [-bar_width, 0, bar_width]
    position = 0
    x_positions = []
    x_labels = []
    lat_inc = ["gwait_acq", "lwait_acq"]
    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        DE = DATA[comm_prot][remote_lock]["client"]["empty_cs2n"]["cum"][impl]
        DM2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"][impl]
        for nclients in DE:
            for nthreads, values in DE[nclients].items():
                if nthreads == nt:
                    duration = values["total_duration"].max() * DURATION_FACTOR 
                    add_lat(ax_empty_mc, ax_empty_mc2, values, position+offsets[nclients-1], comm_prot, bar_width, lat_inc)
                    add_box(ax_empty_mc_fair, ax_empty_mc_fair2, position+offsets[nclients-1], values)
                    la = values["lock_acquires"].mean() / duration
                    maxs[nclients-1] = la if maxs[nclients-1] < la else maxs[nclients-1]

            for nthreads, values in DM2N[nclients].items():
                if nthreads == nt:
                    values = values[values["array_size"] == values["array_size"].max()]
                    duration = values["total_duration"].max() * DURATION_FACTOR 
                    add_lat(ax_mem_mc, ax_mem_mc2, values, position+offsets[nclients-1], comm_prot, bar_width, lat_inc)
                    add_box(ax_mem_mc_fair, ax_mem_mc_fair2, position+offsets[nclients-1], values)
                    la = values["lock_acquires"].mean() / duration
                    maxsm[nclients-1] = la if maxsm[nclients-1] < la else maxsm[nclients-1]

                    add_lat(ax_mem_mc_h, ax_mem_mc2_h, values, position+offsets[nclients-1], comm_prot, bar_width, ["lock_hold"])

    for max in maxs:
        ax_empty_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
    for max in maxsm:
        ax_mem_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
        ax_mem_mc2_h.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')


                        
    save_figs(ax_empty_mc, ax_empty_mc2, ax_empty_mc_fair, ax_empty_mc_fair2, fig_empty_mc, fig_empty_mc_fair,
              x_positions, x_labels, comm_prot, remote_lock, "empty_cs2n", client_mode="MC",
              nthreads=nt)
    save_figs(ax_mem_mc, ax_mem_mc2, ax_mem_mc_fair, ax_mem_mc_fair2, fig_mem_mc, fig_mem_mc_fair,
              x_positions, x_labels, comm_prot, remote_lock, "mem2n", client_mode="MC",
              nthreads=nt)
    save_figs(ax_mem_mc_h, ax_mem_mc2_h, None, None, fig_mem_mc_h, fig_mem_mc_h,
              x_positions, x_labels, comm_prot, remote_lock, "mem2n_lh", client_mode="MC",
              nthreads=nt)

    

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
            for stat in STATS:
                DATA[comm_prot] = {remote_lock: {"client": {stat: {}}, "server": {stat: {}}}}
                to_pd(DATA[comm_prot][remote_lock]["client"][stat], RES_DIRS[comm_prot][remote_lock]["client"][stat], CLIENT_COLS)
                to_pd(DATA[comm_prot][remote_lock]["server"][stat], RES_DIRS[comm_prot][remote_lock]["server"][stat], SERVER_COLS)

    DATA["orig"] = {"none": {"client": {stat: {}}}}
    to_pd(DATA["orig"]["none"]["client"][stat], RES_DIRS["orig"][stat], CLIENT_RDMA_COLS)

def prep_res_dirs(RES_DIRS):
    for comm_prot in COMM_PROTOCOLS:
        RES_DIRS[comm_prot] = {}
        for remote_lock in REMOTE_LOCKS:
            RES_DIRS[comm_prot][remote_lock] = {"client" : {}, "server": {}}
            for stat in STATS:
                for mb in MICROBENCHES:
                    client_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/client/{stat}/*/{mb}/"
                    client_res_dir = glob.glob(client_res_dir)
                    server_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/server/{stat}/*/{mb}/"
                    server_res_dir = glob.glob(server_res_dir)

                    RES_DIRS[comm_prot][remote_lock]["client"][stat][mb] = client_res_dir
                    RES_DIRS[comm_prot][remote_lock]["server"][stat][mb] = server_res_dir

    RES_DIRS["orig"] = {}
    for mb in MICROBENCHES:
        for stat in STATS:
            orig_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/orig/{stat}/*/{mb}/"
            orig_res_dir = glob.glob(orig_res_dir)
            RES_DIRS["orig"][stat][mb] = orig_res_dir

        
file_dir = os.path.dirname(os.path.realpath(__file__))
        
RES_DIRS = {}
DATA = {}

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)
plots_SC(DATA)
plots_1v2N(DATA)
plot_XvY(DATA, "rdma", "tcp")
plot_XvY(DATA, "orig", "rdma")
plot_MC(DATA)