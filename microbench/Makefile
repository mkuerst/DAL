# CYCLE_PER_US:=$(shell lscpu | grep "MHz" | awk 'NR==1 {printf "%dL\n", $$4}')
CYCLE_PER_US:=$(shell cat "/proc/cpuinfo" | grep "MHz" | head -1 | awk '{printf "%dL\n", $$4}')
CACHELINE_SIZE:=$(shell cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size)
# CYCLE_PER_US=2400L
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

CC = gcc
GCCVERSIONGTEQ5 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 5)

# u-SCL is not tested with -O3 flag for gcc version above 5 yet.
# ifeq "$(GCCVERSIONGTEQ5)" "1"
#     OFLAG=-O2
# else
#     OFLAG=-O3
# endif
OFLAG=-O2

FLAGS=-I../litl2/include/ -I../litl2/src/ -I/usr/include/rdma/ \
-I/home/kumichae/my_libs/rdma-core/build/include/rdma \
-g -Wall ${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -DCACHELINE_SIZE=${CACHELINE_SIZE} \
-L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -libverbs -lnuma

.PHONY: all target

all: target

target: ../litl2/src/utils.c ../litl2/src/rdma_common.o
	@echo "USING CYCLE_PER_US = $(CYCLE_PER_US) MHz"
	@echo "USING CACHELINE_SIZE = $(CACHELINE_SIZE) Bytes"
	mpicc main_disa.c $^ -o main_disa ${FLAGS} -DMUTEX -DTCP_SPINLOCK
	gcc main_orig.c $^ -o main_orig ${FLAGS} -DMUTEX
	mpicc mpi_test.c $^ -o mpi_test ${FLAGS} -DMUTEX -DTCP_SPINLOCK
# gcc main.c -o main_spin ${FLAGS} -DSPIN



# mutex:
# 	gcc main.c -o main ${FLAGS} -DMUTEX

# spin:
# 	gcc main.c -o main ${FLAGS} -DSPIN

clean:
	rm main

# -L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm