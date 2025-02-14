include ../litl2/Makefile.config

CYCLE_PER_US:=$(shell cat "/proc/cpuinfo" | grep "MHz" | head -1 | awk '{printf "%dL\n", $$4}')
CACHELINE_SIZE:=$(shell cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size)
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

CC = gcc
GCCVERSIONGTEQ5 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 5)


ifeq ($(SYS_GROUP), 1)
FLAGS=-I../litl2/include/ -I../litl2/src/ -I/usr/include/rdma/ \
-I../Sherman/include/ -I../Sherman/test/ \
-I/home/kumichae/my_libs/rdma-core/build/include/rdma \
-g -Wall -${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -DCACHELINE_SIZE=${CACHELINE_SIZE} \
-DBOOST_COROUTINES_NO_DEPRECATION_WARNING \
-D$(DE) -D$(CORRECTNESS) \
-L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -libverbs -lnuma -pthread \
-lsherman_litlHoOcmBw \
-lsherman \
-lsherman_litl \
-lsherman_shermanLock \
-lsherman_shermanHo \
-lsherman_litlHo \
-L../Sherman/build/ -lboost_system -lboost_coroutine \
-I/home/kumichae/my_libs/cityhash/include \
/home/kumichae/my_libs/cityhash/lib/libcityhash.a \
-I/home/kumichae/my_libs/libmemcached-1.0.18 \
/home/kumichae/my_libs/libmemcached-1.0.18/libmemcached/.libs/libmemcached.a
else
FLAGS=-I../litl2/include/ -I../litl2/src/ -I/usr/include/rdma/ \
-I../Sherman/include/ -I../Sherman/test/ \
-g -Wall -${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -DCACHELINE_SIZE=${CACHELINE_SIZE} \
-DBOOST_COROUTINES_NO_DEPRECATION_WARNING\
-D$(DE) -D$(CORRECTNESS)\
-L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -libverbs -lnuma -pthread\
-L../Sherman/build/  -lcityhash -lmemcached -lboost_system -lboost_coroutine\
-lsherman_litlHoOcmBw \
-lsherman \
-lsherman_litl \
-lsherman_shermanLock \
-lsherman_shermanHo \
-lsherman_litlHo
endif

.PHONY: all target

all: target

target: ../litl2/src/utils.c ../litl2/src/rdma_common.o
	@echo "USING CYCLE_PER_US = $(CYCLE_PER_US) MHz"
	@echo "USING CACHELINE_SIZE = $(CACHELINE_SIZE) Bytes"
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanLock.a -o microbench_shermanLock ${FLAGS}
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHo.a -o microbench_shermanHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman.a -o microbench_sherman ${FLAGS} -DMUTEX

	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litl.a -o microbench_litl ${FLAGS}
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHo.a -o microbench_litlHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHoOcmBw.a -o microbench_litlHoOcmBw ${FLAGS} -DMUTEX

clean:
	rm main
