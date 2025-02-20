for pid in $(sudo lsof | grep infiniband | awk '{print $2}' | sort -u); do
    echo "Killing process $pid using RDMA resources..."
    sudo kill -9 "$pid"
done
sudo rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9
sudo rdma resource show mr | awk '{print $16}' | sort -u | xargs -r sudo kill -9
sudo rdma resource show qp | awk '{print $14}' | sort -u | xargs -r sudo kill -9
sudo rdma resource show qp | awk '{print $16}' | sort -u | xargs -r sudo kill -9
# sudo rdma resource show pd | awk '{print $12}' | sort -u | xargs -r sudo kill -9
ps aux | grep 'appbench_*' | grep -v 'grep' | awk '{print $2}' | xargs -r sudo kill -9
ps aux | grep 'microbench_*' | grep -v 'grep' | awk '{print $2}' | xargs -r sudo kill -9

sudo pkill -P $$ 
pkill -9 ibv_
pkill -9 rping
pkill -9 perftest
pkill -9 rdma
pkill -9 infiniband
# echo 1 | sudo tee /sys/class/infiniband/mlx5_2/device/reset