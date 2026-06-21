# Makefile para ZAR-SHELL
# Usar con MinGW/MSYS2: make
# O especificar compilador: make CXX=g++

CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra
LDFLAGS   = -static -lwinhttp
TARGET    = zar_shell.exe
SRC       = src/zar_shell.cpp

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)
	@echo ""
	@echo "  [OK] Compilado: $(TARGET)"
	@echo "  Ejecuta con: ./$(TARGET)"
	@echo ""

clean:
	del /f $(TARGET) 2>nul || rm -f $(TARGET)

install: $(TARGET)
	@echo "Copia $(TARGET) a una carpeta en tu PATH para usarlo como shell global."
