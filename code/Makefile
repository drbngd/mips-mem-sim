SRC = $(wildcard src/*.cpp)
INPUT ?= $(wildcard inputs/*/*.x)

.PHONY: all verify clean

all: sim

sim: $(SRC)
	g++ -std=c++17 -g -O2 $^ -o $@

basesim: $(SRC)
	g++ -std=c++17 -g -O2 $^ -o $@

run: sim
	@python run.py $(INPUT)

clean:
	rm -rf *.o *~ sim

