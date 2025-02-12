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

OPTS =  [
"litl", 
"litlHo",
"litlHoOcmBw",
"sherman",
"shermanHo",
"shermanLock"
]

OPT_TO_NAME = {
"litl": "", 
"litlHo": "Ho",
"litlHoOcmBw": "HoOcmBw",
"sherman": "HoOcmBw",
"shermanHo": "Ho",
"shermanLock": "",
}
    
COMM_PROTOCOLS = ["rdma"]

LAT_COLS = [
"lock_hold",
"lwait_acq",
"lwait_rel",
"gwait_acq",
"gwait_rel",
"data_read",
"data_write",
"array_size",
"nodeID",
"run",
"lockNR"
]

TP_COLS = [
"tid",
"loop_in_cs",
"lock_acquires",
"duration",
"glock_tries",
"array_size",
"nodeID",
"run",
"lockNR"
]

STAT_TO_COLS = {
    "tp": TP_COLS,
    "lat": LAT_COLS,
}

IMPL = [
# FLAT
"alockepfl_original",
"pthreadinterpose_original",
"backoff_original",
"clh_spinlock",
"clh_spin_then_park",
"partitioned_original",
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
"concurrency_original",
"fns_spinlock",
"fnm_spin_then_park",
"mutexee_original",
"shermanLock",
]

FLAT = [
"alockepfl_original",
"backoff_original",
"partitioned_original",
"pthreadinterpose_original",
"spinlock_original",
"ticket_original",
"ttas_original",
"shermanLock",
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

MICROBENCHES = [ "empty_cs", "mlocks"]

# TODO: Add "single"
STATS = ["tp", "lat"]

lat_bar_colors = {
"gwait_acq": "gray", "lwait_acq": "gold", 
"gwait_rel": "cyan", "lwait_rel": "green",
"lock_hold": "red", 
"data_read": "white", "data_write": "magenta",
}
median_colors = {"gwait_acq": "silver", "lwait_acq": "orange", "gwait_rel": "violet", "lwait_rel": "cyan", "lock_hold": "purple"}
node_colors = {"empty_cs1n": "gray", "empty_cs2n": "black", "mem1n": "gray", "mem2n": "black"}
client_hatches = {1:'/', 2:'\\', 3:'|', 5:'-', 4:'+'}
mlocks_hatches = {1:'/', 128:'\\', 256:'|', 1024:'-', 512:'+'}

FIG_X = 10
FIG_Y = 6
DURATION_FACTOR = 1e1
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

def add_lat(ax, ax2, values_lat, values_tp, position, comm_prot, bar_width,
            inc, hatches={}, hatch_key=0, lockNR=1):
    duration = values_tp["duration"].max() * DURATION_FACTOR
    lock_acquires = values_tp.loc[values_tp["lockNR"] == lockNR, "lock_acquires"]
    sum = 0

    for measurement in inc:

        mean = values_lat[0][measurement].mean() / LAT_FACTOR if (measurement in values_lat[0]) else 0
        bars = ax.bar(position, mean, width=bar_width, bottom=sum,
            color=lat_bar_colors[measurement], label=f"{comm_prot} ({measurement})" if position == 1 else "",
            edgecolor="black"
        )
        if hatch_key:
            for bar in bars:
                bar.set_hatch(hatches[hatch_key])

        ax.scatter([position], [values_lat[1][measurement].mean() + sum],
                    marker="o", color=lat_bar_colors[measurement], edgecolor="black", zorder=3)
        sum += mean
        
    ax2.scatter([position], [lock_acquires.mean() / duration], marker="x", color="black")

def add_box(ax1, ax2, position, values, hatch_idx, bw=0.3, hatches=client_hatches, lockNR=1):
    duration = values["duration"].max() * DURATION_FACTOR
    lock_acquires = values.loc[values["lockNR"] == lockNR, "lock_acquires"]
    bps = ax1.boxplot(
        lock_acquires / duration,
        positions=[position],
        widths=bw,
        patch_artist=True,
    )
    if hatch_idx:
        for bp in bps["boxes"]:
            bp.set_hatch(hatches[hatch_idx])
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
               include_metrics, include_hatch_keys, lat=True):
    legend_lat = {}
    if lat:
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
    else:
        legend_lat["Jain's Fairness"]= mlines.Line2D(
                                [], [],
                                color="gold",
                                marker="^",
                                markersize=8,
                                linestyle="None",
                                markeredgecolor='black',
                                label="Jain's Fairness")

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
              comm_prot="rdma", opt="spinlock", bench="",
              client_mode ="MC", nthreads=16, log=1, clients=[1],
              include_metrics=[], hatches=[], hatch_categories={},
              include_hatch_keys=[], multi=0,
              ):
    
    clients_str = "|".join(map(str, clients))
    title1 = f"Latency+TP | {bench} | {clients_str} CNs | {comm_prot} | {opt}"
    title2 = f"TP+Fairness | {bench} | {clients_str} CNs | {comm_prot} | {opt}"

    if not multi:
        set_ax(ax1, ax2, ax3, ax4, x_positions, x_labels, 
               "Implementation", "Median Latencies (ns)", "Avg TP (ops/s)",
               "Implementation", "TP (ops/s)", "Jain's Frainess Index",
               log)
        set_legend(ax1, hatches, hatch_categories,
                   include_metrics, include_hatch_keys)
        set_legend(ax3, hatches, hatch_categories,
                   [], include_hatch_keys, False)

    else:
        for a1,a2,a3,a4 in zip(ax1,ax2,ax3,ax4):
            set_ax(a1, a2, a3, a4, x_positions, x_labels, 
                "Implementation", "Median Latencies (ns)", "Avg TP (ops/s)",
                "Implementation", "TP (ops/s)", "Jain's Frainess Index",
                log)

        set_legend(a1, hatches, hatch_categories,
                   include_metrics, include_hatch_keys)
            
    output_path = file_dir+f"/results/plots/lat_{comm_prot}_{opt}_{bench}_{client_mode}_{nthreads}T_.png"
    fig1.suptitle(title1)
    fig1.savefig(output_path, dpi=300, bbox_inches='tight')

    if ax3 is not None and ax4 is not None:
        output_path = file_dir+f"/results/plots/tpfair_{comm_prot}_{opt}_{bench}_{client_mode}_{nthreads}T_.png"
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
def to_pd(DATA, dirs, COLS, stat):
    for dir in dirs:
        impl = Path(dir).name.removeprefix("lib")
        csv_dirs = glob.glob(dir+"/nodeNR*_threadNR*.csv")
        if impl == "sherman":
            impl = "shermanLock"
        DATA[impl] = {}
        for csv_dir in csv_dirs:
            match = re.search(r"nodeNR(\d+)_threadNR(\d+).csv", os.path.basename(csv_dir))
            nodeNR = int(match.group(1))
            threadNR = int(match.group(2))
            if not DATA[impl].get(nodeNR):
                DATA[impl][nodeNR] = {}
            cleaned_lines = []
            cleaned_lines_median = []
            cleaned_lines_99 = []

            with open(csv_dir, 'r') as file:
                if stat == "lat":
                    i = 0
                    for line in file:
                        if i == 0:
                            i += 1
                            continue
                        
                        if i % 2 == 1: 
                            cleaned_lines_median.append(line)
                        else:
                            cleaned_lines_99.append(line)

                        i += 1
                        
                    cleaned_median = StringIO("".join(cleaned_lines_median))
                    cleaned_99 = StringIO("".join(cleaned_lines_99))
                    pd_median = pd.read_csv(cleaned_median, skiprows=0, names=COLS)
                    pd_99 = pd.read_csv(cleaned_99, skiprows=0, names=COLS)

                    pd_median.loc[pd_median["lwait_acq"] >= 1000, "lwait_acq"] = (pd_median.loc[pd_median["lwait_acq"] >= 1000, "lwait_acq"] - 1000) * 1000
                    pd_99.loc[pd_99["lwait_acq"] >= 1000, "lwait_acq"] = (pd_99.loc[pd_99["lwait_acq"] >= 1000, "lwait_acq"] - 1000) * 1000

                    pd_median.loc[pd_median["gwait_acq"] >= 1000, "gwait_acq"] = (pd_median.loc[pd_median["gwait_acq"] >= 1000, "gwait_acq"] - 1000) * 1000
                    pd_99.loc[pd_99["gwait_acq"] >= 1000, "gwait_acq"] = (pd_99.loc[pd_99["gwait_acq"] >= 1000, "gwait_acq"] - 1000) * 1000

                    DATA[impl][nodeNR][threadNR] = [pd_median, pd_99]


                else:
                    for line in file:
                        if line == "":
                            continue
                        cleaned_lines.append(line) 

                    cleaned_data = StringIO("".join(cleaned_lines))
                    DATA[impl][nodeNR][threadNR] = pd.read_csv(cleaned_data, skiprows=1, names=COLS)

