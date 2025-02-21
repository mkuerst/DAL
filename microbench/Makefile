include ../litl2/Makefile.config

CYCLE_PER_US:=$(shell cat "/proc/cpuinfo" | grep "MHz" | head -1 | awk '{printf "%dL\n", $$4}')
CACHELINE_SIZE:=$(shell cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size)
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

FLAGS=-I../litl2/include/ -I../litl2/src/ -I/usr/include/rdma/ \
-I../Sherman/include/ -I../Sherman/test/ \
-I/home/kumichae/my_libs/rdma-core/build/include/rdma \
-g -Wall -${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -DCACHELINE_SIZE=${CACHELINE_SIZE} -DBOOST_COROUTINES_NO_DEPRECATION_WARNING\
-D$(DE) -D$(CORRECTNESS)\
-L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -libverbs -lnuma -pthread\
-L../Sherman/build/  -lcityhash -lmemcached -lboost_system -lboost_coroutine\
# -lsherman \
# -lsherman_litlHoOcmBw \
# -lsherman_litl \
# -lsherman_shermanLock \
# -lsherman_shermanHo \
# -lsherman_litlHo \

.PHONY: all target

all: target


target:
	@echo "USING CYCLE_PER_US = $(CYCLE_PER_US) MHz"
	@echo "USING CACHELINE_SIZE = $(CACHELINE_SIZE) Bytes"
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanLock.a -o microbench_shermanLock ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHo.a -o microbench_shermanHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHod.a -o microbench_shermanHod ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman.a -o microbench_sherman ${FLAGS}

# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litl.a -o microbench_litl ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHo.a -o microbench_litlHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHod.a -o microbench_litlHod ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHoOcmBw.a -o microbench_litlHoOcmBw ${FLAGS}

	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman.a -o appbench_sherman ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanLock.a -o appbench_shermanLock ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHo.a -o appbench_shermanHo ${FLAGS}

	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litl.a -o appbench_litl ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHo.a -o appbench_litlHo ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHoOcmBw.a -o appbench_litlHoOcmBw ${FLAGS}

clean:
	rm microbench_* appbench_*
