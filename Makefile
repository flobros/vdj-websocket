.PHONY: all install intel arm universal clean

CXX      = clang++
CXXFLAGS = -O2 -Wall -fPIC -std=c++11 -I sdk
OUTDIR   = $(HOME)/Library/Application Support/VirtualDJ/Plugins64/SoundEffects

# Default: universal binary (Intel + Apple Silicon)
all: DeckBridge.bundle

DeckBridge.bundle: DeckBridge.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 -arch arm64 $< -o "$@"

intel: DeckBridge.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 $< -o DeckBridge.bundle

arm: DeckBridge.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch arm64 $< -o DeckBridge.bundle

universal: DeckBridge.cpp sdk/vdjPlugin8.h
	$(CXX) $(CXXFLAGS) -bundle -arch x86_64 DeckBridge.cpp -o DeckBridge_x86_64.bundle
	$(CXX) $(CXXFLAGS) -bundle -arch arm64  DeckBridge.cpp -o DeckBridge_arm64.bundle
	lipo -create DeckBridge_x86_64.bundle DeckBridge_arm64.bundle -output DeckBridge.bundle
	rm DeckBridge_x86_64.bundle DeckBridge_arm64.bundle

install: DeckBridge.bundle
	mkdir -p "$(OUTDIR)"
	cp DeckBridge.bundle "$(OUTDIR)/DeckBridge.bundle"
	@if [ -f DeckBridge.ini ]; then \
		cp DeckBridge.ini "$(OUTDIR)/DeckBridge.ini"; \
		echo "Installed: $(OUTDIR)/DeckBridge.ini"; \
	else \
		echo "NOTE: DeckBridge.ini not found - plugin will use built-in defaults (no auth)"; \
	fi
	@echo "Installed: $(OUTDIR)/DeckBridge.bundle"
	@echo "Restart VirtualDJ, then enable DeckBridge once via Effects > Sound Effects."

clean:
	rm -f DeckBridge.bundle DeckBridge_x86_64.bundle DeckBridge_arm64.bundle
