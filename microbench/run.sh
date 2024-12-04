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

eval "$(ssh-agent -s)"
ssh_key="/home/mihi/.ssh/id_ed25519_localhost"
ssh-add $ssh_key  
ssh-copy-id "$REMOTE_USER@$REMOTE_HOST"

# PATHS
BASE="$PWD/../litl2/lib"
logpath="$PWD/server_logs/"
tcp_server_app="$PWD/../litl2/tcp_server"
rdma_server_app="$PWD/../litl2/rdma_server"
client_suffix="_client.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
client_libs_dir=$BASE"/client/"
orig_libs_dir=$BASE"/original/"
microbenches=("empty_cs" "cs" "mem")

# MICROBENCH INPUTS
nthreads=$(nproc)
# nsockets=$(lscpu | grep "^Socket(s)" | awk '{print $2}')
# ncpu=$(lscpu | grep "^Core(s) per socket" | awk '{print $4}')
# nnodes=$(lscpu | grep -oP "NUMA node\(s\):\s+\K[0-9]+")
# echo "nthreads: $nthreads | nsockets: $nsockets | cpu_per_socket: $ncpu | nnodes: $nnodes"
duration=20
critical=1000


rm -rf server_logs/
for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    client_so=${client_libs_dir}${impl}$client_suffix
    server_so=${server_libs_dir}${impl}$server_suffix
    orig_so=${orig_libs_dir}${impl}.so
    for ((j=0; j<=0; j+=1))
    do
        microb="${microbenches[$j]}"
        client_res_dir="./results/disaggregated/client/$impl/$microb"
        server_res_dir="./results/disaggregated/server/$impl/$microb"
        orig_res_dir="./results/non_disaggregated/$impl/$microb"
        log_dir="$logpath/$impl/$microb"
        mkdir -p "$client_res_dir" 
        mkdir -p "$server_res_dir" 
        mkdir -p "$orig_res_dir" 
        mkdir -p "$log_dir"

        # for ((i=1; i<=nthreads; i+=1))
        for i in 1 16 29
        do
            client_res_file="$client_res_dir"/nthread_"$i".csv
            server_res_file="$server_res_dir"/nthread_"$i".csv
            orig_res_file="$orig_res_dir/nthread_$i.csv"
            echo "tid,loop_in_cs,lock_acquires,lock_hold(ms)" > "$client_res_file"
            echo "tid,loop_in_cs,lock_acquires,lock_hold(ms)" > "$orig_res_file"
            echo "tid,lock_impl_time(ms)" > "$server_res_file"
            # echo "START $impl SERVER with $i THREADS"
            # tmux new-session -d -s "server_"$impl"_$i" "ssh $REMOTE_USER@$REMOTE_HOST LD_PRELOAD=$server_so $rdma_server_app -t $i -a $server_ip >> $server_res_file 2>> $log_dir/server_$i.log"

            # tmux capture-pane -pt "server_"$impl"_$i"
            # gnome-terminal -- bash -c "ssh $REMOTE_USER@$REMOTE_HOST -i $ssh_key 'LD_PRELOAD=$server_so $tcp_server_app $i' >> $server_res_file"

            sleep 5
            echo "START MICROBENCH CLIENT WITH $i THREADS"
            # LD_PRELOAD=$client_so ./main_disa $i $duration $critical $server_ip $j >> $client_res_file
            LD_PRELOAD=$orig_so ./main_orig $i $duration $critical $server_ip $j >> $orig_res_file
            # tmux kill-session -t "server_"$impl"_$i"
        done
    done
done


# lsof | grep '.nfs'
# lsof -iTCP -sTCP:LISTEN
# kill -9 pid
# grep flags /proc/cpuinfo | grep -q " ida " && echo Turbo mode is on || echo Turbo mode is off

# LD_PRELOAD=/home/kumichae/DAL/litl2/lib/client/libcbomcs_spinlock_client.so /home/kumichae/DAL/microbench/main 1 2 1000 1 1
# /home/kumichae/DAL/litl2/tcp_server 1 1 1