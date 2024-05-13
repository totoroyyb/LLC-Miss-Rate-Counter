# include this Makefile in all subprojects
# define ROOT_PATH before including
ifndef ROOT_PATH
$(error ROOT_PATH is not set)
endif

# load configuration parameters
include $(ROOT_PATH)/build/config

# shared toolchain definitions
INC = -I$(ROOT_PATH)/inc
FLAGS  = -g -Wall -D_GNU_SOURCE $(INC) -m64 -mxsavec -m64 -mxsave -m64 -muintr
LDFLAGS = -T $(ROOT_PATH)/base/base.ld
LD      = gcc
CC      = gcc
LDXX	= g++
CXX	= g++
AR      = ar
SPARSE  = sparse

ifeq ($(CONFIG_CLANG),y)
LD	= clang
CC	= clang
LDXX	= clang++
CXX	= clang++
FLAGS += -Wno-sync-fetch-and-nand-semantics-changed
endif

# parse configuration options
ifeq ($(CONFIG_DEBUG),y)
FLAGS += -DDEBUG -rdynamic -O0 -ggdb -mssse3
LDFLAGS += -rdynamic
else
FLAGS += -DNDEBUG -O3
ifeq ($(CONFIG_OPTIMIZE),y)
FLAGS += -march=native -flto -ffast-math
ifeq ($(CONFIG_CLANG),y)
LDFLAGS += -flto
endif
else
FLAGS += -mssse3
endif
endif

CFLAGS = -std=gnu11 $(FLAGS)
CXXFLAGS = -std=gnu++20 $(FLAGS)

# handy for debugging
print-%  : ; @echo $* = $($*)
