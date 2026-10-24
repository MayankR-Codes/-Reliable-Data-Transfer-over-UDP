CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lws2_32

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests

# Object files
OBJS_COMMON = $(SRC_DIR)/packet.o $(SRC_DIR)/socket_utils.o

# Target executables
TARGETS = emulator.exe test_packet.exe test_loopback.exe test_emulator.exe

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

test: all
	@echo Running test_packet...
	@./test_packet.exe
	@echo Running test_loopback...
	@./test_loopback.exe

ifeq ($(OS),Windows_NT)
clean:
	-cmd /c "del /Q /F src\*.o tests\*.o *.exe 2>nul"
else
clean:
	rm -f $(SRC_DIR)/*.o *.exe $(TEST_DIR)/*.o
endif
