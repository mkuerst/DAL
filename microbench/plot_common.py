import os
import re
from io import StringIO
import glob
import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.lines as mlines            

file_dir = os.path.dirname(os.path.realpath(__file__))

REMOTE_LOCKS = ["spinlock", "lease1"]
N_RLOCKS = len(REMOTE_LOCKS)

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
"run", "nlocks",
]

SERVER_COLS = [
    "tid", "wait_acq", "wait_rel","client_id","run"
]

IMPL = [
# FLAT
"backoff_original",
"clh_spinlock",
"clh_spin_then_park",
"partitioned_original",
"pthreadinterpose_original",
"spinlock_original",
"ticket_original",
"ttas_original",
# QUEUE
"mcs_spinlock",
"mcs_spin_then_park",
# HIERARCHICAL
"cbomcs_spin_then_park",
"cbomcs_spinlock",
"cna_spinlock",
"cptltkt_original",
"ctkttkt_original",
"hmcs_original",
"htlockepfl_original",
"hyshmcs_original",
# LOAD CONTROL
"malthusian_spinlock",
"malthusian_spin_then_park",
# OTHER
"alockepfl_original",
"concurrency_original",
"fns_spinlock",
"fnm_spin_then_park",
"mutexee_original",
]

FLAT = [
"backoff_original",
"partitioned_original",
"pthreadinterpose_original",
"spinlock_original",
"ticket_original",
"ttas_original",
]

QUEUE = [
"clh_spin_then_park",
"clh_spinlock",
"mcs_spin_then_park",
"mcs_spinlock",
]

HIERARCHICAL = [
"cbomcs_spin_then_park",
"cbomcs_spinlock",
"cna_spinlock",
"cptltkt_original",
"ctkttkt_original",
"hmcs_original",
"htlockepfl_original",
"hyshmcs_original",
]

LOAD_CONTROL = [
"malthusian_spin_then_park",
"malthusian_spinlock",
]

OTHER = [
"alockepfl_original",
"concurrency_original",
"fns_spinlock",
"fnm_spin_then_park",
"mutexee_original",
]

DELEGATION = [
    
]

IMPL_DICT = {"flat": FLAT,
            "queue": QUEUE, 
            "hierachical": HIERARCHICAL,
            "load control": LOAD_CONTROL,
            "other": OTHER,
            "delegation": DELEGATION}

MICROBENCHES = ["empty_cs1n", "empty_cs2n", "lat", "mem1n", "mem2n", "mlocks1n", "mlocks2n"]

# TODO: Add "single"
STATS = ["cum"]

lat_bar_colors = {
"gwait_acq": "gray", "lwait_acq": "gold", 
"gwait_rel": "blue", "lwait_rel": "green",
"lock_hold": "red", 
"data_read": "white", "data_write": "magenta",
}
median_colors = {"gwait_acq": "silver", "lwait_acq": "orange", "gwait_rel": "violet", "lwait_rel": "cyan", "lock_hold": "purple"}
node_colors = {"empty_cs1n": "gray", "empty_cs2n": "black", "mem1n": "gray", "mem2n": "black"}
client_hatches = {1:'/', 2:'\\', 3:'|', 5:'-', 4:'+'}
mlocks_hatches = {1:'/', 128:'\\', 256:'|', 1024:'-', 512:'+'}

BW_RLOCKS = 0.9 / N_RLOCKS
FIG_X = 10
FIG_Y = 6
DURATION_FACTOR = 1e3
LAT_FACTOR = 1

def jain_fairness_index(x):
    numerator = np.sum(x)**2
    denominator = len(x) * np.sum(x**2)
    return numerator / denominator if denominator != 0 else 0

#########
## FIG ##
#########
def make_offset(candidates, bw):
    num_cand = len(candidates)
    if num_cand == 1:
        return {candidates[0]: 0}
    if num_cand == 2:
       return {candidates[0]: -0.5*bw, candidates[1]:.5*bw} 
    if num_cand == 3:
       return {candidates[0]: -bw, candidates[1]: 0, candidates[2]: bw} 

