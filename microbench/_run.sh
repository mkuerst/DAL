#!/bin/sh
TCP_PORT0=8000
TCP_PORT1=8022
TCP_PORT2=8080
RDMA_PORT=20051
RDMA_PORT1=20052
RDMA_PORT2=20053
 
chmod +x "$0"

cleanup_exit() {
    echo ""
    echo "Cleaning up..."
    if kill -0 $SERVER_PID 2>/dev/null; then
        echo "Stopping server with PID $SERVER_PID..."
        kill $SERVER_PID
    fi
    tmux kill-server
    fuser -k ${TCP_PORT0}/tcp 2>/dev/null
    fuser -k ${TCP_PORT1}/tcp 2>/dev/null
    fuser -k ${TCP_PORT2}/tcp 2>/dev/null
    fuser -k ${RDMA_PORT}/rdma 2>/dev/null
    fuser -k ${RDMA_PORT1}/rdma 2>/dev/null
    fuser -k ${RDMA_PORT2}/rdma 2>/dev/null
    # rdma-devices reset
    pkill -P $$ 
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -9 "$pid"
    done
    echo "CLEANUP DONE"
    echo "EXIT"
    exit 1
}

cleanup() {
    echo ""
    echo "Cleaning up..."
    if kill -0 $SERVER_PID 2>/dev/null; then
        echo "Stopping server with PID $SERVER_PID..."
        kill -SIGINT $SERVER_PID
    fi
    fuser -k ${TCP_PORT0}/tcp 2>/dev/null
    fuser -k ${TCP_PORT1}/tcp 2>/dev/null
    fuser -k ${TCP_PORT2}/tcp 2>/dev/null
    fuser -k ${RDMA_PORT}/rdma 2>/dev/null
    fuser -k ${RDMA_PORT1}/rdma 2>/dev/null
    fuser -k ${RDMA_PORT2}/rdma 2>/dev/null
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -SIGINT "$pid"
    done
    sleep 3
    tmux kill-server
    pkill -P $$ 
    echo "CLEANUP DONE"
}

trap cleanup_exit SIGINT
trap cleanup_exit SIGTERM
trap cleanup_exit SIGKILL
trap cleanup_exit SIGHUP

#LOCAL
# REMOTE_USER="mihi"
# REMOTE_SERVER="localhost"
# server_ip=10.5.12.168

# HOME
# server_ip=192.168.1.70
#CLUSTER
REMOTE_USER="kumichae"
REMOTE_SERVER="r630-12"
server_ip=10.233.0.21
# REMOTE_SERVER="r630-06"
# server_ip=10.233.0.15
REMOTE_CLIENTS=("r630-11" "r630-03" "r630-04" "r630-06")

REMOTE_USER="root"
REMOTE_SERVER="node1"
REMOTE_CLIENTS=("node1" "node2" "node3" "node4" "node5")
server_ip=10.10.1.1

# eval "$(ssh-agent -s)"

# for remote_client in ${REMOTE_CLIENTS[@]}
# do
#     ssh-copy-id "$REMOTE_USER@$remote_client"
# done
# for remote_client in ${REMOTE_CLIENTS[@]}
# do
#     ssh-copy-id "$REMOTE_USER@$remote_client"
# done

# PATHS
BASE="$PWD/../litl2/lib"
server_logpath="$PWD/server_logs"
client_logpath="$PWD/client_logs"
tcp_server_app="$PWD/../litl2/tcp_server"
rdma_server_app="$PWD/../litl2/rdma_server"
client_suffix="_client.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
client_libs_dir=$BASE"/client/"
spinlock_so="$server_libs_dir/libspinlock_original_server.so"
disa_bench="$PWD/main_disa"


client_filecum_header="tid,loop_in_cs,lock_acquires,lock_hold(ms),total_duration(s),\
wait_acq(ms),wait_rel(ms),lwait_acq,lwait_rel,\
gwait_acq,gwait_rel,data_read,data_write,\
glock_tries,array_size(B),client_id,run,nlocks"

client_filesingle_header="tid,total_duration(s),\
min_slock_hold,med,max,\
min_swait_acq,med,max,\
min_swait_rel,med,max,\
min_slwait_acq,med,max,\
min_slwait_rel,med,max,\
min_sgwait_acq,med,max,\
min_sgwait_rel,med,max,\
min_sdata_read,med,max,\
min_sdata_write,med,max,\
min_sglock_tries,med,max,\
array_size(B),client_id,\
nlocks"

server_file_header="tid,wait_acq(ms),wait_rel(ms),client_id,run"

# MICROBENCH INPUTS
critical=1000

