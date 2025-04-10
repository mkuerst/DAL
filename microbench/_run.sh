#!/bin/sh
 
cleanup() {
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sudo bash /nfs/DAL/cleanup_rdma.sh"
    sudo dsh -M -f ./nodes.txt -o "-o StrictHostKeyChecking=no" -c "sync; sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'"

    # sudo clush --hostfile ./nodes.txt "sudo bash /nfs/DAL/cleanup_rdma.sh"
    sudo pkill -P $$ 
    for pid in $(sudo lsof | grep infiniband | awk '{print $2}' | sort -u); do
        echo "Killing process $pid using RDMA resources..."
        sudo kill -9 "$pid"
    done
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
loop_in_cs,tp,lock_acquires,duration,\
glock_tries,handovers,handovers_data,array_size(B),\
nodeID,run,lockNR,la,numa,cache_misses,c_ho,c_hod,\
cnNR,mnNR,threadNR,maxHandover,colocate,zipfian"

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
lockNR,\
numa,\
cnNR,mnNR,threadNR,maxHandover,colocate,zipfian"

server_file_header="tid,wait_acq(ms),wait_rel(ms),nodeID,run"

comm_prot=rdma

# MICROBENCH INPUTS
opts=("Hod" "Bw" "HodOcmBw" )
# opts=(".")

microbenches=("emptyCS" "mlocks" "singleMachine" "kvs")
duration=10
runNR=2
zipfian=0.99
chipSize=128
dsmSize=8

mnNRs=(4)
nodeNRs=(8)
threadNRs=(16)
lockNRs=(1024 128 8)
bench_idxs=(3)
pinnings=(1)
mHos=(16)
colocate=0


cn_tp_dir="$PWD/results/tp"
cn_lat_dir="$PWD/results/lat"
cn_lock_dir="$PWD/results/ldist"

sudo rm -rf logs/
mkdir -p $cn_tp_dir
mkdir -p $cn_lat_dir
mkdir -p $cn_lock_dir
mkdir -p $PWD/results/plots/lat
mkdir -p $PWD/results/plots/tp
mkdir -p $PWD/results/plots/ldist

sudo chown -R mkuerst:dal-PG0 /nfs/

for opt in ${opts[@]}
do
    # for impl_dir in "$BASE"/original/*
    for impl_dir in "$BASE"/../debug/*
    do
        impl=$(basename $impl_dir)
        impl=${impl%.so}
        llock_so=${llock_libs_dir}${impl}.so
        for mode in ${bench_idxs[@]}
        do
            opt="${opt//./}"
            litl_opt=litl$opt
            sherman_opt=sherman$opt
            opt=$litl_opt
            if echo "$impl" | grep -q "shermanLock"; then
                llock_so=""
                opt=$sherman_opt
            fi

            mb_exe="$PWD/microbench_$opt"
            microb="${microbenches[$mode]}"
            if echo "$microb" | grep -q "kvs"; then
                mb_exe="$PWD/appbench_$opt"
            fi


            impl="${impl//_/.}"
            opt="${opt//litl/}"
            opt="${opt//sherman/}"
            res_suffix="$comm_prot"_"$microb"_"$impl"_"$opt"
            cn_lat_file="$cn_lat_dir"/"$res_suffix".csv
            cn_tp_file="$cn_tp_dir"/"$res_suffix".csv

            if [ ! -e "$cn_tp_file" ]; then
                echo $cn_tp_header > "$cn_tp_file"
                echo $cn_lat_header > "$cn_lat_file"
            fi



            log_dir="$PWD/logs/$comm_prot/$microb/$opt/$impl"
            mkdir -p "$log_dir"

            for nodeNR in ${nodeNRs[@]}
            do
                for threadNR in ${threadNRs[@]}
                do

                    log_file="$log_dir"/nodeNR$nodeNR"_threadNR"$threadNR.log

                    for lockNR in ${lockNRs[@]}
                    do
                        for pinning in ${pinnings[@]}
                        do
                            for mnNR in ${mnNRs[@]}
                            do
                                for mHo in ${mHos[@]}
                                do
                                    for ((run = 0; run < runNR; run++)); do
                                        cn_lock_file="$cn_lock_dir"/"$res_suffix"_nodeNR"$nodeNR"_threadNR"$threadNR"_mnNR"$mnNR"_lockNR"$lockNR"_NUMA"$pinning"_mHo"$mHo"_colocate"$colocate"_r"$run".csv
                                        > "$cn_lock_file"
                                        echo "BENCHMARK $microb | $impl.$opt | $nodeNR Ns | $threadNR Ts | $lockNR Ls | $duration s | RUN $run"
                                        echo "pinning $pinning | DSM $dsmSize GB | $mnNR MNs | chipSize $chipSize KB | colocate $colocate | zipfian $zipfian |"
                                        clush --hostfile <(head -n $nodeNR ./nodes.txt) \
                                        "sudo bash -c 'ulimit -c unlimited' && \
                                        sudo LD_PRELOAD=$llock_so $mb_exe \
                                        -t $threadNR \
                                        -d $duration \
                                        -m $mode \
                                        -n $nodeNR \
                                        -f $cn_tp_file \
                                        -g $cn_lat_file \
                                        -h $cn_lock_file \
                                        -l $lockNR \
                                        -r $run \
                                        -s $mnNR \
                                        -z $zipfian \
                                        -p $pinning \
                                        -c $chipSize \
                                        -y $dsmSize \
                                        -x $mHo \
                                        -q $colocate"
                                        # 2>&1" 
                                        # 2>> $log_file"
                                        cleanup
                                        sleep 3
                                    done
                                done
                            done
                        done
                    done
                done
            done
        done
    done
done

pkill -u $USER ssh-agent 
cleanup 1