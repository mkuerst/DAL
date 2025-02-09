#!/bin/sh
 
cleanup_exit() {
    echo ""
    echo "Cleaning up..."
    pkill -P $$ 
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -9 "$pid"
    done
    dsh -M -f ./nodes.txt -c "rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9"
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
    dsh -M -f ./nodes.txt -c "rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9"
    pkill -P $$ 
    echo "CLEANUP DONE"
}
cleanup

trap cleanup_exit SIGINT
trap cleanup_exit SIGTERM
trap cleanup_exit SIGKILL
trap cleanup_exit SIGHUP


REMOTE_USER="root"
REMOTE_cnS=("node1" "node2" "node3" "node4" "node5")


# PATHS
BASE="$PWD/../litl2/lib"
server_logpath="$PWD/server_logs"
cn_logpath="$PWD/cn_logs"
cn_suffix="_cn.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
cn_libs_dir=$BASE"/cn/"
llock_libs_dir=$BASE"/orig/"
disa_bench="$PWD/main_disa_"


cn_tp_header="tid,\
loop_in_cs,lock_acquires,duration,\
glock_tries,array_size(B),nodeID,run,lockNR"

cn_lat_header="lock_hold,\
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

server_file_header="tid,wait_acq(ms),wait_rel(ms),nodeID,run"
rm -rf server_logs/
rm -rf cn_logs/

comm_prot=rdma

# MICROBENCH INPUTS
opts=("spinlock")
microbenches=("empty_cs" "mlocks" "correctness")
duration=5
runNR=2
mnNR=1
nodeNRs=(3)
threadNRs=(32)
lockNRs=(512)
bench_idxs=(1)

for impl_dir in "$BASE"/original/*
do
    for opt in ${opts[@]}
    do
        impl=$(basename $impl_dir)
        impl=${impl%.so}
        cn_opt_suffix=_cn_$opt.so
        cn_so=${cn_libs_dir}${impl}$cn_opt_suffix
        llock_so=${llock_libs_dir}${impl}.so
        server_so=${server_libs_dir}${impl}$server_suffix
        for mode in ${bench_idxs[@]}
        do
            microb="${microbenches[$mode]}"
            cn_tp_dir="$PWD/results/$comm_prot/$opt/cn/tp/$impl/$microb"
            cn_lat_dir="$PWD/results/$comm_prot/$opt/cn/lat/$impl/$microb"
            # server_res_dir="./results/$comm_prot/$opt/server/tp/$impl/$microb"
            # server_log_dir="$server_logpath/$impl/$opt/$microb"
            # cn_log_dir="$cn_logpath/$impl/$opt/$microb"
            mkdir -p "$cn_tp_dir" 
            mkdir -p "$cn_lat_dir" 
            # mkdir -p "$server_res_dir" 
            # mkdir -p "$server_log_dir"
            # mkdir -p "$cn_log_dir"

            for nodeNR in ${nodeNRs[@]}
            do
                for threadNR in ${threadNRs[@]}
                do
                    cn_tp_file="$cn_tp_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    cn_lat_file="$cn_lat_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    server_res_file="$server_res_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    orig_res_file="$orig_res_dir/threadNR$threadNR.csv"
                    echo $cn_tp_header > "$cn_tp_file"
                    echo $cn_lat_header > "$cn_lat_file"
                    # echo $server_file_header > "$server_res_file"

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
                            -f $cn_tp_file \
                            -g $cn_lat_file \
                            -l $lockNR \
                            -r $run \
                            -s $mnNR 2>&1"
                            # 2>> $cn_log_dir/ncns$n_cns"_nthreads"$i.log"
                            # "sudo LD_PRELOAD=$cn_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $ncns -f $cn_rescum_file -g $cn_ressingle_file -l $nlocks -r $runs -e $mem_runs"

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