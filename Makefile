all: runTrace

runTrace: runTrace.cpp
	g++ runTrace.cpp -o runTrace -std=c++17 -O4 -Wall -g

clean:
	rm runTrace -f
