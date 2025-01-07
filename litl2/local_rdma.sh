# sudo modprobe rdma_rxe
# sudo rxe_cfg add wlp5s0
# ibv_devices

sudo modprobe siw
sudo rdma link add siw0 type siw netdev wlp5s0
sudo rdma link add siw0_lo type siw netdev lo
ibv_devices
rdma -d link