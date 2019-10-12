convert2csv: main.cpp
	g++ -o convert2csv main.cpp -std=c++17 -ltesseract -llept -lstdc++fs -Wall
