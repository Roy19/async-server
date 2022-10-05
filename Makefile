all:
	$(CC) -O3 src/*.c -o server

clean:
	rm -f ./server