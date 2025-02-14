import matplotlib.pyplot as plt
import matplotlib.lines as mlines            
from plot_common import *


def plot_MC_rlocks(DATA, comm_prot="rdma", opts=["spinlock"], lat_ecs_inc=[], lat_ml_inc=[],
    mnNR=1, cnNRs=[1,4], threadNRs=32, lockNRs=[1], log=1
    ):
    fig_empty_mc, ax_empty_mc, ax_empty_mc2 = make_ax_fig(FIG_X, FIG_Y)
    fig_empty_mc_fair, ax_empty_mc_fair, ax_empty_mc_fair2 = make_ax_fig(FIG_X, FIG_Y)

    fig_ml, ax_ml, ax_ml2 = make_ax_fig(FIG_X, FIG_Y)
    fig_ml_fair, ax_ml_fair, ax_ml_fair2 = make_ax_fig(FIG_X, FIG_Y)

    bw_cns = 0.9 / len(cnNRs)
    bw_mlocks = 0.9 /len(lockNRs)
    client_offsets = make_offset(cnNRs, bw_cns) 
    mlocks_offsets = make_offset(lockNRs, bw_mlocks)
    

    position = 0
    x_positions = []
    x_labels = []

    num_vlines = 0
    max_vlines = len(cnNRs) - 1
    comp_nodes = []
    for cnNR in cnNRs:
        for opt in opts:
            for impl in IMPL:
                if not impl in DATA["tp"][comm_prot]["empty_cs"][opt].keys() and \
                not impl in DATA["tp"][comm_prot]["mlocks"][opt].keys():
                    continue

                position += 1
                x_positions.append(position)
                x_labels.append(impl+f"_{opt}")

                if not cnNR in comp_nodes:
                    comp_nodes.append(cnNR)

                if impl in DATA["lat"][comm_prot]["empty_cs"][opt].keys() and \
                not DATA["lat"][comm_prot]["empty_cs"][opt][impl] == {}:
                    DLAT = DATA["lat"][comm_prot]["empty_cs"][opt][impl][cnNR+mnNR]
                    for threadNR, values_lat in DLAT.items():
                        if threadNR == threadNRs:
                            values_tp = DATA["tp"][comm_prot]["empty_cs"][opt][impl][cnNR+mnNR][threadNR]
                            add_lat(ax_empty_mc, ax_empty_mc2, values_lat, values_tp, position+client_offsets[cnNR], 
                                    comm_prot, bw_cns, lat_ecs_inc, client_hatches, cnNR)
                            add_box(ax_empty_mc_fair, ax_empty_mc_fair2, position, values_tp, cnNR)

                if impl in DATA["lat"][comm_prot]["mlocks"][opt].keys() and \
                not DATA["lat"][comm_prot]["mlocks"][opt][impl] == {}:
                    DLAT = DATA["lat"][comm_prot]["mlocks"][opt][impl][cnNR+mnNR]
                    for threadNR, values_lat in DLAT.items():
                        if threadNR == threadNRs:
                            for lockNR in lockNRs:
                                values_tp = DATA["tp"][comm_prot]["mlocks"][opt][impl][cnNR+mnNR][threadNR]
                                add_lat(ax_ml, ax_ml2, values_lat, values_tp, position+mlocks_offsets[lockNR], 
                                        comm_prot, bw_mlocks, lat_ml_inc, mlocks_hatches, lockNR, lockNR)
                                add_box(ax_ml_fair, ax_ml_fair2, position+mlocks_offsets[lockNR],
                                        values_tp, lockNR, bw_mlocks, mlocks_hatches, lockNR)

            position += 1
        position += 1
        if num_vlines < max_vlines:
            ax_empty_mc.axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            ax_empty_mc_fair.axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            ax_ml.axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            ax_ml_fair.axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            num_vlines += 1

    C_str = f"{cnNR}C"
    comp_nodes.sort()
    cn_hatch_categories = {1: "1 CN", 2: "2 CNs", 3: "3 CNs", 4: "4 CNs"}
    mlocks_hatch_categories = {i: f"{i} Lock(s)" for i in range(1,2049)}

    save_figs(ax_empty_mc, ax_empty_mc2, ax_empty_mc_fair, ax_empty_mc_fair2, fig_empty_mc, fig_empty_mc_fair,
              x_positions, x_labels, comm_prot, "OPTS", "Empty CS", client_mode=C_str, clients=comp_nodes,
              nthreads=threadNRs, include_metrics=lat_ecs_inc, hatches=client_hatches, hatch_categories=cn_hatch_categories,
              include_hatch_keys=comp_nodes, log=log)

    save_figs(ax_ml, ax_ml2, ax_ml_fair, ax_ml_fair2, fig_ml, fig_ml_fair,
              x_positions, x_labels, comm_prot, "OPTS", "MLocks", client_mode=C_str, clients=comp_nodes,
              nthreads=threadNRs, include_metrics=lat_ml_inc, hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
              include_hatch_keys=lockNRs, log=log)


RES_DIRS = {}
DATA = {}

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)

plot_MC_rlocks(
                DATA, 
                opts=["HoOcmBw"],
                lat_ecs_inc = ["gwait_acq", "gwait_rel"],
                lat_ml_inc = ["lwait_acq", "gwait_acq", "gwait_rel"],
                # lat_ml_inc = ["data_read", "data_write", "lock_hold"],
                cnNRs=[4], 
                lockNRs=[256], 
                threadNRs=64,
                log=0
                )
pass