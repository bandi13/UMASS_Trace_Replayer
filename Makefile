all: runTrace

runTrace: runTrace.cpp
	g++ runTrace.cpp -o runTrace -std=c++14 -O4 -g -pg -Wall
