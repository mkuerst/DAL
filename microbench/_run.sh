#!/bin/sh
 
cleanup_exit() {
    echo ""
    echo "Cleaning up..."
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
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -SIGINT "$pid"
    done
    sleep 3
    pkill -P $$ 
    echo "CLEANUP DONE"
}
cleanup

trap cleanup_exit SIGINT
trap cleanup_exit SIGTERM
trap cleanup_exit SIGKILL
trap cleanup_exit SIGHUP


REMOTE_USER="root"
REMOTE_SERVER="node1"
REMOTE_CLIENTS=("node1" "node2" "node3" "node4" "node5")
server_ip=10.10.1.1


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
disa_bench="$PWD/main_disa_"


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
rm -rf server_logs/
rm -rf client_logs/

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
duration=10
runNR=1
mnNR=1
nodeNRs=(3)
threadNRs=(16)
lockNRs=(512)
bench_idxs=(0)

for impl_dir in "$BASE"/original/*
do
    for opt in ${opts[@]}
    do
        impl=$(basename $impl_dir)
        impl=${impl%.so}
        client_opt_suffix=_client_$opt.so
        client_so=${client_libs_dir}${impl}$client_opt_suffix
        server_so=${server_libs_dir}${impl}$server_suffix
        for mode in ${bench_idxs[@]}
        do
            microb="${microbenches[$mode]}"
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

            for nodeNR in ${nodeNRs[@]}
            do
                for threadNR in ${threadNRs[@]}
                do
                    client_rescum_file="$client_rescum_dir"/nclients$nclients"_nthreads"$i.csv
                    client_ressingle_file="$client_ressingle_dir"/nclients$nclients"_nthreads"$i.csv
                    server_res_file="$server_res_dir"/nclients$nclients"_nthreads"$i.csv
                    orig_res_file="$orig_res_dir/nthread_$i.csv"
                    echo $client_filecum_header > "$client_rescum_file"
                    echo $client_filesingle_header > "$client_ressingle_file"
                    echo $server_file_header > "$server_res_file"

                    for lockNR in ${lockNRs[@]}
                    do

                        echo "START MICROBENCH $impl $microb $opt $nodeNR Nodes & $threadNR T & $lockNR L & $duration s"
                        dsh -M -f <(head -n $nodeNR ./clients.txt) -c \
                        "sudo $disa_bench -t $threadNR -d $duration -m $mode -n $nodeNR -f $client_rescum_file -g $client_ressingle_file -l $lockNR -r $runNR -s $mnNR 2>&1"
                        # "sudo LD_PRELOAD=$client_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $nclients -f $client_rescum_file -g $client_ressingle_file -l $nlocks -r $runs -e $mem_runs"

                        cleanup
                    done
                done
            done
        done
    done
done

pkill -u $USER ssh-agent 
cleanup_exit