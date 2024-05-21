ROOT_PATH=.
include $(ROOT_PATH)/build/shared.mk

# libbase.a - the base library
base_src = $(wildcard base/*.c)
base_obj = $(base_src:.c=.o)

# pcm lib
PCM_DEPS = $(ROOT_PATH)/deps/pcm/build/src/libpcm.a
PCM_LIBS = -lm -lstdc++

# apps
apps_src = $(wildcard apps/*.cpp)
apps_obj = $(apps_src:.cpp=.o)
apps_targets = $(basename $(apps_src))


# must be first
all: libbase.a counterd $(apps_targets)

libbase.a: $(base_obj)
	$(AR) rcs $@ $^

# counter
counter_src = $(wildcard counter/*.c)
counter_obj = $(counter_src:.c=.o)

counterd: $(counter_obj)
	$(LD) $(LDFLAGS) -o $@ $(counter_obj) libbase.a $(PCM_DEPS) $(PCM_LIBS) -lpthread -lnuma -ldl

# iokerneld: $(iokernel_obj) libbase.a libnet.a base/base.ld $(PCM_DEPS)
# 	$(LD) $(LDFLAGS) -o $@ $(iokernel_obj) libbase.a libnet.a $(DPDK_LIBS) \
# 	$(PCM_DEPS) $(PCM_LIBS) -lpthread -lnuma -ldl

$(apps_targets): $(apps_obj)
	$(LDXX) $(FLAGS) $(LDFLAGS) -o $@ $(apps_obj) -lpthread

# general build rules for all targets
# src = $(base_src) $(net_src) $(runtime_src) $(iokernel_src) $(test_src)
# asm = $(runtime_asm)
src = $(counter_src)
obj = $(src:.c=.o) $(asm:.S=.o)
dep = $(obj:.o=.d)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: submodule
submodule:
	$(ROOT_PATH)/build/init_submodule.sh

.PHONY: submodule-clean
submodule-clean:
	$(ROOT_PATH)/build/init_submodule.sh clean

# prints sparse checker tool output
sparse: $(src)
	$(foreach f,$^,$(SPARSE) $(filter-out -std=gnu11, $(CFLAGS)) $(CHECKFLAGS) $(f);)

.PHONY: clean
clean:
	rm -f $(obj) $(dep) libbase.a \
	counterd \
	$(apps_targets) $(apps_obj)
