include ../Makefile.config

LDFLAGS=-L../obj/CLHT/external/lib -L../obj/CLHT -Wl,--whole-archive -Wl,--version-script=interpose.map -lsspfd -lssmem -lclht -Wl,--no-whole-archive  -lrt -lm -ldl -L/home/kumichae/my_libs/papi-7.2.0b1/src -lpapi -L/home/kumichae/my_libs/rdma-core/build/lib/ -lrdmacm -m64 -pthread
CFLAGS=-I../include/ -I/home/kumichae/my_libs/rdma-core/build/include/rdma/ -I../obj/CLHT/include/ -I../obj/CLHT/external/include/ -I/home/kumichae/my_libs/papi-7.2.0b1/src -fPIC -Wall -O2 -g

# Keep objects files
.PRECIOUS: %.o
.SECONDARY: $(OBJS)

.SECONDEXPANSION:
../obj/%.o: $$(lastword $$(subst /, ,%)).c $$(lastword $$(subst /, ,%)).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../obj/%.o: $$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).c ../include/$$(firstword $$(subst _, , $$(lastword $$(subst /, ,%)))).h
	$(eval $@_TMP := $(shell echo $@ | cut -d/ -f3 | cut -d_ -f1))
	$(CC) $(CFLAGS) -D$$(echo $@ | cut -d/ -f3 | cut -d_ -f1 | tr '[a-z]' '[A-Z]') -DCOND_VAR=$(COND_VAR) -DFCT_LINK_SUFFIX=$($@_TMP) -DWAITING_$$(echo $@ | cut -d/ -f3 | cut -d_ -f2- | tr '[a-z]' '[A-Z]') -o $@ -c $<

.SECONDEXPANSION:
../lib/lib%.so: ../obj/%/interpose.o ../obj/%/utils.o ../obj/%/rdma_common.o $$(subst algo,%,../obj/algo/algo.o)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
