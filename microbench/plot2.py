import matplotlib.pyplot as plt
import matplotlib.lines as mlines            
from plot_common import *

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
    for category, impls in IMPL_DICT.items():
        for impl in impls:
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
                        
        position += 1

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
    
def plots_1v2N(DATA, comm_prot="orig", rlock="none", nclients=1, n_threads=16):
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
            ################################ EMPTY CS 1N vs 2N ############################################
        for empty_bench in empty_benches:
            if not impl in DATA[comm_prot][rlock]["client"]["cum"][empty_bench]:
                continue

            DE1N = DATA[comm_prot][rlock]["client"]["cum"][empty_bench]
            for nthreads, values in DE1N[impl][nclients].items():
                if values.empty:
                    continue
                if nthreads == n_threads:
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
            DM1N = DATA[comm_prot][rlock]["client"]["cum"][mem_bench]
            for nthreads, values in DM1N[impl][nclients].items():
                if values.empty:
                    continue
                if nthreads == n_threads:
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
    ax_emptycs1v2n.set_title(f"TP Empty CS 1v2N {comm_prot} {rlock} {nclients} Cs {n_threads} Ts") 
    ax_emptycs1v2n.grid(linestyle="--", alpha=0.7)
    ax_emptycs1v2n.legend(legend_empty.values(), [entry.get_label() for entry in legend_empty.values()],
              title="Node Pinning", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_empty_cs1v2n_{comm_prot}_{rlock}_{nclients}C_{n_threads}T.png"
    fig_emptycs1v2n.savefig(output_path, dpi=300, bbox_inches='tight')




    ax_latmem1v2n.set_xticks(x_positions)
    ax_latmem1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_latmem1v2n.set_xlabel("Implementation")
    ax_latmem1v2n.set_ylabel("Mean Latencies (ms)")
    ax_latmem1v2n2.set_ylabel("TP (ops/ms)")
    ax_latmem1v2n.set_yscale('log')
    ax_latmem1v2n.set_title(f"Latency Wait+TP Memory 1v2N {comm_prot} {rlock} {nclients} Cs {n_threads} Ts")
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
    output_path = file_dir+f"/plots/avglatwait_mem1v2n_{comm_prot}_{rlock}_{nclients}C_{n_threads}T.png"
    fig_latmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')

    ax_latmem1v2n_hold.set_xticks(x_positions)
    ax_latmem1v2n_hold.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_latmem1v2n_hold.set_xlabel("Implementation")
    ax_latmem1v2n_hold.set_ylabel("Mean Latencies (ms)")
    ax_latmem1v2n_hold2.set_ylabel("TP (ops/ms)")
    # ax_latmem1v2n_hold.set_yscale('log')
    ax_latmem1v2n_hold.set_title(f"Latency Lock Hold+TP Memory 1v2N {comm_prot} {rlock} {nclients} Cs {n_threads} Ts")
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
    output_path = file_dir+f"/plots/avglathold_mem1v2n_{comm_prot}_{rlock}_{nclients}C_{n_threads}T.png"
    fig_latmem1v2n_hold.savefig(output_path, dpi=300, bbox_inches='tight')

    
    


    ax_tpmem1v2n.set_xticks(x_positions)
    ax_tpmem1v2n.set_xticklabels(x_labels, rotation=45, ha='right')
    ax_tpmem1v2n.set_xlabel("Implementation")
    ax_tpmem1v2n.set_ylabel("Throughput (lock acquisitions/us)")
    ax_tpmem1v2n_fair.set_ylabel("Jain's Fairness Index")
    ax_tpmem1v2n.set_yscale('log')
    ax_tpmem1v2n.set_title(f"TP Memory 1v2N {comm_prot} {rlock} {nclients} Cs {n_threads} Ts")
    ax_tpmem1v2n.grid(linestyle="--", alpha=0.7)
    ax_tpmem1v2n.legend(legend_empty.values(), [entry.get_label() for entry in legend_empty.values()],
              title="Node Pinning", loc="upper left", bbox_to_anchor=(1.05, 1))
    output_path = file_dir+f"/plots/tp_mem1v2n_{comm_prot}_{rlock}_{nclients}C_{nthreads}T.png"
    fig_tpmem1v2n.savefig(output_path, dpi=300, bbox_inches='tight')

def plot_MC(DATA, comm_prot="rdma", remote_lock="spinlock", num_clients=3, nt=16):
    fig_empty_mc, ax_empty_mc, ax_empty_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_empty_mc_fair, ax_empty_mc_fair, ax_empty_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc, ax_mem_mc, ax_mem_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_mem_mc_fair, ax_mem_mc_fair, ax_mem_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc_h, ax_mem_mc_h, ax_mem_mc2_h = make_ax_fig(FIG_X, FIG_Y)
    
    maxs = {i: 0 for i in range(16)}
    maxsm = {i: 0 for i in range(16)}
    bar_width = 0.3 
    offsets = {1: -bar_width, 2: 0, 4: bar_width}
    position = 0
    x_positions = []
    x_labels = []
    lat_inc = ["gwait_acq", "lwait_acq"]
    clients = []
    add_vline([ax_empty_mc, ax_empty_mc_fair, ax_mem_mc, ax_mem_mc_h], 0, bar_width)
    for impl in IMPL:
        position += 1
        x_positions.append(position)
        x_labels.append(impl)
        if not impl in DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"].keys():
            continue

        add_vline([ax_empty_mc, ax_empty_mc_fair, ax_mem_mc, ax_mem_mc_h], position, bar_width)
        DE = DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"][impl]
        DM2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"][impl]
        for nclients in DE:
            if not nclients in clients:
                clients.append(nclients)

            for nthreads, values in DE[nclients].items():
                if nthreads == nt:
                    duration = values["total_duration"].max() * DURATION_FACTOR 
                    add_lat(ax_empty_mc, ax_empty_mc2, values, position+offsets[nclients], comm_prot, bar_width, lat_inc)
                    add_box(ax_empty_mc_fair, ax_empty_mc_fair2, position+offsets[nclients], values)
                    la = values["lock_acquires"].mean() / duration
                    maxs[nclients] = la if maxs[nclients] < la else maxs[nclients]

            for nthreads, values in DM2N[nclients].items():
                if nthreads == nt:
                    values = values[values["array_size"] == values["array_size"].max()]
                    duration = values["total_duration"].max() * DURATION_FACTOR 
                    add_lat(ax_mem_mc, ax_mem_mc2, values, position+offsets[nclients], comm_prot, bar_width, lat_inc)
                    add_box(ax_mem_mc_fair, ax_mem_mc_fair2, position+offsets[nclients], values)
                    la = values["lock_acquires"].mean() / duration
                    maxsm[nclients] = la if maxsm[nclients] < la else maxsm[nclients]

                    add_lat(ax_mem_mc_h, ax_mem_mc2_h, values, position+offsets[nclients], comm_prot, bar_width, ["lock_hold"])

    for _,max in maxs.items():
        if max > 0:
            ax_empty_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
    for _,max in maxsm.items():
        if max > 0:
            ax_mem_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
            ax_mem_mc2_h.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')

    C_str = f"{num_clients}C"
    clients.sort()
    save_figs(ax_empty_mc, ax_empty_mc2, ax_empty_mc_fair, ax_empty_mc_fair2, fig_empty_mc, fig_empty_mc_fair,
              x_positions, x_labels, comm_prot, remote_lock, "empty_cs2n", client_mode=C_str, clients=clients,
              nthreads=nt)
    save_figs(ax_mem_mc, ax_mem_mc2, ax_mem_mc_fair, ax_mem_mc_fair2, fig_mem_mc, fig_mem_mc_fair,
              x_positions, x_labels, comm_prot, remote_lock, "mem2n", client_mode=C_str, clients=clients,
              nthreads=nt)
    save_figs(ax_mem_mc_h, ax_mem_mc2_h, None, None, fig_mem_mc_h, fig_mem_mc_h,
              x_positions, x_labels, comm_prot, remote_lock, "mem2n_lh", client_mode=C_str, clients=clients,
              nthreads=nt)

def plot_MC_rlocks(DATA, comm_prot="rdma", remote_locks=["spinlock"], lat_inc=[], num_clients=[1,4], nt=16, num_locks=[1], log=1):
    fig_empty_mc, ax_empty_mc, ax_empty_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_empty_mc_fair, ax_empty_mc_fair, ax_empty_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc, ax_mem_mc, ax_mem_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_mem_mc_fair, ax_mem_mc_fair, ax_mem_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_mem_mc_h, ax_mem_mc_h, ax_mem_mc2_h = make_ax_fig(FIG_X, FIG_Y)

    fig_ml, ax_ml, ax_ml2 = make_ax_fig(FIG_X, FIG_Y)
    fig_ml_fair, ax_ml_fair, ax_ml_fair2 = make_ax_fig(FIG_X, FIG_Y)

    bw_clients = 0.9 / len(num_clients)
    bw_mlocks = 0.9 /len(num_locks)
    client_offsets = make_offset(num_clients, bw_clients) 
    mlocks_offsets = make_offset(num_locks, bw_mlocks)
    
    maxs = {i: 0 for i in range(nt)}
    maxsm = {i: 0 for i in range(nt)}

    position = 0
    x_positions = []
    x_labels = []
    # lat_inc = ["gwait_acq", "data_read", "data_write", "gwait_rel"]
    # lat_inc = ["data_read", "data_write"]
    clients = []
    for nclients in num_clients:
        for remote_lock in remote_locks:
            for impl in IMPL:
                if not impl in DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"].keys() and \
                not impl in DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"].keys() and \
                not impl in DATA[comm_prot][remote_lock]["client"]["cum"]["mlocks2n"].keys():
                    continue

                position += 1.5
                x_positions.append(position)
                x_labels.append(impl+f"_{remote_lock}")

                DE = {}
                DM2N = {}
                DML2N = {}
                ml_ax_idx = 0
                if not nclients in clients:
                    clients.append(nclients)

                if impl in DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"].keys() and \
                not DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"][impl] == {}:
                    DE = DATA[comm_prot][remote_lock]["client"]["cum"]["empty_cs2n"][impl]
                    for nthreads, values in DE[nclients].items():
                        values["lwait_acq"] = values["lwait_acq"] - values["gwait_acq"]
                        values["lwait_rel"] = values["lwait_acq"] - values["gwait_rel"]
                        values["gwait_acq"] = values["gwait_acq"] - values["data_read"]
                        values["gwait_rel"] = values["gwait_rel"] - values["data_write"]
                        if nthreads == nt:
                            duration = values["total_duration"].max() * DURATION_FACTOR 
                            add_lat(ax_empty_mc, ax_empty_mc2, values, position+client_offsets[nclients], 
                                    comm_prot, bw_clients, lat_inc, client_hatches, nclients)
                            add_box(ax_empty_mc_fair, ax_empty_mc_fair2, position, values, nclients)
                            la = values["lock_acquires"].mean() / duration
                            # maxs[nclients] = la if maxs[nclients] < la else maxs[nclients]

                if impl in DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"].keys() and \
                not DATA[comm_prot][remote_lock]["client"]["cum"]["mlocks2n"][impl] == {}:

                    DM2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mem2n"][impl]
                    for nthreads, values in DM2N[nclients].items():
                        if nthreads == nt:
                            values = values[values["array_size"] == values["array_size"].max()]
                            duration = values["total_duration"].max() * DURATION_FACTOR 
                            add_lat(ax_mem_mc, ax_mem_mc2, values, position+client_offsets[nclients],
                                    comm_prot, bw_clients, lat_inc, client_hatches, nclients)
                            add_box(ax_mem_mc_fair, ax_mem_mc_fair2, position, values, nclients)
                            la = values["lock_acquires"].mean() / duration
                            # maxsm[nclients] = la if maxsm[nclients] < la else maxsm[remote_lock]

                            add_lat(ax_mem_mc_h, ax_mem_mc2_h, values, position+client_offsets[nclients], 
                                    comm_prot, bw_clients, ["lock_hold"], client_hatches, nclients)

                if impl in DATA[comm_prot][remote_lock]["client"]["cum"]["mlocks2n"].keys() and \
                not DATA[comm_prot][remote_lock]["client"]["cum"]["mlocks2n"][impl] == {}:

                    DML2N = DATA[comm_prot][remote_lock]["client"]["cum"]["mlocks2n"][impl]
                    for nthreads, values in DML2N[nclients].items():
                        if nthreads == nt:
                            for nlocks in num_locks:
                                duration = values["total_duration"].max() * DURATION_FACTOR 
                                v = values[values["nlocks"] == nlocks].copy()
                                v["lock_acquires"] = v["lock_acquires"].replace(0, 1)
                                v["lwait_acq"] = v["lwait_acq"].replace(0, duration)
                                v["gwait_acq"] = v["gwait_acq"].replace(0, duration)
                                # v.loc[v["lwait_acq"] == 0, "lwait_acq"] = duration
                                # v.loc[v["gwait_acq"] == 0, "gwait_acq"] = duration

                                v["lwait_acq"] = v["lwait_acq"] - v["gwait_acq"]
                                v["lwait_rel"] = v["lwait_rel"] - v["gwait_rel"]
                                v["gwait_acq"] = v["gwait_acq"] - v["data_read"]
                                v["gwait_rel"] = v["gwait_rel"] - v["data_write"]


                                add_lat(ax_ml, ax_ml2, v, position+mlocks_offsets[nlocks],
                                        comm_prot, bw_mlocks, lat_inc, mlocks_hatches, nlocks)
                                add_box(ax_ml_fair, ax_ml_fair2, position+mlocks_offsets[nlocks], v, nclients, bw_mlocks)

                ml_ax_idx += 1
            position += 1
        position += 1
    # for _,max in maxs.items():
    #     if max > 0:
    #         ax_empty_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
    # for _,max in maxsm.items():
    #     if max > 0:
    #         ax_mem_mc2.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')
    #         ax_mem_mc2_h.axhline(y=max, color='red', linestyle='--', label=f'Max value: {max:.2f}')

    C_str = f"{num_clients}C"
    clients.sort()
    client_hatch_categories = {1: "1 Client", 2: "2 Clients", 3: "3 Clients", 4: "4 Clients"}
    mlocks_hatch_categories = {i: f"{i} Lock(s)" for i in range(1,2049)}

    save_figs(ax_empty_mc, ax_empty_mc2, ax_empty_mc_fair, ax_empty_mc_fair2, fig_empty_mc, fig_empty_mc_fair,
              x_positions, x_labels, comm_prot, "RLOCK OPTS", "empty_cs2n", client_mode=C_str, clients=clients,
              nthreads=nt, include_metrics=lat_inc, hatches=client_hatches, hatch_categories=client_hatch_categories,
              include_hatch_keys=clients)

    save_figs(ax_mem_mc, ax_mem_mc2, ax_mem_mc_fair, ax_mem_mc_fair2, fig_mem_mc, fig_mem_mc_fair,
              x_positions, x_labels, comm_prot, "RLOCK OPTS", "mem2n", client_mode=C_str, clients=clients,
              nthreads=nt, include_metrics=lat_inc, hatches=client_hatches, hatch_categories=client_hatch_categories,
              include_hatch_keys=clients) 

    save_figs(ax_mem_mc_h, ax_mem_mc2_h, None, None, fig_mem_mc_h, fig_mem_mc_h,
              x_positions, x_labels, comm_prot, "RLOCK OPTS", "mem2n_lh", client_mode=C_str, clients=clients,
              nthreads=nt, include_metrics=["lock_hold"], hatches=client_hatches, hatch_categories=client_hatch_categories,
              include_hatch_keys=clients)

    save_figs(ax_ml, ax_ml2, ax_ml_fair, ax_ml_fair2, fig_ml, fig_ml_fair,
              x_positions, x_labels, comm_prot, "RLOCK OPTS", "mlocks2n", client_mode=C_str, clients=clients,
              nthreads=nt, include_metrics=lat_inc, hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
              include_hatch_keys=num_locks, log=log)







RES_DIRS = {}
DATA = {}

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)
# plots_SC(DATA)
# plots_1v2N(DATA, "rdma", "spinlock", 4, 8)
# plot_XvY(DATA, "rdma", "tcp")
# plot_XvY(DATA, "orig", "rdma")
plot_MC_rlocks(DATA, 
               remote_locks=["spinlock", "spinlock_bo"],
               lat_inc=["gwait_acq", "data_read", "data_write", "gwait_rel"],
            #    lat_inc=["gwait_acq", "gwait_rel"],
            #    lat_inc=["data_read", "data_write"],
            #    lat_inc=["lock_hold"],
               num_clients=[4], 
               num_locks=[512], 
               nt=16,
               log=1)