# ============================================================
# Makefile — Reliable Data Transfer over UDP (Basic Version)
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
TESTS    = test_packet.exe test_loopback.exe test_file_io.exe \
           test_sw.exe test_gbn.exe test_sr.exe
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

# ── Core Test Suite Executables ───────────────────────────────
test_packet.exe: $(TEST_DIR)/test_packet.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_loopback.exe: $(TEST_DIR)/test_udp_loopback.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_file_io.exe: $(TEST_DIR)/test_file_io.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_sw.exe: $(TEST_DIR)/test_stop_and_wait.c $(OBJS_SW)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_gbn.exe: $(TEST_DIR)/test_go_back_n.c $(OBJS_GBN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_sr.exe: $(TEST_DIR)/test_selective_repeat.c $(OBJS_SR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Run Core Tests ────────────────────────────────────────────
test: all
	@echo [1/6] Running test_packet...
	@./test_packet.exe
	@echo [2/6] Running test_loopback...
	@./test_loopback.exe
	@echo [3/6] Running test_file_io...
	@./test_file_io.exe
	@echo [4/6] Running test_sw...
	@./test_sw.exe
	@echo [5/6] Running test_gbn...
	@./test_gbn.exe
	@echo [6/6] Running test_sr...
	@./test_sr.exe
	@echo.
	@echo =====================================================
	@echo   ALL CORE ARQ PROTOCOL TESTS PASSED SUCCESSFULLY!
	@echo =====================================================

ifeq ($(OS),Windows_NT)
clean:
	-cmd /c "del /Q /F src\*.o *.exe 2>nul"
else
clean:
	rm -f $(SRC_DIR)/*.o *.exe
endif
