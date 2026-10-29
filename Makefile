# FoodRush – Makefile (MSYS2 UCRT64 / Windows GCC)
# Usage: mingw32-make  (or make inside MSYS2 shell)

CC      = gcc
TARGET  = foodrush.exe
SRCDIR  = src
INCDIR  = include

# Source files
SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/threadpool.c \
       $(SRCDIR)/database.c \
       $(SRCDIR)/json_utils.c \
       $(SRCDIR)/router.c \
       $(SRCDIR)/handlers.c

# Compiler flags
CFLAGS  = -Wall -Wextra -O2 -g \
          -I$(INCDIR) \
          -IC:/msys64/ucrt64/include \
          -DSQLITE_THREADSAFE=1 \
          -D_WIN32_WINNT=0x0601

# Linker flags
LDFLAGS = -LC:/msys64/ucrt64/lib \
          -lsqlite3 \
          -lpthread \
          -lws2_32 \
          -static-libgcc

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs:
	@if not exist db mkdir db
	@if not exist logs mkdir logs

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
	@echo.
	@echo ======================================================
	@echo   Build successful: $(TARGET)
	@echo   Run with: .\$(TARGET)
	@echo   Then open: http://localhost:8080
	@echo ======================================================

run: all
	.\$(TARGET)

clean:
	@if exist $(TARGET) del /Q $(TARGET)
	@if exist db\foodrush.db del /Q db\foodrush.db
	@echo Cleaned.