def add_lat(ax, ax2, values, position, comm_prot, bar_width, inc, hatches={}, hatch_key=0):
    duration = values["total_duration"].max() * DURATION_FACTOR
    lock_acquires = values["lock_acquires"]
    cum = 0

    for measurement in inc:
        mean = (values[measurement] / lock_acquires).mean() / LAT_FACTOR if (measurement in values) else 0
        median = (values[measurement] / lock_acquires).median() / LAT_FACTOR if (measurement in values) else 0
        bars = ax.bar(position, mean, width=bar_width, bottom=cum,
            color=lat_bar_colors[measurement], label=f"{comm_prot} ({measurement})" if position == 1 else "",
            edgecolor="black"
        )
        if hatch_key:
            for bar in bars:
                bar.set_hatch(hatches[hatch_key])
        # ax.scatter([position], [cum + median], marker="x", color=median_colors[measurement])
        cum += mean
        
    ax2.scatter([position], [lock_acquires.mean() / duration], marker="x", color="black")

def add_box(ax1, ax2, position, values, num_clients, bw=0.3):
    duration = values["total_duration"].max() * DURATION_FACTOR
    lock_acquires = values["lock_acquires"]
    bps = ax1.boxplot(
        values["lock_acquires"] / duration,
        positions=[position],
        widths=bw,
        patch_artist=True,
    )
    if num_clients:
        for bp in bps["boxes"]:
            bp.set_hatch(client_hatches[num_clients])
    num_runs = values["run"].max() + 1
    fairness = 0
    for i in range(num_runs):
        fairness += jain_fairness_index(lock_acquires[values["run"] == i])
    fairness /= num_runs
    ax2.scatter([position], fairness, marker="^", color="gold", edgecolor="black")

def add_vline(axs, position, bw):
    for ax in axs:
        ax.axvline(x=position+2*bw-0.1, color='black', linestyle='--', alpha=0.5)

def set_legend(ax1, hatches, hatch_categories,
               include_metrics, include_hatch_keys):
    legend_lat = {}
    for metric,color in lat_bar_colors.items():
        if metric in include_metrics:
            legend_lat[metric] = mlines.Line2D(
                                    [], [],
                                    color=color,
                                    marker="o",
                                    markersize=8,
                                    linestyle="None",
                                    markeredgecolor='black',
                                    label=metric)
    legend_lat["Avg TP"] = mlines.Line2D(
                            [], [],
                            color="black",
                            marker="x",
                            markersize=8,
                            linestyle="None",
                            label="Avg TP")
    for key, value in hatches.items():
        if key in include_hatch_keys:
            legend_lat[key] =  plt.Rectangle((0, 0), 10, 10, facecolor="white", hatch=value, edgecolor='black', label=hatch_categories[key])  

    # ax1.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
    #         title="", loc="upper left", bbox_to_anchor=(1.05, 1))
    ax1.legend(legend_lat.values(), [entry.get_label() for entry in legend_lat.values()],
            title="",
            fontsize='x-small',
            # bbox_to_anchor=(1.05, 1),
            markerscale=0.6,
            labelspacing=0.2,
            borderaxespad=0.1,
            borderpad=0.1,
            handlelength=1,
            handleheight=1,
            loc='upper right'
            )


def set_ax(ax1, ax2, ax3, ax4, x_positions, x_labels, x1_title, y1_title1, y1_title2,
           x2_title, y2_title1, y2_title2, log=1):
    if ax1 and ax2 is not None:
            ax1.set_xticks(x_positions)
            ax1.set_xticklabels(x_labels, rotation=45, ha='right')
            ax1.set_xlabel(x1_title)
            ax1.set_ylabel(y1_title1)
            ax2.set_ylabel(y1_title2)
            if log:
                ax1.set_yscale('log')
            ax1.grid(linestyle="--", alpha=0.7)

    if ax3 is not None and ax4 is not None:
        ax3.set_xticks(x_positions)
        ax3.set_xticklabels(x_labels, rotation=45, ha='right')
        ax3.set_xlabel(x2_title)
        ax3.set_ylabel(y2_title1)
        ax4.set_ylabel(y2_title2)
        # ax3.set_yscale('log')
        ax3.grid(linestyle="--", alpha=0.7)
    
