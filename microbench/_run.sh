#!/bin/sh
 
cleanup_exit() {
    echo ""
    echo "Cleaning up..."
    pkill -P $$ 
    for pid in $(lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        kill -9 "$pid"
    done
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9"
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo rdma resource show mr | awk '{print $16}' | sort -u | xargs -r sudo kill -9"
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
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9"
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo rdma resource show mr | awk '{print $16}' | sort -u | xargs -r sudo kill -9"
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

SSH_OPTIONS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"


# PATHS
BASE="$PWD/../litl2/lib"
server_logpath="$PWD/server_logs"
cn_logpath="$PWD/cn_logs"
cn_suffix="_cn.so"
server_suffix="_server.so"
server_libs_dir=$BASE"/server/"
cn_libs_dir=$BASE"/cn/"
llock_libs_dir=$BASE"/original/"
pthread_so="$PWD/../litl2/lib/original/libpthreadinterpose_original.so"


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

comm_prot=rdma

# MICROBENCH INPUTS
# opts=("shermanLock" "shermanHo" "sherman" "litl" "litlHo" "litlHoOcmBw")
opts=("litlHoOcmBw")
microbenches=("empty_cs" "mlocks" "correctness")
duration=20
runNR=5
mnNR=1
zipfan=0
nodeNRs=(2)
threadNRs=(32)
lockNRs=(512)
bench_idxs=(1)

sudo rm -rf logs/
mkdir -p results/
sudo chown -R mkuerst:dal-PG0 /nfs/

for opt in ${opts[@]}
do
    mb_exe="$PWD/microbench_$opt"
    if echo "$opt" | grep -q "sherman"; then
        for mode in ${bench_idxs[@]}
        do
            microb="${microbenches[$mode]}"
            cn_tp_dir="$PWD/results/cn/tp/$comm_prot/$microb/$opt/sherman"
            cn_lat_dir="$PWD/results/cn/lat/$comm_prot/$microb/$opt/sherman"
            log_dir="$PWD/logs/$comm_prot/$microb/$opt/sherman"
            mkdir -p "$cn_tp_dir" 
            mkdir -p "$cn_lat_dir" 
            mkdir -p "$log_dir"

            for nodeNR in ${nodeNRs[@]}
            do
                for threadNR in ${threadNRs[@]}
                do
                    cn_tp_file="$cn_tp_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    cn_lat_file="$cn_lat_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                    echo $cn_tp_header > "$cn_tp_file"
                    echo $cn_lat_header > "$cn_lat_file"

                    log_file="$log_dir"/nodeNR$nodeNR"_threadNR"$threadNR.log

                    for lockNR in ${lockNRs[@]}
                    do

                        for ((run = 0; run < runNR; run++)); do
                            echo "START MICROBENCH $microb | $opt $impl | $nodeNR Ns & $threadNR Ts & $lockNR Ls & $duration s & RUN $run"
                            dsh -M -f <(head -n $nodeNR ./nodes.txt) -o "-o StrictHostKeyChecking=no" -c \
                            "sudo $mb_exe \
                            -t $threadNR \
                            -d $duration \
                            -m $mode \
                            -n $nodeNR \
                            -f $cn_tp_file \
                            -g $cn_lat_file \
                            -l $lockNR \
                            -r $run \
                            -s $mnNR \
                            -z $zipfan \
                            2>> $log_file"
                            # "sudo LD_PRELOAD=$cn_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $ncns -f $cn_rescum_file -g $cn_ressingle_file -l $nlocks -r $runs -e $mem_runs"
                            # 2>&1

                            cleanup
                        done
                    done
                done
            done
        done
    else
        for impl_dir in "$BASE"/original/*
        do
            impl=$(basename $impl_dir)
            impl=${impl%.so}
            llock_so=${llock_libs_dir}${impl}.so
            for mode in ${bench_idxs[@]}
            do
                microb="${microbenches[$mode]}"
                cn_tp_dir="$PWD/results/cn/tp/$comm_prot/$microb/$opt/$impl"
                cn_lat_dir="$PWD/results/cn/lat/$comm_prot/$microb/$opt/$impl"
                log_dir="$PWD/logs/$comm_prot/$microb/$opt/sherman"
                mkdir -p "$cn_tp_dir" 
                mkdir -p "$cn_lat_dir" 
                mkdir -p "$log_dir" 

                for nodeNR in ${nodeNRs[@]}
                do
                    for threadNR in ${threadNRs[@]}
                    do
                        cn_tp_file="$cn_tp_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                        cn_lat_file="$cn_lat_dir"/nodeNR$nodeNR"_threadNR"$threadNR.csv
                        echo $cn_tp_header > "$cn_tp_file"
                        echo $cn_lat_header > "$cn_lat_file"

                        log_file="$log_dir"/nodeNR$nodeNR"_threadNR"$threadNR.log

                        for lockNR in ${lockNRs[@]}
                        do

                            for ((run = 0; run < runNR; run++)); do
                                echo "START MICROBENCH $microb | $opt $impl | $nodeNR Ns & $threadNR Ts & $lockNR Ls & $duration s & RUN $run"
                                dsh -M -f <(head -n $nodeNR ./nodes.txt) -o "-o StrictHostKeyChecking=no" -c \
                                "sudo LD_PRELOAD=$llock_so $mb_exe \
                                -t $threadNR \
                                -d $duration \
                                -m $mode \
                                -n $nodeNR \
                                -f $cn_tp_file \
                                -g $cn_lat_file \
                                -l $lockNR \
                                -r $run \
                                -s $mnNR \
                                -z $zipfan \
                                2>> $log_file"
                                # "sudo LD_PRELOAD=$cn_so $disa_bench -t $i -d $duration -s $server_ip -p $p_ips -m $j -c $ncns -f $cn_rescum_file -g $cn_ressingle_file -l $nlocks -r $runs -e $mem_runs"
                                # 2>&1"
                                cleanup
                            done
                        done
                    done
                done
            done
        done
    fi
done

pkill -u $USER ssh-agent 
cleanup_exit