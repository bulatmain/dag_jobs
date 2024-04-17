all: compilation

compilation:
	g++ main.cpp dag_jobs.hpp -o main -g -pedantic

clean:
	rm *.o main

