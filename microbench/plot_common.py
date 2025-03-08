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
"litlHod",
"litlHoOcmBw",
"litlHodOcmBw",
"sherman",
"shermanHo",
"shermanHod",
"shermanHodOcmBw",
"shermanLock",
]

OPT_TO_NAME = {
"litl": "", 
"litlHo": "Ho",
"litlHod": "Hod",
"litlHoOcmBw": "HoOcmBw",
"litlHodOcmBw": "HodOcmBw",

"shermanLock": "",
"shermanHo": "Ho",
"shermanHod": "Hod",
"sherman": "HoOcmBw",
"shermanHodOcmBw": "HoOcmBw",
"shermanHodOcmBw": "HodOcmBw",
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
"end_to_end",
"array_size",
"nodeID",
"run",
"lockNR",
"pinning",
]

TP_COLS = [
"tid",
"loop_in_cs",
"lock_acquires",
"duration",
"glock_tries",
"handovers",
"handovers_data",
"array_size",
"nodeID",
"run",
"lockNR",
"la",
"pinning",
"cache_misses",
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

MICROBENCHES = [ "empty_cs", "mlocks", "kvs"]

STATS = ["tp", "lat"]

tp_axis_titles = {
    "lock_acquires" : ("TP (ops/s)", "Jain's Fairness Index"),
    "glock_tries": ("[GLock CASes]/[Lock Acquisition]", ""),
    "handovers": ("Handovers", ""),
    "handovers_data": ("Handovers w/ data", ""),
}

# lat_bar_colors = {
# "gwait_acq": "gray", "lwait_acq": "gold", 
# "gwait_rel": "cyan", "lwait_rel": "green",
# "lock_hold": "red", 
# "data_read": "white", "data_write": "magenta",
# }
lat_bar_colors = {
    "gwait_acq": "#1f77b4",  # Muted Blue (commonly used in scientific plots)
    "lwait_acq": "#ff7f0e",  # Orange
    "gwait_rel": "#2ca02c",  # Green
    "lwait_rel": "#d62728",  # Red
    "lock_hold": "#9467bd",  # Purple
    "data_read": "#8c564b",  # Brown
    "data_write": "#e377c2", # Pink
}
# lat_bar_colors = {
#     "gwait_acq": "white",  
#     "lwait_acq": "dimgray",
#     "gwait_rel": "black",  
#     "lwait_rel": "black",  
#     "lock_hold": "gray",  
#     "data_read": "white",
#     "data_write": "black", 
# }
median_colors = {"gwait_acq": "silver", "lwait_acq": "orange", "gwait_rel": "violet", "lwait_rel": "cyan", "lock_hold": "purple"}
node_colors = {"empty_cs1n": "gray", "empty_cs2n": "black", "mem1n": "gray", "mem2n": "black"}
client_hatches = {1:'/', 2:'\\', 3:'|', 5:'-', 4:'+'}
mlocks_hatches = {1:'/', 128:'\\', 256:'|', 1024:'-', 512:'', 16384: ''}

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

        ax.scatter([position], [values_lat[1][measurement].mean()],
                    marker="o", color=lat_bar_colors[measurement], edgecolor="black", zorder=3)
        sum += mean
        
    ax2.scatter([position], [lock_acquires.mean() / duration], marker="x", color="black")

def add_box(ax1, ax2, position, values, hatch_idx, bw=0.3,
            hatches=client_hatches, lockNR=1, tp_inc="lock_acquires", inc_fair=True):
    duration = values["duration"].max() * DURATION_FACTOR
    la = values["lock_acquires"]
    data = values.loc[values["lockNR"] == lockNR, tp_inc]

    if tp_inc == "lock_acquires":
        data = data / duration 
    if tp_inc == "glock_tries":
        data = data / la

    bps = ax1.boxplot(
        data,
        positions=[position],
        widths=bw,
        patch_artist=True,
        boxprops=dict(facecolor='none'),
        medianprops=dict(color='black'),
    )
    if hatch_idx:
        for bp in bps["boxes"]:
            bp.set_hatch(hatches[hatch_idx])
    num_runs = values["run"].max() + 1
    if inc_fair:
        fairness = 0
        for i in range(num_runs):
            fairness += jain_fairness_index(data[values["run"] == i])
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


def set_ax(ax1, ax2, x_positions, x_labels, 
           x1_title, y1_title1, y1_title2,
           log=1):

    ax1.set_xticks(x_positions)
    ax1.set_xticklabels(x_labels, rotation=45, ha='right')
    ax1.set_xlabel(x1_title)
    ax1.set_ylabel(y1_title1)
    ax2.set_ylabel(y1_title2)
    if log:
        ax1.set_yscale('log')
    ax1.grid(linestyle="--", alpha=0.7)

    
def save_lat_figs(ax1, ax2, fig,
              x_positions, x_labels, 
              comm_prot="rdma", opt="spinlock", bench="",
              client_mode ="MC", nthreads=16, log=1, clients=[1],
              include_metrics=[], hatches=[], hatch_categories={},
              include_hatch_keys=[], latplot_idx=0,
              y1="Median Latencies(ns)", y2="Avg TP (ops/s)",
              t="Latency+TP"
              ):
    
    clients_str = "|".join(map(str, clients))
    title = f"{t} | {bench} | {clients_str} CNs | {comm_prot} | {opt}"

    set_ax(ax1, ax2, x_positions, x_labels, 
            "Implementation", y1, y2,
            log)
    set_legend(ax1, hatches, hatch_categories,
                include_metrics, include_hatch_keys)

    output_path = file_dir+f"/results/plots/lat/lat_{comm_prot}_{opt}_{bench}_{client_mode}_{nthreads}T_{latplot_idx}.png"
    fig.suptitle(title)
    fig.savefig(output_path, dpi=300, bbox_inches='tight')

def save_tp_figs(ax1, ax2, fig,
              x_positions, x_labels, 
              comm_prot="rdma", opt="spinlock", bench="",
              client_mode ="MC", nthreads=16, log=0, clients=[1],
              include_metrics=[], hatches=[], hatch_categories={},
              include_hatch_keys=[],
              y1="TP (ops/s)", y2="Jain's Fairness Index",
              t="TP+Fairness",
              ):
    
    clients_str = "|".join(map(str, clients))
    title = f"{t} | {bench} | {clients_str} CNs | {comm_prot} | {opt}"

    set_ax(ax1, ax2, x_positions, x_labels, 
            "Implementation", y1, y2,
            log)
    set_legend(ax1, hatches, hatch_categories,
                include_metrics, include_hatch_keys, False)
            
    output_path = file_dir+f"/results/plots/tp/tp_{comm_prot}_{opt}_{bench}_{client_mode}_{nthreads}T_{t}.png"
    fig.suptitle(title)
    fig.savefig(output_path, dpi=300, bbox_inches='tight')

    
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
        
    
def make_multiplots(inc):
    figs = []
    axs = []
    axs2 = []
    incNR = len(inc)
    for i in range(incNR):
        fig, ax1, ax2 = make_ax_fig(FIG_X, FIG_Y)
        figs.append(fig)
        axs.append(ax1)
        axs2.append(ax2)
    return figs, axs, axs2

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


                elif stat == "tp":
                    for line in file:
                        if line == "":
                            continue
                        cleaned_lines.append(line) 

                    cleaned_data = StringIO("".join(cleaned_lines))
                    DATA[impl][nodeNR][threadNR] = pd.read_csv(cleaned_data, skiprows=1, names=COLS)
                
                # TODO:
                elif stat == "ldist":
                    pass

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
                        

                    
                