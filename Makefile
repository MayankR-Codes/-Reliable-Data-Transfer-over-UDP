# ============================================================
# Makefile — Reliable Data Transfer over UDP
# Team: Mayank Rana, Dimple Mohapatra, Manisha Behera, Raushan
# ============================================================
CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lws2_32

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests

# Object files
OBJS_COMMON = $(SRC_DIR)/packet.o $(SRC_DIR)/socket_utils.o $(SRC_DIR)/file_io.o
OBJS_SW     = $(OBJS_COMMON) $(SRC_DIR)/stop_and_wait.o
OBJS_GBN    = $(OBJS_COMMON) $(SRC_DIR)/go_back_n.o
OBJS_SR     = $(OBJS_COMMON) $(SRC_DIR)/selective_repeat.o
OBJS_ALL    = $(OBJS_COMMON) $(SRC_DIR)/stop_and_wait.o $(SRC_DIR)/go_back_n.o $(SRC_DIR)/selective_repeat.o

# Target executables
BINARIES = emulator.exe sender.exe receiver.exe
TESTS    = test_packet.exe test_loopback.exe test_emulator.exe test_loss.exe \
           test_file_io.exe test_delay.exe test_buffer.exe test_sw.exe test_gbn.exe \
           test_corrupt.exe test_sr.exe test_stress.exe
TARGETS  = $(BINARIES) $(TESTS)

all: $(TARGETS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Primary Binaries ──────────────────────────────────────────
emulator.exe: $(SRC_DIR)/emulator.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

sender.exe: $(SRC_DIR)/sender.c $(OBJS_ALL)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

receiver.exe: $(SRC_DIR)/receiver.c $(OBJS_ALL)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Test Suite Executables ────────────────────────────────────
test_packet.exe: $(TEST_DIR)/test_packet.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_loopback.exe: $(TEST_DIR)/test_udp_loopback.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_emulator.exe: $(TEST_DIR)/test_emulator_forwarding.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_loss.exe: $(TEST_DIR)/test_emulator_loss.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_file_io.exe: $(TEST_DIR)/test_file_io.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_delay.exe: $(TEST_DIR)/test_emulator_delay.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_buffer.exe: $(TEST_DIR)/test_buffer.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_corrupt.exe: $(TEST_DIR)/test_emulator_corruption.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_sw.exe: $(TEST_DIR)/test_stop_and_wait.c $(OBJS_SW)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_gbn.exe: $(TEST_DIR)/test_go_back_n.c $(OBJS_GBN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_sr.exe: $(TEST_DIR)/test_selective_repeat.c $(OBJS_SR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_stress.exe: $(TEST_DIR)/test_stress.c $(OBJS_ALL)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Run ALL Tests ─────────────────────────────────────────────
test: all
	@echo Running test_packet...
	@./test_packet.exe
	@echo Running test_loopback...
	@./test_loopback.exe
	@echo Running test_file_io...
	@./test_file_io.exe
	@echo Running test_emulator...
	@./test_emulator.exe
	@echo Running test_loss...
	@./test_loss.exe
	@echo Running test_delay...
	@./test_delay.exe
	@echo Running test_buffer...
	@./test_buffer.exe
	@echo Running test_corrupt...
	@./test_corrupt.exe
	@echo Running test_sw...
	@./test_sw.exe
	@echo Running test_gbn...
	@./test_gbn.exe
	@echo Running test_sr...
	@./test_sr.exe
	@echo Running test_stress...
	@./test_stress.exe

# ── Automated Benchmark & Plotting (Day 6) ───────────────────
benchmark: sender.exe receiver.exe emulator.exe
	@echo Running Automated Benchmarks...
	python scripts/benchmark.py
	@echo Generating Comparison Plots...
	python scripts/plot_results.py

# ── Role-Specific Test Targets ────────────────────────────────
test_day4_mayank: test_corrupt.exe
	@echo.
	@echo =====================================================
	@echo   MAYANK RANA — DAY 4: EMULATOR CORRUPTION TESTS
	@echo =====================================================
	@echo.
	@./test_corrupt.exe

test_day1: test_packet.exe test_loopback.exe
	@./test_packet.exe
	@./test_loopback.exe

test_day2: test_sw.exe
	@./test_sw.exe

test_day3: test_gbn.exe
	@./test_gbn.exe

test_day4: test_sr.exe test_corrupt.exe
	@./test_sr.exe
	@./test_corrupt.exe

ifeq ($(OS),Windows_NT)
clean:
	-cmd /c "del /Q /F src\*.o tests\*.o *.exe 2>nul"
else
clean:
	rm -f $(SRC_DIR)/*.o *.exe $(TEST_DIR)/*.o
endif
