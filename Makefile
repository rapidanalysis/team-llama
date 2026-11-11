# Xerxes Pi Admin Backend
#
#   make            build ./xerxes_backend (native). Only rebuilds what changed.
#   make static     build ./xerxes_backend with libgcc and libstdc++ linked in,
#                   so it doesn't depend on the device's C++ runtime version
#                   (libcurl and glibc are still loaded from the device)
#   make llama      build llama-server from third_party/llama.cpp (needed by
#                   the Llama tab; on a Raspberry Pi this can take an hour+)
#   make deb        build build/deb/xerxes-pi_<version>_<arch>.deb, which
#                   installs the same files as deploy_systemd.sh
#   make bundle     build xerxes_install.sh: a single self-extracting script
#                   containing the source that compiles itself on the target.
#                   Copy it to the device and run: bash xerxes_install.sh
#   make test       build and run every test (see Tests below)
#   make clean      remove build output (keeps the llama-server build)
#
# Cross-compile:  make CXX=aarch64-linux-gnu-g++ TARGET=xerxes_backend_arm64

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -O2
CPPFLAGS += -Iinclude -Ithird_party
LDFLAGS  ?=
LDLIBS   ?= -pthread -lcurl
TARGET   ?= xerxes_backend

# The OLED display (src/oled) and RFID reader (src/rfid) run inside the
# backend, so there is one binary
OLED_SRCS = $(wildcard src/oled/*.cpp)
SRCS      = $(wildcard src/*.cpp) $(wildcard src/rfid/*.cpp) $(OLED_SRCS)
HDRS      = $(wildcard include/*.h) $(wildcard src/oled/*.h src/oled/*.hpp)

# Objects mirror src/ under build/obj/. -MMD writes a .d file next to each
# object listing the headers it includes, so editing a header rebuilds only
# the files that use it.
OBJ_DIR = build/obj
OBJS    = $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS    = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Relinks only: the objects are the same, just the link flags change
static:
	rm -f $(TARGET)
	$(MAKE) LDFLAGS="$(LDFLAGS) -static-libgcc -static-libstdc++" TARGET=$(TARGET)

# ── llama.cpp ────────────────────────────────────────────────────────────
# xerxes_backend starts llama-server from this path, relative to its working
# directory (kServerBinary in src/llama_manager.cpp).

LLAMA_DIR        = third_party/llama.cpp
LLAMA_BIN_DIR    = $(LLAMA_DIR)/build/bin
LLAMA_SERVER_BIN = $(LLAMA_BIN_DIR)/llama-server
# Parallel jobs for the llama.cpp build. 1 keeps a 2-4 GB Pi from running out
# of memory; raise it on a bigger machine, e.g. make llama LLAMA_JOBS=4
LLAMA_JOBS      ?= 1

llama: $(LLAMA_SERVER_BIN)

$(LLAMA_SERVER_BIN):
	@if [ ! -f $(LLAMA_DIR)/CMakeLists.txt ]; then \
		echo "[Error] $(LLAMA_DIR) is empty. Run: git submodule update --init"; \
		exit 1; \
	fi
	cmake -B $(LLAMA_DIR)/build -S $(LLAMA_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(LLAMA_DIR)/build -j$(LLAMA_JOBS) --target llama-server

# ── Debian package ───────────────────────────────────────────────────────
# Installs to /opt/xerxes with the systemd unit from packaging/, the same
# layout deploy_systemd.sh produces, and starts the service on install.
# Models in /opt/xerxes/models and settings in /root/.xerxes are kept on
# upgrade and removal.

DEB_NAME    ?= xerxes-pi
DEB_VERSION ?= 0.2.0
DEB_ARCH    ?= $(shell dpkg --print-architecture 2>/dev/null || echo arm64)
DEB_MAINTAINER ?= $(shell git config user.name 2>/dev/null || echo Team Llama) <$(shell git config user.email 2>/dev/null || echo team-llama@localhost)>
DEB_DIR      = build/deb/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH)
DEB_APP      = $(DEB_DIR)/opt/xerxes

deb: static llama
	rm -rf $(DEB_DIR) $(DEB_DIR).deb
	mkdir -p $(DEB_APP)/$(LLAMA_BIN_DIR) $(DEB_DIR)/lib/systemd/system $(DEB_DIR)/DEBIAN
	install -m 755 $(TARGET) $(DEB_APP)/xerxes_backend
	cp -r public $(DEB_APP)/public
	cp -a $(LLAMA_SERVER_BIN) $(LLAMA_BIN_DIR)/*.so* $(DEB_APP)/$(LLAMA_BIN_DIR)/
	install -m 644 packaging/xerxes.service $(DEB_DIR)/lib/systemd/system/xerxes.service
	sed -e 's|@NAME@|$(DEB_NAME)|g' -e 's|@VERSION@|$(DEB_VERSION)|g' -e 's|@ARCH@|$(DEB_ARCH)|g' \
	    -e 's|@MAINTAINER@|$(DEB_MAINTAINER)|g' \
	    packaging/control.in > $(DEB_DIR)/DEBIAN/control
	install -m 755 packaging/postinst packaging/prerm packaging/postrm $(DEB_DIR)/DEBIAN/
	dpkg-deb --build --root-owner-group $(DEB_DIR)
	@echo "Wrote $(DEB_DIR).deb. Install it with: sudo apt install ./$(DEB_DIR).deb"

# ── Self-extracting installer ────────────────────────────────────────────
# Shell stub + base64 tarball of everything needed to build the backend.
# llama.cpp is not included (it is large); the Llama tab needs make deb or
# deploy_systemd.sh from a full clone.

BUNDLE_FILES = Makefile src include public packaging tests install_deps.sh

bundle: xerxes_install.sh

xerxes_install.sh: packaging/bundle_stub.sh $(shell find $(BUNDLE_FILES) -type f)
	cat packaging/bundle_stub.sh > $@
	echo "__PAYLOAD__" >> $@
	tar czf - --exclude='.DS_Store' $(BUNDLE_FILES) | base64 -w0 >> $@
	echo "" >> $@
	chmod +x $@
	@echo "Wrote $@ ($$(wc -c < $@) bytes). Copy it to the device and run: bash $@"

# ── Tests ────────────────────────────────────────────────────────────────
# Each tests/test_<name>.cpp is linked with only the source it covers, so
# tests build without pulling in main.cpp or the rest of the server.
# Built binaries go in build/tests/ (gitignored).
#
#   make test             build and run every test
#   make test_newt        build and run a single test
#   make tests            build all tests without running them

TEST_DIR    = build/tests
# The tested code doesn't use curl, so tests build without libcurl installed
TEST_LDLIBS = -pthread

# Source file(s) each test needs, besides the test itself
test_utils_SRCS = src/utils.cpp
test_newt_SRCS  = src/newt_manager.cpp
# The RFID tests use a simulated module instead of /dev/i2c-N
test_rfid_driver_SRCS  = src/rfid/rfid.cpp tests/mfrc522_sim.cpp
test_rfid_manager_SRCS = src/rfid/rfid_manager.cpp src/rfid/rfid.cpp src/rfid/tags.cpp tests/mfrc522_sim.cpp
# No display needed: checks settings, validation and the preview
test_oled_manager_SRCS = $(OLED_SRCS)

TESTS     = test_utils test_newt test_rfid_driver test_rfid_manager test_oled_manager
TEST_BINS = $(addprefix $(TEST_DIR)/,$(TESTS))

.SECONDEXPANSION:
$(TEST_DIR)/%: tests/%.cpp $$(%_SRCS) $(HDRS) $(wildcard tests/*.h) | $(TEST_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $($*_SRCS) -o $@ $(TEST_LDLIBS)

$(TEST_DIR):
	mkdir -p $@

tests: $(TEST_BINS)

# Runs every test even if an earlier one fails, then fails if any did
test: $(TEST_BINS)
	@failed=""; \
	for t in $(TEST_BINS); do \
		echo ""; echo "==> $$t"; \
		./$$t || failed="$$failed $$(basename $$t)"; \
	done; \
	echo ""; \
	if [ -n "$$failed" ]; then echo "FAILED:$$failed"; exit 1; \
	else echo "All test suites passed."; fi

$(TESTS): %: $(TEST_DIR)/%
	./$<

clean:
	rm -rf build $(TARGET) xerxes_install.sh

.PHONY: all static llama deb bundle clean test tests $(TESTS)
