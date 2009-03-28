CXXFLAGS := -ggdb -O2 -Wall -Wextra -I..

src := $(wildcard *.cc)
obj := $(subst .cc,.o,$(src))

all: ctags deps libRS.a 
	@echo "Ready!"

ctags:
	@ctags ../*/*.{cc,hh}

deps:
	@echo "Checking depends..."
	@g++ -MM *.cc -I.. > Makefile.deps

-include Makefile.deps

libRS.a: $(obj)
	@echo "Building library..."
	@rm -f $@
	@ar cq $@ $^

.cc.o:
	@echo "Compiling '$<'..."
	@g++ $(CXXFLAGS) -c $<

clean:
	@echo "Cleaning compilation..."
	@rm -f *.o *.a Makefile.deps core core.* tags 

