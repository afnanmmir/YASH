yash: yash.c
	gcc yash.c -o yash -lreadline

yash-debug: yash.c
	gcc -g yash.c -o yash -lreadline