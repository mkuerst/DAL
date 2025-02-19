#!/bin/sh
 
cleanup() {
    sudo pkill -P $$ 
    for pid in $(sudo lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        sudo kill -9 "$pid"
    done
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo bash /nfs/DAL/cleanup_rdma.sh"
    echo "CLEANUP DONE"
    if [[ "$1" == "1" || -n "$SIGNAL_CAUGHT" ]]; then
        echo "EXIT"
        exit 1
    fi
}

trap 'SIGNAL_CAUGHT=1; cleanup 1' SIGINT SIGTERM SIGHUP
cleanup

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
glock_tries,handovers,array_size(B),\
nodeID,run,lockNR"

cn_lat_header="lock_hold,\
lwait_acq,\
lwait_rel,\
gwait_acq,\
gwait_rel,\
data_read,\
data_write,\
end_to_end,\
array_size,\
nodeID,\
run,\
lockNR"

server_file_header="tid,wait_acq(ms),wait_rel(ms),nodeID,run"

comm_prot=rdma

# MICROBENCH INPUTS
# opts=("shermanLock" "shermanHo" "sherman" "litl" "litlHo" "litlHoOcmBw")
opts=("litl" "litlHo" "litlHoOcmBw")
microbenches=("empty_cs" "mlocks" "correctness")
duration=10
runNR=3
mnNR=4
zipfan=1
nodeNRs=(4)
threadNRs=(32)
pinning=1
chipSize=128
dsmSize=16

sudo rm -rf logs/
mkdir -p results/plots/
sudo chown -R mkuerst:dal-PG0 /nfs/

for opt in ${opts[@]}
do
    mb_exe="$PWD/appbench_$opt"
    if echo "$opt" | grep -q "sherman"; then
        cn_tp_dir="$PWD/results/cn/tp/$comm_prot/kvs/$opt/sherman"
        cn_lat_dir="$PWD/results/cn/lat/$comm_prot/kvs/$opt/sherman"
        log_dir="$PWD/logs/$comm_prot/kvs/$opt/sherman"
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

                for ((run = 0; run < runNR; run++)); do
                    echo "START APPBENCH kvs | $opt $impl | $nodeNR Ns & $threadNR Ts & $duration s & RUN $run"
                    dsh -M -f <(head -n $nodeNR ./nodes.txt) -o "-o StrictHostKeyChecking=no" -c \
                    "sudo $mb_exe \
                    -t $threadNR \
                    -d $duration \
                    -n $nodeNR \
                    -f $cn_tp_file \
                    -g $cn_lat_file \
                    -r $run \
                    -s $nodeNR \
                    -p $pinning \
                    -c $chipSize \
                    -y $dsmSize \
                    2>&1"
                    # 2>> $log_file"
                    cleanup
                done
            done
        done
    else
        for impl_dir in "$BASE"/original/*
        do
            impl=$(basename $impl_dir)
            impl=${impl%.so}
            llock_so=${llock_libs_dir}${impl}.so
            cn_tp_dir="$PWD/results/cn/tp/$comm_prot/kvs/$opt/$impl"
            cn_lat_dir="$PWD/results/cn/lat/$comm_prot/$kvs/$opt/$impl"
            log_dir="$PWD/logs/$comm_prot/kvs/$opt/$impl"
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

                    for ((run = 0; run < runNR; run++)); do
                        echo "START APPBENCH kvs | $opt $impl | $nodeNR Ns & $threadNR Ts & $duration s & RUN $run"
                        dsh -M -f <(head -n $nodeNR ./nodes.txt) -o "-o StrictHostKeyChecking=no" -c \
                        "sudo LD_PRELOAD=$llock_so $mb_exe \
                        -t $threadNR \
                        -d $duration \
                        -n $nodeNR \
                        -f $cn_tp_file \
                        -g $cn_lat_file \
                        -r $run \
                        -s $nodeNR \
                        -p $pinning \
                        -c $chipSize \
                        -y $dsmSize \
                        2>&1"
                        # 2>> $log_file"
                        cleanup
                    done
                done
            done
        done
    fi
done

pkill -u $USER ssh-agent 