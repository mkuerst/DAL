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

.PHONY: all target

all: target


target:
	@echo "USING CYCLE_PER_US = $(CYCLE_PER_US) MHz"
	@echo "USING CACHELINE_SIZE = $(CACHELINE_SIZE) Bytes"
# echo "" > ../litl2/lib/original/shermanLock



# APPBENCH
# LITL
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litl.a -o appbench_litl ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHod.a -o appbench_litlHod ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlOcm.a -o appbench_litlOcm ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHo.a -o appbench_litlHo ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlRfaa.a -o appbench_litlRfaa ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHoOcmBw.a -o appbench_litlHoOcmBw ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHodOcmBw.a -o appbench_litlHodOcmBw ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlBw.a -o appbench_litlBw ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_litlHodRfaa.a -o appbench_litlHodRfaa ${FLAGS}
# SHERMAN
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_sherman.a -o appbench_sherman ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanGo.a -o appbench_shermanGo ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHo.a -o appbench_shermanHo ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanOcm.a -o appbench_shermanOcm ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHod.a -o appbench_shermanHod ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanRfaa.a -o appbench_shermanRfaa ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHoOcmBw.a -o appbench_shermanHoOcmBw ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHodOcmBw.a -o appbench_shermanHodOcmBw ${FLAGS}
	g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanBw.a -o appbench_shermanBw ${FLAGS}
# g++ -std=c++17 appbench.cpp ../Sherman/build/libsherman_shermanHodRfaa.a -o appbench_shermanHodRfaa ${FLAGS}

#MICROBENCH
#LITL
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litl.a -o microbench_litl ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHo.a -o microbench_litlHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHod.a -o microbench_litlHod ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlRfaa.a -o microbench_litlRfaa ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHodOcm.a -o microbench_litlHodOcm ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_litlHodOcmRfaa.a -o microbench_litlHodOcmRfaa ${FLAGS}
#SHERMAN
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanGo.a -o microbench_shermanGo ${FLAGS}
	g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_sherman.a -o microbench_sherman ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHo.a -o microbench_shermanHo ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHod.a -o microbench_shermanHod ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanRfaa.a -o microbench_shermanRfaa ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHodOcm.a -o microbench_shermanHodOcm ${FLAGS}
# g++ -std=c++17 microbench.cpp ../Sherman/build/libsherman_shermanHodOcmRfaa.a -o microbench_shermanHodOcmRfaa ${FLAGS}





clean:
	rm microbench_* appbench_*
