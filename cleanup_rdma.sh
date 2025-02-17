for pid in $(sudo lsof | grep infiniband | awk '{print $2}' | sort -u); do
    echo "Killing process $pid using RDMA resources..."
    sudo kill -9 "$pid"
done
sudo rdma resource show mr | awk '{print $12}' | sort -u | xargs -r sudo kill -9
sudo rdma resource show mr | awk '{print $16}' | sort -u | xargs -r sudo kill -9
sudo rdma resource show qp | awk '{print $14}' | sort -u | xargs -r sudo kill -9
# sudo rdma resource show pd | awk '{print $12}' | sort -u | xargs -r sudo kill -9
sudo pkill -P $$ 
echo "CLEANUP DONE"