CC = gcc

CFLAGS = -Wall -Wextra -std=c11

TARGET = moc

SRC = src/main.c src/rle.c src/middleout.c src/pattern.c src/reference.c src/moc.c src/baseline.c

all:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
