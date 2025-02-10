include ../litl2/Makefile.config

CYCLE_PER_US:=$(shell cat "/proc/cpuinfo" | grep "MHz" | head -1 | awk '{printf "%dL\n", $$4}')
CACHELINE_SIZE:=$(shell cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size)
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

CC = gcc
GCCVERSIONGTEQ5 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 5)


FLAGS=-I../litl2/include/ -I../litl2/src/ -I/usr/include/rdma/ \
-I../Sherman/include/ -I../Sherman/test/ \
-I/home/kumichae/my_libs/rdma-core/build/include/rdma \
-g -Wall -${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -DCACHELINE_SIZE=${CACHELINE_SIZE} -DBOOST_COROUTINES_NO_DEPRECATION_WARNING\
-D$(DE) -D$(CORRECTNESS)\
-L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -libverbs -lnuma -pthread\
-L../Sherman/build/  -lcityhash -lmemcached -lboost_system -lboost_coroutine\
-lsherman -lsherman_litl -lsherman_shermanLock


.PHONY: all target

all: target

target: ../litl2/src/utils.c ../litl2/src/rdma_common.o
	@echo "USING CYCLE_PER_US = $(CYCLE_PER_US) MHz"
	@echo "USING CACHELINE_SIZE = $(CACHELINE_SIZE) Bytes"
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman.a -o microbench_sherman ${FLAGS} -DMUTEX
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanLock.a -o microbench_shermanLock ${FLAGS} -DMUTEX
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litl.a -o microbench_litl ${FLAGS} -DMUTEX
# g++ -std=c++17 test_rdma_conn.cpp litl_utils.o  ../Sherman/build/libsherman.a ../litl2/src/rdma_common.o -o test_rdma_conn ${FLAGS} -DMUTEX

# gcc -c ../litl2/src/utils.c -o litl_utils.o ${FLAGS} 
# gcc main_orig.c $^ -o main_orig ${FLAGS} -DMUTEX
# mpicc main_disa.c $^ -o main_disa ${FLAGS} -DMUTEX -D$(DEBUG) -D$(COMM_PROT) -D$(MPI) -D${CORRECTNESS}

clean:
	rm main
