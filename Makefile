

CXXFLAGS = \
	   -g \
	   -Wall -Werror -Wextra \
	   -fpermissive
CXXFLAGS2 := \
	$(CXXFLAGS) \
	-std=c++14 \
	-fPIC -MMD -I .
LDFLAGS2 := -shared
LIBS := -ldl -lkj -lcapnp -lcapnp-rpc
SONAME := sandstorm-preload

capnp_objects := \
	filesystem.capnp.o \
	util.capnp.o
capnp_src := \
	filesystem.capnp \
	$(SANDSTORM_PATH)/src/sandstorm/util.capnp

capnp_headers := $(capnp_objects:.o=.h)
capnp_cxx := $(capnp_objects:.o=.c++)

objects := \
	$(capnp_objects) \
	shim.o \
	real.o \
	EventLoopData.o \
	EventInjector.o \
	CapnpFile.o \
	Vfs.o

all: $(SONAME).so
clean:
	git clean -X -f

$(SONAME).so: $(objects)
	$(CXX) $(LDFLAGS2) $(LIBS) -o $@ $(objects)

%.o: %.c++ $(capnp_headers)
	$(CXX) $(CXXFLAGS2) -c -o $@ $<

$(capnp_headers) $(capnp_cxx): .built-capnp
.built-capnp: $(capnp_src)
	capnp compile -oc++ \
		-I $(SANDSTORM_PATH)/src/sandstorm \
		--src-prefix=$(SANDSTORM_PATH)/src/sandstorm \
		$(capnp_src)
	touch $@

filesystem.capnp: sandstorm-filesystem/filesystem/filesystem.capnp
	sed -e 's|^using Go = .*|| ; s|\$$Go.*||' < $< > $@


.PHONY: all clean
.SUFFIXES:

-include $(wildcard *.d)
