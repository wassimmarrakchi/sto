# Makefile for Writing Make Files Example

# *****************************************************
# Variables to control Makefile operation

CXX = g++
CXXFLAGS = -Wall -g -pthread

# ****************************************************
# Targets needed to bring the executable up to date

ShflLock-Test: ShflLock-Test.cc ShflLock.hh
	$(CXX) $(CXXFLAGS) -o ShflLock-Test ShflLock-Test.cc