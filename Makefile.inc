TST =$(TGT).t
DEBUG =-g
BOOST =  /bb/blaw/tools/boost-1_52_0/4.8.0/
CXXFLAGS =-Wall -std=gnu++11 -I. -I$(BOOST)/include $(DEBUG) 
OBJS =$(patsubst %.cc,.obj/%.o,$(wildcard *.cc))
CXX=/bb/blaw/tools/gcc-4_8_0/4.8.0/bin/g++
LD=/bb/blaw/tools/gcc-4_8_0/4.8.0/bin/g++
LDLIBS=-L$(BOOST)/lib -lboost_program_options -lpthread

$(TGT): $(OBJS)

.obj/main.o: main.cc $(TGT).h

.obj/$(TGT).o: $(TGT).cc $(TGT).h

clean:
	$(RM) *~ .obj/*.o $(TGT) 

