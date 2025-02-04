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

duration=5
critical=1000
runs=1
mem_runs=1

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
        orig_rescum_dir="$PWD/results/orig/$impl/cum/$microb"
        orig_ressingle_dir="$PWD/results/orig/$impl/single/$microb"
        mkdir -p "$orig_rescum_dir" 
        mkdir -p "$orig_ressingle_dir" 

        for i in 1
        do
            orig_rescum_file="$orig_rescum_dir/nclients1_nthreads$i.csv"
            orig_ressingle_file="$orig_ressingle_dir/nclients1_nthreads$i.csv"
            echo $client_filecum_header > "$orig_rescum_file"
            echo $client_filesingle_header > "$orig_ressingle_file"


            echo "START MICROBENCH $microb CLIENT WITH $i THREADS"
            LD_PRELOAD=$orig_so ./main_orig -t $i -d $duration -m $j -f $orig_rescum_file -g $orig_ressingle_file -r $runs -e $mem_runs
        done
    done
done

















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