#!/bin/sh
TCP_PORT0=8000
TCP_PORT1=8022
TCP_PORT2=8080
RDMA_PORT=20051
 
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
        kill $SERVER_PID
    fi
    tmux kill-server
    fuser -k ${TCP_PORT0}/tcp 2>/dev/null
    fuser -k ${TCP_PORT1}/tcp 2>/dev/null
    fuser -k ${TCP_PORT2}/tcp 2>/dev/null
    fuser -k ${RDMA_PORT}/rdma 2>/dev/null
    pkill -P $$ 
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -9 "$pid"
    done
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
rdma_ip=0.0.0.0

REMOTE_CLIENTS=("r630-11" "r630-03" "r630-04" "r630-06")
REMOTE_CLIENT="r630-11"

eval "$(ssh-agent -s)"
ssh_key="/home/mihi/.ssh/id_ed25519_localhost"
ssh-add $ssh_key  

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
glock_tries,array_size(B),client_id,run"
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
array_size(B),client_id"
server_file_header="tid,wait_acq(ms),wait_rel(ms),client_id,run"

# MICROBENCH INPUTS
duration=10
critical=1000

rm -rf server_logs/
rm -rf client_logs/
rm -rf barrier_files/*

comm_prot="rdma"
microbenches=("empty_cs2n" "empty_cs1n" "lat" "mem2n" "mem1n" "mlocks2n" "mlocks1n")
opts=("spinlock" "lease1")
client_ids=(0 1 2 3 4 5 6 7 8 9)

n_clients=(1 4)
n_threads=(16)
bench_idxs=(0 3)
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
                for i in ${n_threads[@]}
                do
                    for nlocks in ${num_locks[@]}
                    do
                        client_rescum_file="$client_rescum_dir"/nclients$nclients"_nthreads"$i.csv
                        client_ressingle_file="$client_ressingle_dir"/nclients$nclients"_nthreads"$i.csv
                        server_res_file="$server_res_dir"/nclients$nclients"_nthreads"$i.csv
                        orig_res_file="$orig_res_dir/nthread_$i.csv"
                        echo $client_filecum_header > "$client_rescum_file"
                        echo $client_filesingle_header > "$client_ressingle_file"
                        echo $server_file_header > "$server_res_file"

                        server_session="server_$i"
                        echo "START $impl $opt SERVER FOR $i THREADS PER CLIENT & $nclients CLIENTS & $nlocks LOCKS"

                        tmux new-session -d -s "$server_session" \
                        "ssh $REMOTE_USER@$REMOTE_SERVER $rdma_server_app -c $nclients -a $server_ip -t $i -l $nlocks >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                        # tmux new-session -d -s "$server_session" \
                        # "ssh $REMOTE_USER@$REMOTE_SERVER LD_PRELOAD=$spinlock_so $tcp_server_app $i $j $nclients >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                        sleep 3

                        # ============= MPIRUN ========================================================
                        echo "START MICROBENCH $microb WITH $nclients $opt MPI-CLIENTS AND $i THREADS PER MPI-CLIENT & $nlocks LOCKS"
                        mpirun --hostfile ./clients.txt -np $nclients \
                        --x LD_PRELOAD=$client_so \
                        --mca btl_tcp_if_exclude lo,eno3,eno1,eno4,eno2,docker0 \
                        --mca oob_tcp_dynamic_ipv4_ports 8000,8080 \
                        --mca btl_tcp_port_min_v4 8000 --mca btl_tcp_port_range_v4 10 \
                        --mca btl_base_debug 1 --mca oob_tcp_debug 1 --mca plm_base_verbose 5 --mca orte_base_help_aggregate 0 \
                        $disa_bench $i $duration $critical $server_ip $j 0 $nclients $client_rescum_file $client_ressingle_file $nlocks \
                        2>> $client_log_dir/nclients$n_clients"_nthreads"$i.log

                        # -mca btl openib,self \
                        # --mca btl_tcp_inf_include 10.233.0.10/24,10.233.0.11/24,10.233.0.20/24,10.233.0.14/24,10.233.0.15/24 \
                        # --mca btl_openib_allow_ib true \
                        # --mca btl_openib_cpc_include udcm \
                        # --mca btl_openib_cpc_exclude udcm \
                        # --mca mpi_debug 1 \
                        # --mca oob_tcp_if_include enp3s0 \
                        # --report-bindings --mca mpi_add_procs_verbose 1 --mca mpi_btl_base_verbose 1 \
                        # mpirun -np $nclients -x LD_PRELOAD=$client_so \
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