def save_figs(ax1, ax2, ax3, ax4, fig1, fig2,
              x_positions, x_labels, 
              comm_prot="rdma", remote_lock="spinlock", bench="",
              client_mode ="MC", nthreads=16, log=1, clients=[1],
              include_metrics=[], hatches=[], hatch_categories={},
              include_hatch_keys=[], multi=0,
              ):
    
    clients_str = "|".join(map(str, clients))
    title1 = f"Latency Wait+TP {bench} {clients_str} Cs {comm_prot} {remote_lock}"
    title2 = f"TP+Fairness {bench} {clients_str} Cs {comm_prot} {remote_lock}"

    if not multi:
        set_ax(ax1, ax2, ax3, ax4, x_positions, x_labels, 
               "Implementation", "Mean Latencies (ms)", "TP (ops/ms)",
               "Implementation", "TP (ops/ms)", "Jain's Frainess Index",
               log)
        set_legend(ax1, hatches, hatch_categories,
                   include_metrics, include_hatch_keys)

    else:
        for a1,a2,a3,a4 in zip(ax1,ax2,ax3,ax4):
            set_ax(a1, a2, a3, a4, x_positions, x_labels, 
                "Implementation", "Mean Latencies (ms)", "TP (ops/ms)",
                "Implementation", "TP (ops/ms)", "Jain's Frainess Index",
                log)

        set_legend(a1, hatches, hatch_categories,
                   include_metrics, include_hatch_keys)
            
    output_path = file_dir+f"/plots/cumlat_{comm_prot}_{remote_lock}_{bench}_{client_mode}_{nthreads}T_.png"
    fig1.suptitle(title1)
    fig1.savefig(output_path, dpi=300, bbox_inches='tight')

    if ax3 is not None and ax4 is not None:
        output_path = file_dir+f"/plots/tpfair_{comm_prot}_{remote_lock}_{bench}_{client_mode}_{nthreads}T_.png"
        fig2.suptitle(title2)
        fig2.savefig(output_path, dpi=300, bbox_inches='tight')
    
def make_ax_fig(x, y, rows=1, cols=1):
    if rows == 1 and cols == 1:
        fig, ax = plt.subplots(figsize=(x,y))
        ax2 = ax.twinx()
        return fig, ax, ax2

    fig, axs = plt.subplots(rows, cols, figsize=(x,y))
    axs2 = []
    for ax in axs:
        axs2.append(ax.twinx())
    return fig, axs, axs2
        
    

##########
## READ ##
##########
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
        DATA[comm_prot] = {}
        for remote_lock in REMOTE_LOCKS:
            DATA[comm_prot][remote_lock] = {}
            for stat in STATS:
                DATA[comm_prot][remote_lock] = {"client": {stat: {}}, "server": {stat: {}}}
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
                RES_DIRS[comm_prot][remote_lock]["client"][stat] = {}
                RES_DIRS[comm_prot][remote_lock]["server"][stat] = {}
                for mb in MICROBENCHES:
                    client_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/client/{stat}/*/{mb}/"
                    client_res_dir = glob.glob(client_res_dir)
                    server_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/{comm_prot}/{remote_lock}/server/{stat}/*/{mb}/"
                    server_res_dir = glob.glob(server_res_dir)

                    RES_DIRS[comm_prot][remote_lock]["client"][stat][mb] = client_res_dir
                    RES_DIRS[comm_prot][remote_lock]["server"][stat][mb] = server_res_dir

    RES_DIRS["orig"] = {stat: {} for stat in STATS}
    for mb in MICROBENCHES:
        for stat in STATS:
            orig_res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/orig/{stat}/*/{mb}/"
            orig_res_dir = glob.glob(orig_res_dir)
            RES_DIRS["orig"][stat][mb] = orig_res_dir

