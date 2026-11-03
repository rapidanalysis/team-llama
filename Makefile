CXX      = g++
CXXFLAGS = -std=c++17 -Wall -O2 -Iinclude -Ithird_party
LDLIBS   = -pthread -lcurl

TARGET = xerxes_backend
SRCS   = $(wildcard src/*.cpp) $(wildcard src/rfid/*.cpp)
HDRS   = $(wildcard include/*.h)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDLIBS)

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

TESTS     = test_utils test_newt test_rfid_driver test_rfid_manager
TEST_BINS = $(addprefix $(TEST_DIR)/,$(TESTS))

.SECONDEXPANSION:
$(TEST_DIR)/%: tests/%.cpp $$(%_SRCS) $(HDRS) $(wildcard tests/*.h) | $(TEST_DIR)
	$(CXX) $(CXXFLAGS) $< $($*_SRCS) -o $@ $(TEST_LDLIBS)

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
	rm -f $(TARGET)
	rm -rf $(TEST_DIR)

.PHONY: clean test tests $(TESTS)