def read_data(DATA, RES_DIRS):
    for stat in STATS:
        COLS = STAT_TO_COLS[stat]
        DATA[stat] = {}
        for comm_prot in COMM_PROTOCOLS:
            DATA[stat][comm_prot] = {}
            for mb in MICROBENCHES:
                DATA[stat][comm_prot][mb] = {}
                for opt in RES_DIRS[stat][comm_prot][mb].keys():
                    DATA[stat][comm_prot][mb][opt] = {}
                        
                    to_pd(DATA[stat][comm_prot][mb][opt], RES_DIRS[stat][comm_prot][mb][opt], COLS, stat)

def prep_res_dirs(RES_DIRS):
    for stat in STATS:
        RES_DIRS[stat] = {}
        for comm_prot in COMM_PROTOCOLS:
            RES_DIRS[stat][comm_prot] = {}
            for mb in MICROBENCHES:
                RES_DIRS[stat][comm_prot][mb] = {}
                for opt in OPTS:
                    res_dir = os.path.dirname(os.path.realpath(__file__))+f"/results/cn/{stat}/{comm_prot}/{mb}/{opt}/*"
                    res_dir = glob.glob(res_dir)
                    opt = OPT_TO_NAME[opt]
                    if not opt in RES_DIRS[stat][comm_prot][mb].keys():
                        RES_DIRS[stat][comm_prot][mb][opt] = res_dir
                    else:
                        RES_DIRS[stat][comm_prot][mb][opt] += res_dir
                        

                    
                