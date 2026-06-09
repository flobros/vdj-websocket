CXX      = clang++
CXXFLAGS = -O2 -Wall -fPIC -std=c++11 -I sdk
LDFLAGS  = -bundle
OUTDIR   = $(HOME)/Library/Application Support/VirtualDJ/Plugins64/Generics

all: NowPlaying.bundle

NowPlaying.bundle: NowPlaying.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) NowPlaying.cpp -o "$@"

install: NowPlaying.bundle
	mkdir -p "$(OUTDIR)"
	cp "NowPlaying.bundle" "$(OUTDIR)/NowPlaying.bundle"
	@if [ -f NowPlaying.ini ]; then \
		cp NowPlaying.ini "$(OUTDIR)/NowPlaying.ini"; \
		echo "Installed: $(OUTDIR)/NowPlaying.ini"; \
	else \
		echo "NOTE: NowPlaying.ini not found - plugin will use built-in defaults (no auth)"; \
	fi
	@echo "Installed: $(OUTDIR)/NowPlaying.bundle"
	@echo "Restart VirtualDJ to load the plugin."

clean:
	rm -f NowPlaying.bundle
