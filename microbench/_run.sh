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
REMOTE_CLIENTS=("node1" "node2" "node3" "node4" "node5")


# PATHS
BASE="$PWD/../litl2/lib"
server_logpath="$PWD/server_logs"
client_logpath="$PWD/client_logs"
client_suffix="_client.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
client_libs_dir=$BASE"/client/"
spinlock_so="$server_libs_dir/libspinlock_original_server.so"
disa_bench="$PWD/main_disa_"


client_filecum_header="tid,\
loop_in_cs,lock_acquires,duration,\
glock_tries,array_size(B),nodeID,run,lockNR"

client_filesingle_header="lock_hold,\
lwait_acq,\
lwait_rel,\
gwait_acq,\
gwait_rel,\
data_read,\
data_write,\
array_size,\
nodeID,\
run,\
lockNR"

server_file_header="tid,wait_acq(ms),wait_rel(ms),client_id,run"
rm -rf server_logs/
rm -rf client_logs/

comm_prot=rdma

# MICROBENCH INPUTS
opts=("spinlock")
microbenches=("empty_cs" "mlocks" "correctness")
duration=5
runNR=2
mnNR=1
nodeNRs=(3)
threadNRs=(32)
lockNRs=(1)
bench_idxs=(1)

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
            client_tp_dir="$PWD/results/$comm_prot/$opt/cn/tp/$impl/$microb"
            client_lat_dir="$PWD/results/$comm_prot/$opt/cn/lat/$impl/$microb"
            server_res_dir="./results/$comm_prot/$opt/server/tp/$impl/$microb"
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
                    client_tp_file="$client_tp_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    client_lat_file="$client_lat_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    server_res_file="$server_res_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    orig_res_file="$orig_res_dir/threadNR$threadNR.csv"
                    echo $client_tp_header > "$client_tp_file"
                    echo $client_lat_header > "$client_lat_file"
                    echo $server_file_header > "$server_res_file"

                    for lockNR in ${lockNRs[@]}
                    do

                        for ((run = 0; run < runNR; run++)); do
                            echo "START MICROBENCH $impl $microb $opt $nodeNR Ns & $threadNR Ts & $lockNR Ls & $duration s & RUN $run"
                            dsh -M -f <(head -n $nodeNR ./nodes.txt) -c \
                            "sudo $disa_bench \
                            -t $threadNR \
                            -d $duration \
                            -m $mode \
                            -n $nodeNR \
                            -f $client_tp_file \
                            -g $client_lat_file \
                            -l $lockNR \
                            -r $run \
                            -s $mnNR 2>&1"
                            # 2>> $client_log_dir/nclients$n_clients"_nthreads"$i.log"
                            # "sudo LD_PRELOAD=$client_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $nclients -f $client_rescum_file -g $client_ressingle_file -l $nlocks -r $runs -e $mem_runs"

                            cleanup
                        done
                    done
                done
            done
        done
    done
done

pkill -u $USER ssh-agent 
cleanup_exit