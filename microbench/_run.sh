#!/bin/sh
# args: duration(s), cs(us), 

#LOCAL
# REMOTE_USER="mihi"
# REMOTE_HOST="localhost"
# server_ip=10.5.12.168
# HOME
# server_ip=192.168.1.70

#CLUSTER
REMOTE_USER="kumichae"
REMOTE_HOST="r630-12"
server_ip=10.233.0.21
rdma_ip=0.0.0.0

eval "$(ssh-agent -s)"
ssh_key="/home/mihi/.ssh/id_ed25519_localhost"
ssh-copy-id "$REMOTE_USER@$REMOTE_HOST"

ssh-add $ssh_key  
# PATHS
BASE="$PWD/../litl2/lib"
server_logpath="$PWD/server_logs/"
client_logpath="$PWD/client_logs/"
tcp_server_app="$PWD/../litl2/tcp_server"
rdma_server_app="$PWD/../litl2/rdma_server"
client_suffix="_client.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
client_libs_dir=$BASE"/client/"
orig_libs_dir=$BASE"/original/"

disa_bench="$PWD/../microbench/main_disa"

microbenches=("empty_cs" "lat" "mem")
client_ids=(0)
num_clients=${#client_ids[@]}

# MICROBENCH INPUTS
nthreads=$(nproc)
# nsockets=$(lscpu | grep "^Socket(s)" | awk '{print $2}')
# ncpu=$(lscpu | grep "^Core(s) per socket" | awk '{print $4}')
# nnodes=$(lscpu | grep -oP "NUMA node\(s\):\s+\K[0-9]+")
# echo "nthreads: $nthreads | nsockets: $nsockets | cpu_per_socket: $ncpu | nnodes: $nnodes"
duration=1
critical=1000

client_file_header="tid,loop_in_cs,lock_acquires,lock_hold(ms),total_duration(s),wait_acq(ms),wait_rel(ms),array_size(B),lat_lock_hold(ms),lat_wait_acq(ms),lat_wait_rel(ms)"
server_file_header="tid,wait_acq(ms),wait_rel(ms)"

rm -rf server_logs/
rm -rf client_logs/
for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    client_so=${client_libs_dir}${impl}$client_suffix
    server_so=${server_libs_dir}${impl}$server_suffix
    orig_so=${orig_libs_dir}${impl}.so
    for j in 0
    do
        microb="${microbenches[$j]}"
        server_res_dir="./results/disaggregated/server/$impl/$microb"
        orig_res_dir="./results/non_disaggregated/$impl/$microb"
        server_log_dir="$server_logpath/$impl/$microb"
        client_log_dir="$client_logpath/$impl/$microb"
        mkdir -p "$server_res_dir" 
        mkdir -p "$orig_res_dir" 
        mkdir -p "$server_log_dir"
        mkdir -p "$client_log_dir"

        # for ((i=1; i<=nthreads; i*=2))
        for i in 1
        do
            server_res_file="$server_res_dir"/nthread_"$i".csv
            orig_res_file="$orig_res_dir/nthread_$i.csv"
            echo $client_file_header > "$orig_res_file"
            echo $server_file_header > "$server_res_file"
            server_session="server_$i"

            echo "START $impl SERVER FOR $i THREADS PER CLIENT"
            # tmux new-session -d -s "$server_session" "ssh $REMOTE_USER@$REMOTE_HOST $rdma_server_app -t $i -a $server_ip >> $server_res_file 2>> $server_log_dir/server_$i.log"
            tmux new-session -d -s "$server_session" "ssh $REMOTE_USER@$REMOTE_HOST $tcp_server_app $i $j $num_clients >> $server_res_file 2>> $server_log_dir/server_$i.log"
            # $rdma_server_app -t $i -a $server_ip >> $server_res_file 2>> $server_log_dir/server_$i.log

            sleep 3
            client_res_dir="./results/disaggregated/client/$impl/$microb"
            client_res_file="$client_res_dir"/nthread_"$i".csv
            echo $client_file_header > "$client_res_file"
            mkdir -p "$client_res_dir" 
            for c in $client_ids
            do
                client_session="client_$impl"
                echo "START MICROBENCH $microb CLIENT $c WITH $i THREADS"
                tmux new-session -d -s "$client_session"_"$c" "ssh $REMOTE_USER@$REMOTE_HOST LD_PRELOAD=$client_so $disa_bench $i $duration $critical $server_ip $j $c \
                >> $client_res_file 2>> $client_log_dir/client$c_$i.log; tmux wait-for -S done_${impl}_$c"
                # LD_PRELOAD=$client_so ./main_disa $i $duration $critical $server_ip $j >> $client_res_file
            done
            for c in $client_ids
            do
                tmux wait-for done_${impl}_$c
            done
            # tmux kill-session -t $server_session
            tmux kill-server
        done
    done
done

pkill -u $USER ssh-agent 

# lsof | grep '.nfs'
# lsof -iTCP -sTCP:LISTEN
# lsof -i :20886
# kill -9 pid
# grep flags /proc/cpuinfo | grep -q " ida " && echo Turbo mode is on || echo Turbo mode is off

# LD_PRELOAD=/home/kumichae/DAL/litl2/lib/client/libcbomcs_spinlock_client.so /home/kumichae/DAL/microbench/main 1 2 1000 1 1
# /home/kumichae/DAL/litl2/tcp_server 1 1 1