CC = gcc

all: oss user_proc

oss: oss.c user_proc.h
	gcc -std=gnu11 -o oss oss.c

user_proc: user_proc.c user_proc.h
	gcc -std=gnu11 -o user_proc user_proc.c

clean:
	$(RM) user_proc oss *.txt
