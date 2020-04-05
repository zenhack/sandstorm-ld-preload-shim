

CXXFLAGS = -g -Wall -Werror -Wextra
CXXFLAGS2 := \
	$(CXXFLAGS) \
	-std=c++14 \
	-fPIC -MMD -I .
LDFLAGS2 := -shared
LIBS := -lkj -lcapnp
SONAME := sandstorm-preload

capnp_objects := \
	filesystem.capnp.o \
	sandstorm/util.capnp.o
capnp_src := \
	filesystem.capnp \
	$(SANDSTORM_PATH)/src/sandstorm/util.capnp

capnp_headers := $(capnp_objects:.o=.h)
capnp_cxx := $(capnp_objects:.o=.c++)

objects := \
	$(capnp_objects) \
	shim.o

all: $(SONAME).so
clean:
	git clean -X -f
	rm -rf sandstorm/

$(SONAME).so: $(objects)
	$(CXX) $(LDFLAGS2) $(LIBS) -o $@ $(objects)

%.o: %.c++ $(capnp_headers)
	$(CXX) $(CXXFLAGS2) -c -o $@ $<

$(capnp_headers) $(capnp_cxx): $(capnp_src)
	capnp compile -oc++ \
		-I $(SANDSTORM_PATH)/src/ \
		--src-prefix=$(SANDSTORM_PATH)/src \
		$(capnp_src)

.PHONY: all clean
.SUFFIXES:
