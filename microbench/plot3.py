import matplotlib.pyplot as plt
import matplotlib.lines as mlines            
from plot_common import *


def plot_tp_lat(DATA, comm_prot="rdma", opts=["."], mbs=["kvs"],
    lat_incs=[], tp_incs=[],
    cnMnNRs=[[1, 2], [4, 2]], threadNRs=[32], lockNRs=[1], log=[1],
    pinnings=[], mHos=[16], colocate=1, vs_colocate=False, impls=[],
    ):


    for mb in mbs:
        if not (DATA["tp"]["mb"] == mb).any():
            continue

        LatPlotsNR = len(lat_incs)
        tpPlotsNR = len(tp_incs)

        fig_lat, ax_lat, ax_lat2 = make_multiplots(lat_incs)
        fig_tp, ax_tp, ax_tp2 = make_multiplots(tp_incs)


        bw_cns = 0.9 / len(cnMnNRs)
        bw_mlocks = 0.9 /len(lockNRs)
        client_offsets = make_offset(cnMnNRs, bw_cns, True) 
        mlocks_offsets = make_offset(lockNRs, bw_mlocks)

        position = 0
        x_positions = []
        x_labels = []

        num_vlines = 0
        max_vlines = len(cnMnNRs) - 1
        comp_nodes = []

        if len(impls) == 0:
            impls = IMPL

        for numa in pinnings:
                for threadNR in threadNRs:
                    # for mnNR in mnNRs:
                    for mHo in mHos:
                        # for cnNR in cnNRs:
                        for cnNR, mnNR in cnMnNRs:
                            if not cnNR in comp_nodes:
                                comp_nodes.append(cnNR)
                            for opt in opts:

                                for impl in impls:
                                    if not (DATA["tp"]["impl"] == impl).any():
                                        continue

                                    position += 1
                                    x_positions.append(position)
                                    x_labels.append(impl+f"_{opt}")

                                    # mnNR = mnNR if mnNR < cnNR else cnNR
                                    lock_idx = 0
                                    for lockNR in lockNRs:
                                        filter_values = {
                                            "comm_prot": comm_prot,
                                            "mb": mb,
                                            "impl": impl,
                                            "numa": numa,
                                            "opt": opt,
                                            "cnNR": cnNR,
                                            "threadNR": threadNR,
                                            "mnNR": mnNR,
                                            "lockNR": lockNR,
                                            "maxHandover": mHo,
                                            "colocate": colocate,
                                        }
                                        query_str = " and ".join([f"{col} == @filter_values['{col}']" for col in filter_values])
                                        df_tp = DATA["tp"].query(query_str)
                                        df_lat = DATA["lat"].query(query_str)
                                        if df_tp.empty and df_lat.empty:
                                            continue


                                        for i,lat_inc in enumerate(lat_incs):
                                            add_lat(ax_lat[i], ax_lat2[i], df_lat, df_tp, position+mlocks_offsets[lockNR], 
                                                    comm_prot, bw_mlocks, lat_inc, mlocks_hatches, lockNR)
                                        for i,tp_inc in enumerate(tp_incs):
                                            add_box(ax_tp[i], ax_tp2[i], position+mlocks_offsets[lockNR],
                                                    df_tp, lock_idx, bw_mlocks, mlocks_hatches,
                                                    tp_inc=tp_inc, inc_fair=tp_inc == "tp" or tp_inc == "la")
                                        lock_idx += 1

                                position += 0.5
                            position += 0.5

                            if num_vlines < max_vlines:
                                for i in range(LatPlotsNR):
                                    ax_lat[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
                                for i in range(tpPlotsNR):
                                    ax_tp[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')

                                num_vlines += 1

                            if vs_colocate:
                                colocate -= 1

                        C_str = f"{cnNR}CNs"
                        comp_nodes.sort()
                        cn_hatch_categories = {1: "1 CN", 2: "2 CNs", 3: "3 CNs", 4: "4 CNs", 5: "5 CNs", 6: "6 CNs", 7: "7 CNs", 8: "8 CNs"}
                        mlocks_hatch_categories = {i: f"{i} Lock(s)" for i in range(1,17000)}


                        for i in range(LatPlotsNR):
                            save_lat_figs(ax_lat[i], ax_lat2[i], fig_lat[i],
                                    x_positions, x_labels, comm_prot, 
                                    cnNR=cnNR, mnNR=mnNR, threadNR=threadNR, numa=numa, maxHo=mHo, mb=mb,
                                    clients=comp_nodes, include_metrics=lat_incs[i], 
                                    # hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                                    include_hatch_keys=lockNRs, log=log[i], latplot_idx=i)

                        
                        for i, tp_inc in enumerate(tp_incs):
                            y1, y2 = tp_axis_titles[tp_inc]

                            save_tp_figs(ax_tp[i], ax_tp2[i], fig_tp[i],
                                    x_positions, x_labels, comm_prot, clients=comp_nodes,
                                    cnNR=cnNR, mnNR=mnNR, threadNR=threadNR, numa=numa, maxHo=mHo, mb=mb,
                                    include_metrics=tp_inc,
                                    hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                                    include_hatch_keys=lockNRs, log=0, t=tp_inc, y1=y1, y2=y2)



def plot_ldist(DATA, opts=[], cnNRs=[], lockNRs=[], threadNRs=[], mnNRs=[1], pinnings=[1],mHos=[16],runs=[0],comm_prot="rdma"):
        for impl in IMPL:
            if not (DATA["ldist"]["impl"] == impl).any():
                continue
            for mb in MICROBENCHES:
                if not (DATA["ldist"]["mb"] == mb).any():
                    continue

                for numa in pinnings:
                    for opt in opts:
                        for cnNR in cnNRs:
                            for threadNR in threadNRs:
                                for mnNR in mnNRs:
                                        for lockNR in lockNRs:
                                            for mHo in mHos:
                                                for run in runs:
                                                    filter_values = {
                                                        "comm_prot": comm_prot,
                                                        "mb": mb,
                                                        "impl": impl,
                                                        "numa": numa,
                                                        "opt": opt,
                                                        "cnNR": cnNR,
                                                        "threadNR": threadNR,
                                                        "mnNR": mnNR,
                                                        "lockNR": lockNR,
                                                        "maxHandover": mHo,
                                                        "run": run,
                                                    }
                                                    query_str = " and ".join([f"{col} == @filter_values['{col}']" for col in filter_values])
                                                    DLDIST = DATA["ldist"].query(query_str)
                                                    DLDIST = DLDIST["dist"].to_numpy()

                                                    if DLDIST.size > 0:
                                                        # num_locks = mnNR*lockNR if mnNR <= cnNR else cnNR*lockNR
                                                        DLDIST = DLDIST[:lockNR]
                                                        plt.figure(figsize=(12, 6))
                                                        plt.bar(np.arange(len(DLDIST)), DLDIST, color="blue", alpha=0.6)
                                                        plt.xlabel("Lock Index")
                                                        plt.ylabel("Acquisition Count")
                                                        plt.title(f"Lock Acquisition Distribution | {impl}_{opt} | {lockNR} lockNR | {cnNR} CNs | {mnNR} MNs | {threadNR} Ts | {mb} MB | {numa} NUMA | {mHo} maxHo | {run} R")
                                                        output_path = file_dir+f"/results/plots/ldist/{impl}_{opt}_{mb}_{cnNR}CN_{threadNR}T_{lockNR}L_{mnNR}MN_{numa}NUMA_{mHo}maxHo_{run}R.png"
                                                        plt.savefig(output_path, dpi=300, bbox_inches="tight")
                                                    
    
def plot_cvnc(DATA, comm_prot, mb, impl, numa, opt, cnNR, threadNR, mnNR, lockNR, mHo):
    filter_values = {
        "comm_prot": comm_prot,
        "mb": mb,
        "impl": impl,
        "numa": numa,
        "opt": opt,
        "cnNR": cnNR,
        "threadNR": threadNR,
        "mnNR": mnNR,
        "lockNR": lockNR,
        "maxHandover": mHo,
        "colocate": 1,
    }
    query_str = " and ".join([f"{col} == @filter_values['{col}']" for col in filter_values])
    df_tp = DATA["tp"].query(query_str)
    df_lat = DATA["lat"].query(query_str)

    grouped_tp = df_tp.groupby("nodeID")
    sum_tp = grouped_tp["tp"].sum()
    sum_colocated = grouped_tp["colocated_access"].sum()
    sum_non_colocated = grouped_tp["non_colocated_access"].sum()
    grouped_ratio = sum_colocated / sum_non_colocated

    lat_median = df_lat[df_lat["perc"] == 0.5].set_index("nodeID")["end_to_end"]
    lat_tail = df_lat[df_lat["perc"] == 0.99].set_index("nodeID")["end_to_end"]

    df_plot = pd.DataFrame({
        "tp_sum": sum_tp,
        "access_ratio": grouped_ratio,
        "median_lat": lat_median * 10,
        "tail_lat": lat_tail * 10,
    }).dropna()

    df_plot = df_plot.sort_values("access_ratio")


    plt.figure(figsize=(8, 5))
    # plt.plot(grouped_ratio, sum_tp, marker='o', linestyle='-', color='steelblue')
    plt.scatter(df_plot["access_ratio"], df_plot["tp_sum"], color="steelblue")
    for i, row in df_plot.iterrows():
        annotation = f"NodeID: {row.name}\nmed: {row['median_lat']:.0f}us\np99: {row['tail_lat']:.0f}us"
        if i % 2:
            i = -i
        plt.annotate(annotation,
                    (row["access_ratio"], row["tp_sum"]),
                    textcoords="offset points",
                    xytext=(10, i*6),
                    # ha='center',
                    fontsize=8,
                    color="black")

    plt.xlabel("Per Node [Colocated / Non-Colocated] Access Ratio")
    plt.ylabel("Total TP per Node")
    plt.title("TP vs. Col/Non-Col Access Ratio by Nodes and End To End Latencies")
    plt.grid(True)
    plt.tight_layout()
    output_path = file_dir+f"/results/plots/tp/cvncRatios_{impl}_{opt}_{mb}_{cnNR}CN_{threadNR}T_{lockNR}L_{mnNR}MN_{numa}NUMA_{mHo}maxHo_.png"
    plt.savefig(output_path, dpi=300, bbox_inches="tight")


RES_DIRS = {}
DATA = {}


read_data(DATA, RES_DIRS, inc_ldist=True)

plot_cvnc(DATA, "rdma", "kvs", "shermanLock", 1, ".", 4, 16, 4, 1024, 16)

# plot_tp_lat(
#                 DATA, 
#                 impls=[],
#                 mbs=["kvs"],
#                 # opts=['.', 'Ho', 'Hod', 'Bw', 'HodOcmBw'],
#                 opts=["."],
#                 # cnMnNRs=[[1,1], [2,1]],
#                 cnMnNRs=[[4,4]],
#                 lockNRs=[1024],
#                 threadNRs=[16],
#                 mHos=[16],
#                 pinnings=[1],
#                 lat_incs = [["lwait_acq"], ["gwait_acq", "gwait_rel"], ["data_read", "data_write"]],
#                 tp_incs=["la", "tp", "glock_tries", "handovers", "handovers_data", "cache_misses"],
#                 log=[1,1,0],
#                 colocate=1,
#                 # vs_colocate=True,
#                 )

pass
plot_ldist(DATA,
            opts=['.'],
            cnNRs=[4],
            lockNRs=[1024],
            threadNRs=[16],
            mnNRs=[4],
            mHos=[16],
            pinnings=[1],
           )
pass