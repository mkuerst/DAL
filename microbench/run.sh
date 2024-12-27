#!/bin/sh

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
microbenches=("empty_cs2n" "empty_cs1n" "lat" "mem2n" "mem1n")

duration=1
critical=1000
use_nodes=1

client_file_header="tid,loop_in_cs,lock_acquires,lock_hold(ms),total_duration(s),wait_acq(ms),wait_rel(ms),lwait_acq, lwait_rel,gwait_acq,gwait_rel,glock_tries,array_size(B),client_id,run"
server_file_header="tid,wait_acq(ms),wait_rel(ms)"

for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    orig_so=${orig_libs_dir}${impl}.so
    for j in 1
    do
        microb="${microbenches[$j]}"
        orig_res_dir="./results/orig/$impl/$microb"
        mkdir -p "$orig_res_dir" 

        for i in 16
        do
            orig_res_file="$orig_res_dir/nthread_$i.csv"
            echo $client_file_header > "$orig_res_file"


            echo "START MICROBENCH $microb CLIENT WITH $i THREADS"
            LD_PRELOAD=$orig_so ./main_orig $i $duration $critical $server_ip $j $orig_res_file
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