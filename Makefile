CC = gcc
CFLAGS = -O2 -Wno-unused-parameter -Wno-unused-result -Wno-format-truncation
SRC = src/main.c src/lexer.c src/parser.c src/codegen.c
OUT = xlang
XPM = xpm
PREFIX = $(HOME)/.local/bin

all: $(OUT) $(XPM) xconv

$(OUT):
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)
	@echo "Built xlang!"

$(XPM):
	$(CC) -O2 src/xpm.c -o $(XPM)
	@echo "Built xpm!"

install: $(OUT) $(XPM) xconv
	@mkdir -p $(PREFIX)
	@cp $(OUT) $(PREFIX)/xlang
	@cp $(XPM) $(PREFIX)/xpm
	@cp xconv $(PREFIX)/xconv
	@chmod +x $(PREFIX)/xlang $(PREFIX)/xpm $(PREFIX)/xconv
	@echo "Installed! xlang + xpm + xconv ready."
	@echo "Add to PATH: echo 'export PATH=\$$PATH:$(PREFIX)' >> ~/.bashrc && source ~/.bashrc"

clean:
	rm -f $(OUT) $(XPM)

test: $(OUT)
	@echo "=== T1: Hello ===" && ./$(OUT) tests/hello.x  -o /tmp/t1 && /tmp/t1
	@echo "=== T2: Vars  ===" && ./$(OUT) tests/vars.x   -o /tmp/t2 && /tmp/t2
	@echo "=== T3: Math  ===" && ./$(OUT) tests/math.x   -o /tmp/t3 && /tmp/t3
	@echo "=== T4: If    ===" && ./$(OUT) tests/ifelse.x -o /tmp/t4 && /tmp/t4
	@echo "=== T5: Loops ===" && ./$(OUT) tests/loops.x  -o /tmp/t5 && /tmp/t5
	@echo "=== ALL PASSED ==="

.PHONY: all install clean test

xconv:
	$(CC) -O2 src/converter.c -o xconv
	@echo "Built xconv!"
