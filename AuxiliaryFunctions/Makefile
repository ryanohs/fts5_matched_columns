CC=gcc
CFLAGS=-Wall -dynamiclib 
OUTPUT=libmatched_columns.dylib

all:
	$(CC) $(CFLAGS) -o $(OUTPUT) -fPIC matched_columns.c libe_sqlite3.dylib

clean:
	rm -f $(OUTPUT)