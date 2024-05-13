# ccflags-y := $(ccflags-y) -xc -E -v
KDIR ?= /lib/modules/$(shell uname -r)/build
MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(MAKEFILE_DIR)/build
BUILD_DIR_MAKEFILE ?= $(MAKEFILE_DIR)/build/Makefile

default: $(BUILD_DIR_MAKEFILE)
	make -C $(KDIR) M=$(BUILD_DIR) src=$(MAKEFILE_DIR) modules

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR_MAKEFILE): $(BUILD_DIR)
	touch "$@"

clean:
	make -C $(KDIR) M=$(BUILD_DIR) src=$(MAKEFILE_DIR) clean
