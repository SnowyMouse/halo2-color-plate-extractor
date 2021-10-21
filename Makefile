CXX=g++
CXXFLAGS=-O3 -std=c++20 -lz -ltiff -lpthread

halo2-color-plate-extractor:
	$(CXX) main.cpp $(CXXFLAGS) -o halo2-color-plate-extractor

clean:
	rm -f halo2-color-plate-extractor