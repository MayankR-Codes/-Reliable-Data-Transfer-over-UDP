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

# Target executables
TARGETS = emulator.exe test_packet.exe test_loopback.exe test_emulator.exe test_loss.exe test_file_io.exe test_delay.exe test_buffer.exe test_sw.exe test_gbn.exe

all: $(TARGETS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

emulator.exe: $(SRC_DIR)/emulator.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

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

# ── Manisha Behera — Day 2: Stop-and-Wait ──────────────────────
test_sw.exe: $(TEST_DIR)/test_stop_and_wait.c $(OBJS_SW)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Manisha Behera — Day 3: Go-Back-N ──────────────────────────
test_gbn.exe: $(TEST_DIR)/test_go_back_n.c $(OBJS_GBN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Run ALL tests
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

# ── Manisha Behera — Day 1 Tests Only ───────────────────────
test_day1: test_packet.exe test_loopback.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA BEHERA — DAY 1 TESTS
	@echo =====================================================
	@echo.
	@echo [1/2] Running Packet Checksum Tests...
	@./test_packet.exe
	@echo [2/2] Running UDP Loopback Tests...
	@./test_loopback.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA DAY 1: ALL TESTS COMPLETE
	@echo =====================================================

# ── Manisha Behera — Day 2 Tests Only ───────────────────────
test_day2: test_sw.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA BEHERA - DAY 2 TESTS
	@echo =====================================================
	@echo.
	@echo [1/1] Running Stop-and-Wait End-to-End Test...
	@./test_sw.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA DAY 2: ALL TESTS COMPLETE
	@echo =====================================================

# ── Manisha Behera — Day 3 Tests Only ───────────────────────
test_day3: test_gbn.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA BEHERA - DAY 3 TESTS
	@echo =====================================================
	@echo.
	@echo [1/1] Running Go-Back-N End-to-End Tests (N=4,8,16)...
	@./test_gbn.exe
	@echo.
	@echo =====================================================
	@echo   MANISHA DAY 3: ALL TESTS COMPLETE
	@echo =====================================================

ifeq ($(OS),Windows_NT)
clean:
	-cmd /c "del /Q /F src\*.o tests\*.o *.exe 2>nul"
else
clean:
	rm -f $(SRC_DIR)/*.o *.exe $(TEST_DIR)/*.o
endif
