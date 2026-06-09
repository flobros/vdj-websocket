.PHONY: all install intel arm universal clean

CXX      = clang++
CXXFLAGS = -O2 -Wall -fPIC -std=c++11 -I sdk
OUTDIR   = $(HOME)/Library/Application Support/VirtualDJ/Plugins64/Generics

# Default: universal binary (Intel + Apple Silicon)
all: NowPlaying.bundle

NowPlaying.bundle: NowPlaying.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 -arch arm64 $< -o "$@"

intel: NowPlaying.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 $< -o NowPlaying.bundle

arm: NowPlaying.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch arm64 $< -o NowPlaying.bundle

universal: NowPlaying.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 NowPlaying.cpp -o NowPlaying_x86_64.bundle
	$(CXX) $(CXXFLAGS) -bundle -arch arm64  NowPlaying.cpp -o NowPlaying_arm64.bundle
	lipo -create NowPlaying_x86_64.bundle NowPlaying_arm64.bundle -output NowPlaying.bundle
	rm NowPlaying_x86_64.bundle NowPlaying_arm64.bundle

install: NowPlaying.bundle
	mkdir -p "$(OUTDIR)"
	cp NowPlaying.bundle "$(OUTDIR)/NowPlaying.bundle"
	@if [ -f NowPlaying.ini ]; then \
		cp NowPlaying.ini "$(OUTDIR)/NowPlaying.ini"; \
		echo "Installed: $(OUTDIR)/NowPlaying.ini"; \
	else \
		echo "NOTE: NowPlaying.ini not found - plugin will use built-in defaults (no auth)"; \
	fi
	@echo "Installed: $(OUTDIR)/NowPlaying.bundle"
	@echo "Restart VirtualDJ to load the plugin."

clean:
	rm -f NowPlaying.bundle NowPlaying_x86_64.bundle NowPlaying_arm64.bundle
