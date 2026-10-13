# TermTrivia build. Targets: all, server, client, test, test-default-timers, clean.
CXX      ?= g++
CXXFLAGS ?= -g -O0
CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -Isrc -Itests -MMD -MP
LDFLAGS  += -pthread

BUILD := build

COMMON_SRC := $(wildcard src/common/*.cpp)
SERVER_SRC := $(wildcard src/server/*.cpp)
CLIENT_SRC := $(wildcard src/client/*.cpp)
# Everything except the two main() files, so tests can link the same code.
SERVER_LIB_SRC := $(filter-out src/server/server_main.cpp,$(SERVER_SRC))
CLIENT_LIB_SRC := $(filter-out src/client/client_main.cpp,$(CLIENT_SRC))

obj = $(patsubst %.cpp,$(BUILD)/%.o,$(1))

COMMON_OBJ := $(call obj,$(COMMON_SRC))
SERVER_LIB_OBJ := $(call obj,$(SERVER_LIB_SRC))
CLIENT_LIB_OBJ := $(call obj,$(CLIENT_LIB_SRC))
UNIT_OBJ := $(call obj,$(wildcard tests/unit/*.cpp))
INTEGRATION_OBJ := $(call obj,$(wildcard tests/integration/*.cpp))
TEST_MAIN_OBJ := $(call obj,tests/test_main.cpp)
HELPERS := $(patsubst tests/helpers/%.cpp,$(BUILD)/%,$(wildcard tests/helpers/*.cpp))

.PHONY: all test test-default-timers clean
all: server client

server: $(COMMON_OBJ) $(SERVER_LIB_OBJ) $(call obj,src/server/server_main.cpp)
	$(CXX) $^ -o $@ $(LDFLAGS)

client: $(COMMON_OBJ) $(CLIENT_LIB_OBJ) $(call obj,src/client/client_main.cpp)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD)/unit_tests: $(TEST_MAIN_OBJ) $(UNIT_OBJ) $(COMMON_OBJ) $(SERVER_LIB_OBJ) $(CLIENT_LIB_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD)/integration_tests: $(TEST_MAIN_OBJ) $(INTEGRATION_OBJ) $(COMMON_OBJ) $(SERVER_LIB_OBJ) $(CLIENT_LIB_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Each helper is one small program under tests/helpers (raw-socket client, ...).
$(BUILD)/%: $(BUILD)/tests/helpers/%.o $(COMMON_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The test target builds every program first, so a clean tree never runs
# stale or missing binaries.
test: server client $(BUILD)/unit_tests $(BUILD)/integration_tests $(HELPERS)
	$(BUILD)/unit_tests
	$(BUILD)/integration_tests
	@for script in tests/scripts/*.sh; do \
	    case $$script in *default_timers*) continue;; esac; \
	    echo "== $$script"; bash $$script || exit 1; \
	done

# Slow check against the real 20/5/10/60 second defaults (about 2 minutes).
test-default-timers: server client $(HELPERS)
	bash tests/scripts/default_timers.sh

clean:
	rm -rf $(BUILD) server client

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
