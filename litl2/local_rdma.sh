sudo rdma link delete siw0
sudo rdma link delete siw0_lo

sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev wlp5s0
sudo rdma link add rxe0_lo type rxe netdev lo

# sudo modprobe siw
# sudo rdma link add siw0 type siw netdev wlp5s0
# sudo rdma link add siw0_lo type siw netdev lo

ibv_devices
rdma -d link