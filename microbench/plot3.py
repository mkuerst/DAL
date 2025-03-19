import matplotlib.pyplot as plt
import matplotlib.lines as mlines            
from plot_common import *


def plot_MC_rlocks(DATA, comm_prot="rdma", opts=["spinlock"],
    lat_ecs_inc=[], lat_ml_inc=[], tp_incs=[],
    mnNR=1, cnNRs=[1,4], threadNRs=32, lockNRs=[1], log=[1],
    ):

    emptyLatPlotsNR = len(lat_ecs_inc)
    mlLatPlotsNR = len(lat_ml_inc)
    tpPlotsNR = len(tp_incs)

    fig_empty_mc, ax_empty_mc, ax_empty_mc2 = make_multiplots(lat_ecs_inc)
    fig_empty_mc_fair, ax_empty_mc_fair, ax_empty_mc_fair2 = make_multiplots(tp_incs)

    fig_ml, ax_ml, ax_ml2 = make_multiplots(lat_ml_inc)
    fig_ml_fair, ax_ml_fair, ax_ml_fair2 = make_multiplots(tp_incs)

    fig_kvs, ax_kvs, ax_kvs2 = make_multiplots(lat_ml_inc)
    fig_kvs_fair, ax_kvs_fair, ax_kvs_fair2 = make_multiplots(tp_incs)

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
                not impl in DATA["tp"][comm_prot]["mlocks"][opt].keys() and \
                not impl in DATA["tp"][comm_prot]["kvs"][opt].keys():
                    continue

                position += 1
                x_positions.append(position)
                x_labels.append(impl+f"_{opt}")

                if not cnNR in comp_nodes:
                    comp_nodes.append(cnNR)

                if impl in DATA["lat"][comm_prot]["empty_cs"][opt].keys() and \
                not DATA["lat"][comm_prot]["empty_cs"][opt][impl] == {}:
                    DLAT = DATA["lat"][comm_prot]["empty_cs"][opt][impl][cnNR]
                    for threadNR, values_lat in DLAT.items():
                        if threadNR == threadNRs:
                            values_tp = DATA["tp"][comm_prot]["empty_cs"][opt][impl][cnNR][threadNR]
                            for i,lat_inc in enumerate(lat_ecs_inc):
                                add_lat(ax_empty_mc[i], ax_empty_mc2[i], values_lat, values_tp, position+client_offsets[cnNR], 
                                        comm_prot, bw_cns, lat_inc, client_hatches, cnNR)
                            for i,tp_inc in enumerate(tp_incs):
                                add_box(ax_empty_mc_fair[i], ax_empty_mc_fair2[i], position+mlocks_offsets[lockNR],
                                        values_tp, lockNR, bw_mlocks, mlocks_hatches, lockNR,
                                        tp_inc, inc_fair=tp_inc == "lock_acquires")

                if impl in DATA["lat"][comm_prot]["mlocks"][opt].keys() and \
                not DATA["lat"][comm_prot]["mlocks"][opt][impl] == {}:
                    DLAT = DATA["lat"][comm_prot]["mlocks"][opt][impl][cnNR]
                    for threadNR, values_lat in DLAT.items():
                        if threadNR == threadNRs:
                            for lockNR in lockNRs:
                                values_tp = DATA["tp"][comm_prot]["mlocks"][opt][impl][cnNR][threadNR]
                                for i,lat_inc in enumerate(lat_ml_inc):
                                    add_lat(ax_ml[i], ax_ml2[i], values_lat, values_tp, position+mlocks_offsets[lockNR], 
                                            comm_prot, bw_mlocks, lat_inc, mlocks_hatches, lockNR, lockNR)
                                for i,tp_inc in enumerate(tp_incs):
                                    add_box(ax_ml_fair[i], ax_ml_fair2[i], position+mlocks_offsets[lockNR],
                                            values_tp, lockNR, bw_mlocks, mlocks_hatches, lockNR,
                                            tp_inc, inc_fair=tp_inc == "lock_acquires")

                if impl in DATA["lat"][comm_prot]["kvs"][opt].keys() and \
                not DATA["lat"][comm_prot]["kvs"][opt][impl] == {}:
                    DLAT = DATA["lat"][comm_prot]["kvs"][opt][impl][cnNR]
                    for threadNR, values_lat in DLAT.items():
                        if threadNR == threadNRs:
                            for lockNR in lockNRs:
                                values_tp = DATA["tp"][comm_prot]["kvs"][opt][impl][cnNR][threadNR]
                                for i,lat_inc in enumerate(lat_ml_inc):
                                    add_lat(ax_kvs[i], ax_kvs2[i], values_lat, values_tp, position+mlocks_offsets[lockNR], 
                                            comm_prot, bw_mlocks, lat_inc, mlocks_hatches, lockNR, lockNR)
                                for i,tp_inc in enumerate(tp_incs):
                                    add_box(ax_kvs_fair[i], ax_kvs_fair2[i], position+mlocks_offsets[lockNR],
                                            values_tp, lockNR, bw_mlocks, mlocks_hatches, lockNR,
                                            tp_inc=tp_inc, inc_fair=tp_inc == "lock_acquires")

            position += 0.5 
        position += 0.5

        if num_vlines < max_vlines:
            for i in range(emptyLatPlotsNR):
                ax_empty_mc[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            for i in range(mlLatPlotsNR):
                ax_ml[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
                ax_kvs[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
            for i in range(tpPlotsNR):
                ax_empty_mc_fair[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
                ax_ml_fair[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')
                ax_kvs_fair[i].axvline(x=position-0.5, color='black', linewidth=4, linestyle='-')

            num_vlines += 1

    C_str = f"{cnNR}CNs"
    comp_nodes.sort()
    cn_hatch_categories = {1: "1 CN", 2: "2 CNs", 3: "3 CNs", 4: "4 CNs", 5: "5 CNs"}
    mlocks_hatch_categories = {i: f"{i} Lock(s)" for i in range(1,17000)}

    for i in range(emptyLatPlotsNR):
        save_lat_figs(ax_empty_mc[i], ax_empty_mc2[i], fig_empty_mc[i],
                x_positions, x_labels, comm_prot, "", "Empty CS", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=lat_ecs_inc[i], 
                hatches=client_hatches, hatch_categories=cn_hatch_categories,
                include_hatch_keys=comp_nodes, log=log[i], latplot_idx=i)

    for i in range(mlLatPlotsNR):
        save_lat_figs(ax_ml[i], ax_ml2[i], fig_ml[i],
                x_positions, x_labels, comm_prot, "OPTS", "MLocks", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=lat_ml_inc[i], 
                hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                include_hatch_keys=lockNRs, log=log[i], latplot_idx=i)

        save_lat_figs(ax_kvs[i], ax_kvs2[i], fig_kvs[i],
                x_positions, x_labels, comm_prot, "OPTS", "KVS", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=lat_ml_inc[i],
                hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                include_hatch_keys=lockNRs, log=log[i], latplot_idx=i)
    
    for i, tp_inc in enumerate(tp_incs):
        y1, y2 = tp_axis_titles[tp_inc]
        save_tp_figs(ax_empty_mc_fair[i], ax_empty_mc_fair2[i], fig_empty_mc_fair[i],
                x_positions, x_labels, comm_prot, "OPTS", "Empty CS", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=tp_inc,
                hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                include_hatch_keys=lockNRs, log=0, t=tp_inc, y1=y1, y2=y2)

        save_tp_figs(ax_ml_fair[i], ax_ml_fair2[i], fig_ml_fair[i],
                x_positions, x_labels, comm_prot, "OPTS", "MLocks", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=tp_inc,
                hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                include_hatch_keys=lockNRs, log=0, t=tp_inc, y1=y1, y2=y2)

        save_tp_figs(ax_kvs_fair[i], ax_kvs_fair2[i], fig_kvs_fair[i],
                x_positions, x_labels, comm_prot, "OPTS", "KVS", client_mode=C_str, clients=comp_nodes,
                nthreads=threadNRs, include_metrics=tp_inc,
                hatches=mlocks_hatches, hatch_categories=mlocks_hatch_categories,
                include_hatch_keys=lockNRs, log=0, t=tp_inc, y1=y1, y2=y2)


RES_DIRS = {}
DATA = {}

prep_res_dirs(RES_DIRS)
read_data(DATA, RES_DIRS)

plot_MC_rlocks(
                DATA, 
                opts=["", "Rfaa", "HodOcm", "HodOcmRfaa"],
                # lat_ecs_inc = [["gwait_acq", "gwait_rel"]],
                # lat_ml_inc = [["lwait_acq"], ["lwait_acq", "gwait_acq", "gwait_rel"], ["data_read", "data_write", "lock_hold"]],
                lat_ml_inc = [["lwait_acq"], ["gwait_acq", "gwait_rel"], ["data_read", "data_write", "lock_hold"]],
                tp_incs=["lock_acquires", "glock_tries", "handovers_data", "cache_misses"],
                cnNRs=[1, 4], 
                lockNRs=[16, 128, 512, 16384], 
                threadNRs=32,
                log=[1,1,0],
                )
pass