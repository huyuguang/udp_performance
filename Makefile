#CXX11_HOME = /usr/local/gcc6

CXX11_HOME = /usr

ASIO_HOME = ../asio_alone

CXXFLAGS := \
 -g3 \
 -fPIC \
 -std=c++14 \
 -Wall \
 -Wextra \
 -gdwarf-2 \
 -gstrict-dwarf \
 -Wno-unused-parameter \
 -Wno-parentheses \
 -Wdeprecated-declarations \
 -fmerge-all-constants  \
 -MMD \
 -I $(CXX11_HOME)/include \
 -I $(ASIO_HOME) \
 -I.

RELEASE_FLAGS := \
 -O3 \
 -DLINUX \
 -DASIO_STANDALONE \
 -DASIO_HAS_STD_CHRONO \


DEBUG_FLAGS := \
 -O0 \
 -D_DEBUG \
 -DLINUX \
 -DASIO_STANDALONE \
 -DASIO_HAS_STD_CHRONO \

LDFLAGS += \
 -static-libstdc++ -static-libgcc \
 -fmerge-all-constants \
 -L${CXX11_HOME}/lib64 \
 -L./

LIBS := \
 -lpthread \
 -lrt
 
DIR := .

SRC := $(foreach d, $(DIR), $(wildcard $(d)/*.cpp))

OBJS := $(patsubst %.cpp, %.o, $(SRC))
RELEASE_OBJ := $(patsubst %.o, %.ro, $(OBJS))
DEBUG_OBJ := $(patsubst %.o, %.do, $(OBJS))

CXX := export LD_LIBRARY_PATH=${CXX11_HOME}/lib; ${CXX11_HOME}/bin/g++
CC := export LD_LIBRARY_PATH=${CXX11_HOME}/lib; ${CXX11_HOME}/bin/gcc

DEPS := $(patsubst %.o, %.d, $(OBJS))
.PHONY: all clean debug
all: udp_performance
debug: udp_performance_debug
-include $(DEPS)

%.ro : %.cpp
	$(CXX) -c $< $(CXXFLAGS) $(RELEASE_FLAGS) -o $@

%.do : %.cpp
	$(CXX) -c $< $(CXXFLAGS) $(DEBUG_FLAGS) -o $@

udp_performance : $(RELEASE_OBJ)
	$(CXX) $^ -o $@.exe $(LDFLAGS) $(LIBS)

udp_performance_debug : $(DEBUG_OBJ)
	$(CXX) $^ -o $@.exe $(LDFLAGS) $(LIBS)

clean:
	-rm -f $(DEP) $(RELEASE_OBJ) $(DEBUG_OBJ)
	-rm -f *.exe
	find . -regex "\(.*\.d\|.*\.o\)" | xargs rm


