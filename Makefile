NAME = wfocr
LIBS = -ltesseract -llept -lstdc++fs
FLAGS = -std=c++17 -Wall -g

.PHONY: clean debug test

$(NAME): main.cpp
	g++ -o $@ $< $(FLAGS) $(LIBS)

clean:
	rm -f ./$(NAME)

debug: $(NAME)
	gdb $<

test: $(NAME)
	python3 test.py
