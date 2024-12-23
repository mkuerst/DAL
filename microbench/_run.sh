#!/bin/sh
TCP_PORT0=8000
TCP_PORT1=8022
TCP_PORT2=8080
RDMA_PORT=20051
 
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
REMOTE_SERVER="r630-06"
server_ip=10.233.0.15
rdma_ip=0.0.0.0

REMOTE_CLIENTS=("r630-01" "r630-02" "r630-05" "r630-06" "r630-09")
REMOTE_CLIENT="r630-01"

eval "$(ssh-agent -s)"
# ssh_key="/home/mihi/.ssh/id_ed25519_localhost"
# ssh-add $ssh_key  
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
orig_libs_dir=$BASE"/original/"
spinlock_so="$server_libs_dir/libspinlock_original_server.so"
disa_bench="$PWD/main_disa"


client_file_header="tid,loop_in_cs,lock_acquires,lock_hold(ms),total_duration(s),wait_acq(ms),wait_rel(ms),lwait_acq, lwait_rel,gwait_acq,gwait_rel,glock_tries,array_size(B),client_id,run"
server_file_header="tid,wait_acq(ms),wait_rel(ms),client_id,run"

# MICROBENCH INPUTS
duration=1
critical=1000

rm -rf server_logs/
rm -rf client_logs/

microbenches=("empty_cs" "lat" "mem_2nodes")
client_ids=(0 1 2 3 4 5 6 7 8 9)
n_clients=(2 3)
# num_clients=${#client_ids[@]}

for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    client_so=${client_libs_dir}${impl}$client_suffix
    server_so=${server_libs_dir}${impl}$server_suffix
    orig_so=${orig_libs_dir}${impl}.so
    for j in 2
    do
        microb="${microbenches[$j]}"
        client_res_dir="./results/disaggregated/client/$impl/$microb"
        server_res_dir="./results/disaggregated/server/$impl/$microb"
        orig_res_dir="./results/non_disaggregated/$impl/$microb"
        server_log_dir="$server_logpath/$impl/$microb"
        client_log_dir="$client_logpath/$impl/$microb"
        mkdir -p "$client_res_dir" 
        mkdir -p "$server_res_dir" 
        mkdir -p "$orig_res_dir" 
        mkdir -p "$server_log_dir"
        mkdir -p "$client_log_dir"

        # for ((i=1; i<=nthreads; i*=2))
        for nclients in ${n_clients[@]}
        do
            for i in 16 32
            do
                client_res_file="$client_res_dir"/nclients$nclients"_nthreads"$i.csv
                server_res_file="$server_res_dir"/nclients$nclients"_nthreads"$i.csv
                orig_res_file="$orig_res_dir/nthread_$i.csv"
                echo $client_file_header > "$client_res_file"
                echo $server_file_header > "$server_res_file"
                echo $client_file_header > "$orig_res_file"

                server_session="server_$i"
                echo "START $impl SERVER FOR $i THREADS PER CLIENT"
                tmux new-session -d -s "$server_session" "ssh $REMOTE_USER@$REMOTE_SERVER $rdma_server_app -t $nclients -a $server_ip >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                # tmux new-session -d -s "$server_session" \
                # "ssh $REMOTE_USER@$REMOTE_SERVER LD_PRELOAD=$spinlock_so $tcp_server_app $i $j $nclients >> $server_res_file 2>> $server_log_dir/server_$n_clients"_"$i.log" & SERVER_PID=$!

                # LD_PRELOAD=$spinlock_so $tcp_server_app $i $j $num_clients & >> $server_res_file 2>> $server_log_dir/server_${n_clients}_$i.log
                # SERVER_PID=$!

                # $rdma_server_app -t $i -a $server_ip >> $server_res_file 2>> $server_log_dir/server_$i.log
                # sleep 3

                # strace -e trace=connect -o mpi.log -f 

                # ============= MPIRUN ========================================================
                mpirun --hostfile ./clients.txt -np $nclients -x LD_PRELOAD=$client_so \
                --mca oob_tcp_dynamic_ipv4_ports 8000,8080 \
                --mca btl_tcp_port_min_v4 8022 --mca btl_tcp_port_range_v4 5 \
                $disa_bench $i $duration $critical $server_ip $j 0 $nclients \
                >> $client_res_file 2>> $client_log_dir/nclients$n_clients"_nthreads"$i.log
                # --mca mpi_debug 1 \
                # --report-bindings --mca mpi_add_procs_verbose 1 --mca mpi_btl_base_verbose 1 \
                # --mca btl_base_debug 1 --mca oob_tcp_debug 1 --mca plm_base_verbose 5 --mca orte_base_help_aggregate 0 \
                # mpirun -np $nclients -x LD_PRELOAD=$client_so \
                # ============= MPIRUN ========================================================

                # --mca btl_tcp_inf_include 10.233.0.0/24,10.233.0.10/24,10.233.0.20/24 \
                # --mca oob_tcp_if_include enp3s0 \
                # --mca oob_tcp_if_include 10.233.0.0/24,10.233.0.10/24,10.233.0.20/24 \
                # --mca oob_tcp_if_exclude lo,eno3,eno1,eno4,eno2,docker0 \
                # --mca btl_tcp_if_exclude lo,eno3,eno1,eno4,eno2,docker0 \
                # --mca oob_tcp_dynamic_ipv4_ports 8082 \

                # --mca oob_tcp_port_min_v4 8090 --mca oob_tcp_port_range_v4 5 \
                # --mca oob_tcp_static_ports 8082 --mca btl_tcp_static_ports 8085 \

                # for ((c=0; c<nclients; c++));
                # do
                #     echo "START MICROBENCH $microb CLIENT $c WITH $i THREADS"
                #     # client_session="client_$impl"
                #     tmux new-session -d -s "${client_session}_${c}" \
                #     "ssh $REMOTE_USER@${REMOTE_CLIENTS[$c]} LD_PRELOAD=$client_so $disa_bench $i $duration $critical $server_ip $j $c $nclients \
                #     >> $client_res_file 2>> $client_log_dir/client${c}_${i}.log; \
                #     tmux wait-for -S done_${impl}_${c}"
                #     # LD_PRELOAD=$client_so $disa_bench $i $duration $critical $server_ip $j $c $nclients \
                #     # >> $client_res_file 2>> $client_log_dir/client${c}_${i}.log 

                # done
                # for ((c=0; c<nclients; c++));
                # do
                #     tmux wait-for done_${impl}_${c}
                # done

                cleanup

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