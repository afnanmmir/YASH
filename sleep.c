#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <stdbool.h>
// #include <stdint.h>

int main(){
    long i = 0;
    int sum = 0;
    while(i < 1000000000l * 4){
        // sum += i;
        i++;
    }
}