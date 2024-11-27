#!/bin/sh
# args: duration(s), cs(us), 

# REMOTE_USER="kumichae"
# REMOTE_HOST="r630-12"
REMOTE_USER="mihi"
REMOTE_HOST="localhost"
REMOTE_SCRIPT="/home/kumichae/DAL/litl2/tcp_server"

eval "$(ssh-agent -s)"
ssh_key="/home/mihi/.ssh/id_ed25519_localhost"
ssh-add $ssh_key  
ssh-copy-id "$REMOTE_USER@$REMOTE_HOST"

BASE="$PWD/../litl2/lib"
tcp_server_app="$PWD/../litl2/tcp_server"
client_suffix="_client.so"
server_suffix="_server.so"
nthreads=$(nproc)
nsockets=$(lscpu | grep "^Socket(s)" | awk '{print $2}')
ncpu=$(lscpu | grep "^Core(s) per socket" | awk '{print $4}')
nnodes=$(lscpu | grep -oP "NUMA node\(s\):\s+\K[0-9]+")

server_libs_dir=$BASE"/server/"
client_libs_dir=$BASE"/client/"
for impl_dir in "$BASE"/original/*
do
    impl=$(basename $impl_dir)
    impl=${impl%.so}
    client_so=${client_libs_dir}${impl}$client_suffix
    server_so=${server_libs_dir}${impl}$server_suffix
    res_dir="./results/disaggregated/$impl"
    mkdir -p "$res_dir" 
    for ((i=1; i<=nthreads; i+=1))
    # for ((i=1; i<=2; i+=1))
    do
        res_file="$res_dir"/nthread_"$i".csv
        echo "tid,loop_in_cs,lock_acquires,lock_hold(ms)" > "$res_file"
        echo "START $impl SERVER with $i THREADS"
        # tmux new-session -d "ssh $REMOTE_USER@$REMOTE_HOST -i $ssh_key 'LD_PRELOAD=$server_so $tcp_server_app $i $ncpu $nnodes'"
        gnome-terminal -- bash -c "ssh $REMOTE_USER@$REMOTE_HOST -i $ssh_key 'LD_PRELOAD=$server_so $tcp_server_app $i $ncpu $nnodes'"
        sleep 1
        echo "START MICROBENCH CLIENT WITH $i THREADS"
        LD_PRELOAD=$client_so ./main $i 3 1000 $ncpu $nnodes >> $res_file
    done
done
