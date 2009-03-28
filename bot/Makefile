CXXFLAGS := -ggdb -Wall -Wextra -O2 -I..
LIBS := -L../rs/ -lRS -lcurl -lpthread -lboost_regex

all: ctags deps Bot
	@echo "Ready!"

ctags:
	@ctags ../*/*.{cc,hh}

deps:
	@echo "Checking depends..."
	@g++ -MM *.cc -I.. > Makefile.deps

-include Makefile.deps

Bot : Bot.cc ../rs/libRS.a
	@echo "Compiling '$@'..."
	@g++ $(CXXFLAGS) -o Bot Bot.cc $(LIBS)

clean:
	@echo "Cleaning compilation..."
	@rm -rf *.o core core.* Bot tags Makefile.deps

