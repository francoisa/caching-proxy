TST =$(TGT).t
DEBUG =-g
GTEST =../../googletest/googletest
CXXFLAGS =-Wall -std=c++11 -I. $(DEBUG)
TESTLIBS =-L/usr/local/lib -lgtest -lpthread
OBJS =$(patsubst %.cc,.obj/%.o,$(wildcard *.cc))
CXX=clang++
LD=clang++
LDLIBS=-L/usr/local/lib -lboost_program_options -lpthread

$(TGT): $(OBJS)

test/$(TST): test/.obj/$(TST).o .obj/$(TGT).o
	@echo "(LD) $@"
	@pwd
	$(LD) $^ $(TESTLIBS) -o $@

.obj/main.o: main.cc $(TGT).h

.obj/$(TGT).o: $(TGT).cc $(TGT).h

test/.obj/$(TST).o: test/$(TST).cc .obj/$(TGT).o

clean:
	$(RM) *~ .obj/*.o $(TGT) test/.obj/*.o test/$(TST)

test: test/$(TST)
	test/$(TST)
