LIBS = -ltesseract -llept -lstdc++fs
FLAGS = -std=c++17 -Wall -g

convert2csv: main.cpp
	g++ -o convert2csv main.cpp $(FLAGS) $(LIBS)

clean:
	rm -f ./convert2csv

debug: convert2csv
	gdb convert2csv
