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

duration=30
critical=1000

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
server_file_header="tid,wait_acq(ms),wait_rel(ms)"

for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    orig_so=${orig_libs_dir}${impl}.so
    for j in 0
    do
        microb="${microbenches[$j]}"
        orig_rescum_dir="$PWD/results/orig/$impl/$microb/cum"
        orig_ressingle_dir="$PWD/results/orig/$impl/$microb/single"
        mkdir -p "$orig_rescum_dir" 
        mkdir -p "$orig_ressingle_dir" 

        for i in 1
        do
            orig_rescum_file="$orig_rescum_dir/nclients1_nthreads$i.csv"
            orig_ressingle_file="$orig_ressingle_dir/nclients1_nthreads$i.csv"
            echo $client_filecum_header > "$orig_rescum_file"
            echo $client_filesingle_header > "$orig_ressingle_file"


            echo "START MICROBENCH $microb CLIENT WITH $i THREADS"
            LD_PRELOAD=$orig_so ./main_orig $i $duration $critical $j $orig_rescum_file $orig_rescum_file
        done
    done
done