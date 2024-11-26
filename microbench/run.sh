#!/bin/sh
# args: duration(s), cs(us), 


BASE="$(dirname "$0")/../litl/lib"
suffix=".so"
nthreads=$(nproc)
nsockets=$(lscpu | grep "^Socket(s)" | awk '{print $2}')
ncpu=$(lscpu | grep "^Core(s) per socket" | awk '{print $4}')

for impl_dir in "$BASE"/*
do
    impl=$(basename $impl_dir)
    impl=${impl%"$suffix"}
    echo running $impl
    res_dir=./results/"$impl"
    mkdir -p "$res_dir" 
    # for ((i=1; i<=2*nthreads; i+=1))
    for ((i=1; i<=2; i+=1))
    do
        res_file="$res_dir"/nthread_"$i".csv
        echo "tid,loop_in_cs,lock_acquires,lock_hold(ms)\n" > "$res_file"
        echo "START MICROBENCH WITH "$i" THREADS"
        LD_PRELOAD=$impl_dir ./main $i 3 1000 $ncpu >> $res_file
    done
done
