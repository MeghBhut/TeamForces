# Makefile — Linux build for TeamForces.  Run:  make
# (On Windows without `make`, use build.ps1 or build.sh instead.)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -I third_party
LDFLAGS  ?= -pthread
SOURCES  := $(wildcard src/*.cpp)
TARGET   := teamforces

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) teamforces.exe

.PHONY: clean
