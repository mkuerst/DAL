#!/bin/bash

echo "Cleaning up RDMA resources on $(hostname)..."

# Destroy queue pairs (QP)
for qp in $(rdma resource show qp | awk '{print $1}'); do
    rdma resource delete qp $qp
done

# Destroy completion queues (CQ)
for cq in $(rdma resource show cq | awk '{print $1}'); do
    rdma resource delete cq $cq
done

# Destroy memory regions (MR)
for mr in $(rdma resource show mr | awk '{print $1}'); do
    rdma resource delete mr $mr
done

# Reset RDMA device
echo 1 | sudo tee /sys/class/infiniband/mlx5_0/device/reset

echo "RDMA cleanup complete on $(hostname)"
