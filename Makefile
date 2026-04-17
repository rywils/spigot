CC = gcc
CFLAGS = -Wall
LDFLAGS = -lpthread

TARGET = spigot
SRC = main.c server.c http.c file.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