rm -rf server_logs/
rm -rf client_logs/
rm -rf barrier_files/*

comm_prot="rdma"
microbenches=("empty_cs2n" "empty_cs1n" "lat" "mem2n" "mem1n" "mlocks2n" "mlocks1n" "correctness")
node_ids=(2 3 4)
peer_ips=(    ""
    "10.233.0.10"
    "10.233.0.11"
    "10.233.0.12"
    "10.233.0.13"
    "10.233.0.14"
    "10.233.0.15"
    "10.233.0.16"
    "10.233.0.17"
    "10.233.0.18"
    "10.233.0.19"
    "10.233.0.20"
    "10.233.0.21"    
)
peer_ips=(
    ""
    ""
    "10.10.1.2"
    "10.10.1.3"
    "10.10.1.4"
    "10.10.1.5"
    "10.10.1.6"
)

opts=("spinlock")
duration=5
mem_runs=1
runs=1
n_clients=(1)
n_threads=(1)
bench_idxs=(7)
num_locks=(1)

for impl_dir in "$BASE"/original/*
do
    for opt in ${opts[@]}
    do
        impl=$(basename $impl_dir)
        impl=${impl%.so}
        client_opt_suffix=_client_$opt.so
        client_so=${client_libs_dir}${impl}$client_opt_suffix
        server_so=${server_libs_dir}${impl}$server_suffix
        for j in ${bench_idxs[@]}
        do
            microb="${microbenches[$j]}"
            client_rescum_dir="$PWD/results/$comm_prot/$opt/client/cum/$impl/$microb"
            client_ressingle_dir="$PWD/results/$comm_prot/$opt/client/single/$impl/$microb"
            server_res_dir="./results/$comm_prot/$opt/server/cum/$impl/$microb"
            server_log_dir="$server_logpath/$impl/$opt/$microb"
            client_log_dir="$client_logpath/$impl/$opt/$microb"
            mkdir -p "$client_rescum_dir" 
            mkdir -p "$client_ressingle_dir" 
            mkdir -p "$server_res_dir" 
            mkdir -p "$server_log_dir"
            mkdir -p "$client_log_dir"

            for nclients in ${n_clients[@]}
            do
                selected=()
                for ((i = 0; i < nclients; i++)); do
                    selected+=("${peer_ips[${node_ids[i]}]}")
                done
                p_ips=$(IFS=,; echo "${selected[*]}")
                # echo "Concatenated result: $p_ips"
                # exit 1

                for i in ${n_threads[@]}
                do
                    client_rescum_file="$client_rescum_dir"/nclients$nclients"_nthreads"$i.csv
                    client_ressingle_file="$client_ressingle_dir"/nclients$nclients"_nthreads"$i.csv
                    server_res_file="$server_res_dir"/nclients$nclients"_nthreads"$i.csv
                    orig_res_file="$orig_res_dir/nthread_$i.csv"
                    echo $client_filecum_header > "$client_rescum_file"
                    echo $client_filesingle_header > "$client_ressingle_file"
                    echo $server_file_header > "$server_res_file"

                    for nlocks in ${num_locks[@]}
                    do
                        server_session="server_$i"
                        echo "START SERVER $i T & $nclients C & $nlocks L & $duration s"

                        tmux new-session -d -s "$server_session" \
                        "sudo ssh $REMOTE_USER@$REMOTE_SERVER $rdma_server_app -c $nclients -a $server_ip -t $i -l $nlocks >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                        # tmux new-session -d -s "$server_session" \
                        # "ssh $REMOTE_USER@$REMOTE_SERVER LD_PRELOAD=$spinlock_so $tcp_server_app $i $j $nclients >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                        sleep 3

                        echo "START MICROBENCH $impl $microb $opt $nclients C & $i T & $nlocks L & $duration s"
                        dsh -M -f <(head -n $nclients ./clients.txt) -c \
                        "sudo LD_PRELOAD=$client_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $nclients -f $client_rescum_file -g $client_ressingle_file -l $nlocks -r $runs -e $mem_runs"

                        # ============= MPIRUN ========================================================
                        # sudo pdsh -u root -w node1,node2 \
                        # "$LD_PRELOAD=$client_so disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $nclients -f $client_rescum_file -g $client_ressingle_file -l $nlocks -r $runs -e $mem_runs"

                        # mpirun --hostfile ./clients.txt -np $nclients \
                        # --mca btl openib,self \
                        # --mca mpi_debug 1 \
                        # --mca btl_base_debug 1 --mca oob_tcp_debug 1 --mca plm_base_verbose 5 --mca orte_base_help_aggregate 0 \
                        # --report-bindings --mca mpi_add_procs_verbose 1 --mca mpi_btl_base_verbose 1 \
                        # --mca plm_rsh_agent "sudo ssh" \
                        # --x LD_PRELOAD=$client_so \
                        # $disa_bench \
                        # -t $i \
                        # -s $server_ip \
                        # -d $duration \
                        # -p $p_ips \
                        # -m $j \
                        # -c $nclients \
                        # -f $client_rescum_file \
                        # -g $client_ressingle_file \
                        # -l $nlocks \
                        # -r $runs\
                        # -e $mem_runs \
                        # 2>> $client_log_dir/nclients$n_clients"_nthreads"$i.log

                        # --mca btl_tcp_inf_include 10.233.0.10/24,10.233.0.11/24,10.233.0.20/24,10.233.0.14/24,10.233.0.15/24 \
                        # --mca btl_openib_allow_ib true \
                        # --mca btl_openib_cpc_include udcm \
                        # --mca btl_openib_cpc_exclude udcm \
                        # --mca btl_tcp_if_exclude lo,eno3,eno1,eno4,eno2,docker0 \
                        # --mca oob_tcp_dynamic_ipv4_ports 8000,8080 \
                        # --mca btl_tcp_port_min_v4 8000 --mca btl_tcp_port_range_v4 10 \
                        # --mca oob_tcp_if_include enp3s0 \
                        # ============= MPIRUN ========================================================
                        cleanup
                    done
                done
            done
        done
    done
done

pkill -u $USER ssh-agent 
cleanup_exit
# lsof | grep '.nfs'
# lsof -iTCP -sTCP:LISTEN
# lsof -i :20886
# kill -9 pid
# grep flags /proc/cpuinfo | grep -q " ida " && echo Turbo mode is on || echo Turbo mode is off

# LD_PRELOAD=/home/kumichae/DAL/litl2/lib/client/libcbomcs_spinlock_client.so /home/kumichae/DAL/microbench/main 1 2 1000 1 1
# /home/kumichae/DAL/litl2/tcp_server 1 1 1