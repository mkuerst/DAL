include ../Makefile.config

CACHELINE_SIZE:=$(shell cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size)
# CLUSTER
LDFLAGS=-L../obj/CLHT/external/lib -L../obj/CLHT -Wl,--whole-archive -Wl,--version-script=interpose.map -lsspfd -lssmem -lclht -Wl,--no-whole-archive  -lrt -lm -ldl -L/home/kumichae/my_libs/papi-7.2.0b1/src -lpapi -L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -m64 -pthread -lnuma -libverbs
CFLAGS=-I../include/ -I/home/kumichae/my_libs/rdma-core/build/include/rdma/ -I../obj/CLHT/include/ -I../obj/CLHT/external/include/ -I/home/kumichae/my_libs/papi-7.2.0b1/src -fPIC -Wall -$(OFLAG) -g -DCACHELINE_SIZE=$(CACHELINE_SIZE) -D$(COMM_PROT) -D$(DEBUG)

# LOCAL MACHINE
LDFLAGS=-L../obj/CLHT/external/lib -L../obj/CLHT -Wl,--whole-archive -Wl,--version-script=interpose.map -lsspfd -lssmem -lclht -Wl,--no-whole-archive  -lrt -lm -ldl -lpapi -m64 -pthread -lrdmacm -libverbs -lnuma
CFLAGS=-I../include/ -I/usr/include/rdma/ -I../obj/CLHT/include/ -I../obj/CLHT/external/include/ -fPIC -Wall -$(OFLAG) -g -DCACHELINE_SIZE=$(CACHELINE_SIZE) -D$(COMM_PROT) -D$(DEBUG)

# SFLAGS=-I../include/ -I/usr/include/rdma/ -fPIC -Wall -O2 -g
SLDFLAGS=-lrdmacm -libverbs -lnuma -pthread -ldl

SERVER_SOURCES = $(wildcard *_server.c)
SERVER_TARGETS = $(addprefix ../, $(basename $(SERVER_SOURCES)))

# CYCLE_PER_US:=$(shell lscpu | grep "MHz" | awk 'NR==1 {printf "%dL\n", $$4}')
CYCLE_PER_US:=$(shell cat "/proc/cpuinfo" | grep "MHz" | head -1 | awk '{printf "%dL\n", $$4}')
# CYCLE_PER_US=2400L
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

# Keep object files
.PRECIOUS: %.o
.SECONDARY: $(OBJS)
.PHONY: all
all: $(SERVER_TARGETS) 

../%_server: utils.c %_server.c %_common.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -DCYCLE_PER_US=$(CYCLE_PER_US)

../tcp_server: utils.c tcp_server.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -DCYCLE_PER_US=$(CYCLE_PER_US)

../interpose_server: interpose_server.c
	echo "Ignoring interpose_server rule"

.SECONDEXPANSION:
../obj/%.o: $$(lastword $$(subst /, ,%)).c $$(lastword $$(subst /, ,%)).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../obj/%.o: $$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).c ../include/$$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../lib/server/lib%_server.so: ../obj/%/interpose_server.o ../obj/%/utils.o ../obj/%/rdma_common.o $$(subst algo,%,../obj/algo/algo.o)
	$(CC) -shared -o $@ $^ $(LDFLAGS) 

.SECONDEXPANSION:
../lib/client/lib%_client.so: ../obj/%/interpose_client.o ../obj/%/utils.o ../obj/%/rdma_common.o $$(subst algo,%,../obj/algo/algo.o)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

.SECONDEXPANSION:
../lib/original/lib%.so: ../obj/%/interpose.o ../obj/%/utils.o $$(subst algo,%,../obj/algo/algo.o)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

