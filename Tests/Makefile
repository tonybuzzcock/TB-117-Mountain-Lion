# Offline tests. Works with g++ or clang++ on Linux (the Mac build is in
# scripts/build_vst_mac.sh). Everything is compiled as C++98 to prove it builds
# on old toolchains.
#   make run        preset store + DSP test suite, writes demo WAVs to ./renders
#   make vst-run    builds the plugin as a shared library and loads it in a mini VST host
#   make asan       both, under AddressSanitizer and UndefinedBehaviorSanitizer

CXX      ?= c++
CXXFLAGS ?= -O2 -std=c++98 -Wall -Wextra -pedantic -Wno-long-long
DSP_SRC   = ../Source/DSP/Flanger.cpp
HEADERS   = ../Source/DSP/*.h ../Source/VST2/*.h

flanger_test: $(DSP_SRC) flanger_test.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(DSP_SRC) flanger_test.cpp -o $@

TB117Flanger.so: $(DSP_SRC) ../Source/VST2/FlangerVST.cpp ../Source/VST2/Presets.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -fPIC -fvisibility=hidden -shared ../Source/VST2/FlangerVST.cpp ../Source/VST2/Presets.cpp $(DSP_SRC) -o $@

vst_host_test: vst_host_test.cpp $(DSP_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) vst_host_test.cpp $(DSP_SRC) -ldl -o $@

preset_test: preset_test.cpp ../Source/VST2/Presets.cpp ../Source/VST2/*.h
	$(CXX) $(CXXFLAGS) preset_test.cpp ../Source/VST2/Presets.cpp -o $@

run: flanger_test preset_test
	./preset_test
	mkdir -p renders
	./flanger_test renders

vst-run: TB117Flanger.so vst_host_test
	./vst_host_test ./TB117Flanger.so

asan:
	$(MAKE) clean
	$(MAKE) run vst-run CXXFLAGS="-O1 -g -std=c++98 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"
	$(MAKE) clean

clean:
	rm -rf flanger_test preset_test vst_host_test TB117Flanger.so renders

.PHONY: run vst-run asan clean
