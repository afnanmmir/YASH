#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> 
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#include <malloc.h> // malloc/malloc.h on macs
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>


int main(){
    int jobid = 1;
    char plus_or_minus = '+';
    char* status = "Running";
    char* command = "ls -l";
    int jobid2 = 2;
    char plus_or_minus2 = '-';
    char* status2 = "Done";
    char* command2 = "ls -l | wc";
    printf("[%d]%c    %-10s %s\n", jobid, plus_or_minus, status, command);
    printf("[%d]%c    %-10s %s\n", jobid2, plus_or_minus2, status2, command2);
}