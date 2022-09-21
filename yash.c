#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <errno.h>


#define MAX_CMD_LENGTH 2000
#define RUNNING 0
#define STOPPED 1
#define DONE 2

pid_t process_group;
pid_t process_group_in;
pid_t process_group_out;
// pid_t process_group_err;

int jobCount;
/* Array for strings to print for state of a process */
const char* state_arr[3] = {
    "Running", "Stopped", "Done"
};

typedef struct Process{
    pid_t pid;
    int in;
    int out;
    int err;
    int status;
    bool pipe_in; // whether it is receiving input from a pipe
    bool pipe_out; // whether it is sending output to a pipe.
    char** cmd;
    int cmdLength;
} Process;

typedef struct Job{
    int jobNum;
    pid_t pgid;
    int status; // 0 = running, 1 = stopped, 2 = done
    char** parsedProcesses;
    char* originalCmd;
    Process* process1;
    Process* process2;
    bool foreground;
    struct Job* next;
} Job;

int getJobNum();
void removeJob(Job* j);

/* Used to find the index of a string in an array of strings. Returns -1 if not found */
int indexOfCharacter(char** toks, char* c){
    int index = -1;
    for(int i = 0; toks[i] != NULL; i++){
        if(strcmp(toks[i], c) == 0){
            index = i;
        }
    }
    return index;
}

/* Parse a command into its command and parameters */
char** parseCommand(char* cmd){
    int numTokens = 1;
    for(int i=0; cmd[i] != '\0';i++){
        if(cmd[i] == ' '){
            numTokens++;
        }
    }
    char** parsedToks = malloc(sizeof(char*) * (numTokens + 1));
    char* saveptr;
    char* token;
    for(int j = 0; ;cmd = NULL,  j++){
        token = strtok_r(cmd, " ", &saveptr);
        if(token == NULL){
            parsedToks[j] = NULL;
            break;
        }
        parsedToks[j] = token;
    }
    return parsedToks;
}

/* Finds an ampersand in a command in order to check if foreground or background process */
bool findAmpersand(char** cmd){
    int ampersandIndex = indexOfCharacter(cmd, "&");
    if(ampersandIndex > -1){
        cmd[ampersandIndex] = NULL;
    }

    return ampersandIndex != -1;
}

/* Remove leading and trailing whitespaces from a command */
void trimSpaces(char* st1){
    char *end;
    size_t length = strlen(st1);

    // Trim leading space
    int count = 0;
    while(st1[count] == ' ') {
        count++;
        length--;
    }

    if(count != 0){
        int i = 0;
        while(st1[i + count] != '\0'){
            st1[i] = st1[i + count];
            i++;
        }
        st1[i] = '\0';
    }

    // Trim trailing space
    end = st1 + strlen(st1) - 1;
    while(end > st1 && isspace((unsigned char)*end)){
        end--;
        length--;
    } 
    st1[length] = '\0';
}

/* Free memory allocated for parsed tokens in a process command */
void destroyCommand(char** parsedCmd){
    for(int i=0; parsedCmd[i] != NULL; i++){
        free(parsedCmd[i]);
    }
    free(parsedCmd);
}


Process* createProcess(char** parsedCmd){
    Process* process = malloc(sizeof(Process));
    memset(process, 0, sizeof(Process));
    int cmdLength = 0;
    process->cmd = parsedCmd;
    process->in = 0; // default standard in TODO: change all of these to macros
    process->out = 1; // default standard out 
    process->err = 2; // default standard error
    process->pipe_in = false;
    process->pipe_out = false;
    process->pid = 0;
    // check for redirects
    for(int i = 0; parsedCmd[i] != NULL; i++){
        char* tok = parsedCmd[i];
        if(strcmp(parsedCmd[i], ">") == 0){ //file redirect output
            parsedCmd[i] = NULL;
            char* file_path = parsedCmd[i+1];
            int fd = creat(file_path, 0644);
            if(fd > 0){
                process->out = fd;
            } else {
                return (Process*) NULL;
            }
        }else if(strcmp(parsedCmd[i], "<") == 0){ //file redirect input
             //file redirect input
            parsedCmd[i] = NULL;
            char* file_path = parsedCmd[i+1];
            int fd = open(file_path, O_RDONLY, S_IRWXU);
            if(fd > 0){
                process->in = fd;
            }else{
                return (Process*) NULL;
            }
        }else if(strcmp(parsedCmd[i], "2>") == 0){ //file redirect stderr
            parsedCmd[i] = NULL;
            char* file_path = parsedCmd[i+1];
            int fd = creat(file_path, 0644);
            if(fd > 0){
                process->err = fd;
            } else { 
                return (Process*) NULL;
            }
        }
        cmdLength++;
    }
    process->status = RUNNING;
    process->cmdLength = cmdLength;
    return process;
}

/* Free memory allocated for a process */
void destroyProcess(Process* p){
    // free(p->cmd[0]);
    free(p->cmd);
    free(p);
}

void executeProcess(Process* process, int* pfd, Job* j){
    // pid_t cpid;
    // cpid = fork();
    // if(cpid == 0){
    // printf("Hello\n");
    if(process->pipe_in == true && process->in == 0){ // if pipe for stdin used
        dup2(pfd[0], 0);
        close(pfd[1]);
    }else if(process->in != 0){ // if file redirect for stdin used
        dup2(process->in, 0);
    }
    if(process->pipe_out == true && process->out == 1){ // if pipe for stdout used
        dup2(pfd[1], 1);
        close(pfd[0]);
    }else if(process->out != 1){ // if file redirect for stdout used
        dup2(process->out, 1);
        close(process->out);
    }
    if(process->err != 2){ // if file redirect used for stderr
        dup2(process->err, 2);
    }
    int status = execvp(process->cmd[0], process->cmd);
    if(errno != ENOTDIR && errno != ENOENT){
        // perror("exec");
    }
    exit(EXIT_FAILURE);
    // }
}

void printJob(Job* j);

Job* head = NULL;

/* Adds a job to the linked list of jobs */
void addJob(Job* j){
    Job* current = head;
    if(head == NULL){
        head = j;
        return;
    }else{
        while(current->next != NULL){
            current = current->next;
        }
        current->next = j;
    }
}


Job* createJob(char* cmd){
    int numTokens = 1;
    Job* job = malloc(sizeof(Job));
    job->pgid = 0;
    job->originalCmd = strdup(cmd);
    for(int i = 0; cmd[i] != '\0'; i++){ // Checks for pipes
        if(cmd[i] == '|'){
            numTokens++;
        }
    }
    char** parsedToks = malloc(sizeof(char*) * (numTokens));
    char* saveptr;
    char* token;
    for(int j = 0; ;cmd = NULL, j++){
        token = strtok_r(cmd, "|", &saveptr);
        if(token == NULL){
            break;
        }
        trimSpaces(token);
        parsedToks[j] = token;
    }
    if(numTokens == 1){
        char** parsedProcess = parseCommand(parsedToks[0]);
        job->process1 = createProcess(parsedProcess);
        if(job->process1 == NULL){
            return (Job*) NULL;
        }
        job->process2 = NULL;
        job->foreground = !findAmpersand(job->process1->cmd);
    }else if(numTokens == 2){
        char** parsedProcess_1 = parseCommand(parsedToks[0]);
        job->process1 = createProcess(parsedProcess_1);
        char** parsedProcess_2 = parseCommand(parsedToks[1]);
        job->process2 = createProcess(parsedProcess_2);
        if(job->process1 == NULL || job->process2 == NULL){
            return (Job*) NULL;
        }
        job->foreground = !findAmpersand(job->process2->cmd);
    }
    job->parsedProcesses = parsedToks;
    job->next = NULL; // created job will be at the end of the linked list
    job->status = RUNNING; // Default state for job is running
    job->jobNum = getJobNum(); // TODO: Figure out how to properly do job ID

    // add job to the linked list
    addJob(job);

    return job;
}

/* Free memory allocated for a job */
void destroyJob(Job* j){
    free(j->parsedProcesses);
    destroyProcess(j->process1);
    if(j->process2 != NULL){
        destroyProcess(j->process2);
    }
    free(j);
}

/* Remove job from linked list and destroy it */
void removeJob(Job* j){
    Job* current = head;
    Job* previous = NULL;
    if(j == head){
        // printf("I am here\n");
        Job* temp = head->next;
        destroyJob(head);
        head = temp;
        if(head == NULL){
            // printf("Head is Null\n");
        }
        jobCount--;
        return;
    }
    current = head->next;
    previous = head;
    while(current != NULL){
        if(current == j){
            Job* temp = current;
            previous->next = current->next;
            destroyJob(temp);
            jobCount--;
            break;
        }else{
            previous = current;
            current = current->next;
        }
    }
}

void checkConditionOneProcess(Job* j, int jobStatus, pid_t waitStatus){
    if(waitStatus == 0){
        j->status = j->status;
    }else if(WIFEXITED(jobStatus)){
        j->status = DONE;
    }else if(WIFCONTINUED(jobStatus)){
        j->status = RUNNING;
    }else if(WIFSTOPPED(jobStatus)){
        j->status = STOPPED;
    }
}

void checkConditionTwoProcesses(Job* j, int jobStatus1, pid_t waitStatus1, int jobStatus2, pid_t waitStatus2){
    if(waitStatus1 == -1){
        j->process1->status = DONE;
    }else if(waitStatus1 == 0){
        j->process1->status = j->process1->status;
    }else if(WIFEXITED(jobStatus1)){
        j->process1->status = DONE;
    }else if(WIFCONTINUED(jobStatus1)){
        j->process1->status = RUNNING;
    }else if(WIFSTOPPED(jobStatus1)){
        j->process1->status = STOPPED;
    }
    if(waitStatus2 == -1){
        j->process2->status = DONE;
    }else if(waitStatus2 == 0){
        j->process2->status = j->process2->status;
    }else if(WIFEXITED(jobStatus2)){
        j->process2->status = DONE;
        // printf("Hello2\n");
    }else if(WIFCONTINUED(jobStatus2)){
        j->process2->status = RUNNING;
    }else if(WIFSTOPPED(jobStatus2)){
        j->process2->status = STOPPED;
    }
    if(j->process1->status == DONE && j->process2->status == DONE){
        j->status = DONE;
    }else if(j->process1->status == STOPPED && j->process2->status == STOPPED){
        j->status = STOPPED;
    } else if (j->process1->status == RUNNING || j->process2->status == RUNNING){
        j->status = RUNNING;
    } else if(j->process1->status == STOPPED || j->process2->status == STOPPED){
        j->status = STOPPED;
    }
}

void updateJobStatus(Job* j, int printOut){
    int jobStatus;
    volatile pid_t waitStatus;
    volatile pid_t waitStatus2;
    int jobStatus2;
    waitStatus = waitpid((j->process1->pid), &jobStatus, WUNTRACED | WNOHANG); // check status of a job
    if(j->process2 != NULL){
        waitStatus2 = waitpid((j->process2->pid), &jobStatus2, WUNTRACED | WNOHANG);
    }
    if(j->process2 == NULL){
        checkConditionOneProcess(j, jobStatus, waitStatus);
    }else{
        checkConditionTwoProcesses(j, jobStatus, waitStatus, jobStatus2, waitStatus2);
    }
    if(j->status == DONE && printOut == 1){ // If job is done it should print
        printJob(j);
    }
}

/* Waits on a job. Used for when the job is in the foreground. */
void waitJob(Job* j){
    int status;
    int status2;
    waitpid((j->process1->pid), &status, WUNTRACED);
    if(j->process2 != NULL){
        waitpid((j->process2->pid), &status2, WUNTRACED);
    }

    if(j->process2 == NULL){
        if(WIFEXITED(status) || WIFSIGNALED(status)){
            removeJob(j);
        }else{
            j->status = STOPPED;
            j->process1->status = STOPPED;
        }
    }else{
        if((WIFSIGNALED(status) || WIFEXITED(status)) && (WIFEXITED(status2) || WIFSIGNALED(status2))){
            removeJob(j);
        }else{
            // printf("Hello");
            j->status = STOPPED;
            j->process1->status = STOPPED;
            j->process2->status = STOPPED;
        }
    }
    
    tcsetpgrp(STDIN_FILENO, process_group_in);
    tcsetpgrp(STDOUT_FILENO, process_group_out);
}

void executeJob(Job* job){
    int status;
    int pfd[2]; // pipe array in case there are 2 commands
    pid_t lcpid; // left child pid 
    pid_t rcpid; // right child pid
    if(job->process2 != NULL){ // if there are 2 processes in the job, setup the pipe
        pipe(pfd);
        job->process1->pipe_out = true;
        job->process2->pipe_in = true;
    }
    lcpid = fork(); // execute the left child first
    if(lcpid == 0){
        signal(SIGINT, SIG_DFL);        // default signal handlers
        signal(SIGTSTP, SIG_DFL);
        pid_t pgid = getpgid(getpid());
        setpgid(0, getpid());
        if(job->foreground == true){ // if the job is to be performed in the foreground, give the process group control of the terminal
            tcsetpgrp(0, pgid);
            tcsetpgrp(1, pgid);
        }
        executeProcess(job->process1, pfd, job); // execute the process
    } else {
        setpgid(lcpid, 0);
        job->process1->pid = lcpid; // when parent gets control back. Assign the job the process group id of the left child
        job->pgid = lcpid;
    }
    if(job->process2 != NULL){ // if right child process exists, execute the right process
        rcpid = fork();
        if(rcpid == 0){
            signal(SIGINT, SIG_DFL);        // default signal handlers
            signal(SIGCHLD, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            setpgid(0, job->pgid); // give the right process the same process group id as the left child
            executeProcess(job->process2, pfd, job);
        } else {
            setpgid(rcpid, job->pgid);
            job->process2->pid = rcpid; 
        }
    }
    if(job->process2 != NULL){ // close pipe if there are 2 children
        close(pfd[0]);
        close(pfd[1]); 
    }
    if(job->foreground == true){ // if the job is in the foreground, terminal waits for the process group to exit before it regains control
        waitJob(job);
    } else{ // if job is in background, do not wait for process group to exit
        int waitStatus = waitpid(-1*(job->pgid),&status, WNOHANG | WUNTRACED);
        // printf("%d\n", waitStatus);
        // if(WEXITSTATUS(status) == 1){
        //     printf("I made it in here\n");
        //     removeJob(job);
        // }
    } 
    // give control back to the parent process
    tcsetpgrp(0, process_group_in);
    tcsetpgrp(1, process_group_out);
}

int findPlusOrMinus() {
    int maxJobNum = 1;
    Job* current = head;
    bool allDone = true;
    while(current != NULL){
        if(current->status != DONE){
            allDone = false;
        }
        current = current->next;
    }
    current = head;
    if(!allDone){
        while(current != NULL){
            if(current->status != DONE){
                maxJobNum = current->jobNum;
            }
        current = current->next;
        }
    } else {
        while(current != NULL){
            maxJobNum = current->jobNum;
            current = current->next;
        }
    }
    // printf("%d\n", allDone);
    return maxJobNum;
}

void printJob(Job* j){
    int jobid = j->jobNum;
    int plusJob = findPlusOrMinus();
    char plus_or_minus = (jobid == plusJob) ? '+' : '-';
    printf("[%d]%c    %-10s %s\n", jobid, plus_or_minus, state_arr[j->status], j->originalCmd);
}

void printJobs(){
    Job* current = head;
    while(current != NULL){
        printJob(current);
        current = current->next;
    }
}

/* Finds the next number a job can hold */
int getJobNum(){
    Job* current = head;
    int currentNum = 0;
    while(current != NULL){
        if(current->jobNum > currentNum){
            currentNum = current->jobNum;
        }
        current = current->next;
    }
    return currentNum + 1;
}

void cleanJobs(){
    Job* current = head;
    while(current != NULL){
        if(current->status == DONE){
            Job* temp = current;
            current = current->next;
            removeJob(temp);
        }else{
            current = current->next;
        }
    }
}

void updateJobs(int printOut, int cleanOption){
    Job* current = head;
    while(current != NULL){
        updateJobStatus(current, printOut);
        current = current->next;
    }
    if(cleanOption == 1){
       cleanJobs(); 
    }   
}

// void waitpidWrapper(Job* j, int option){
//     int jobStatus;
//     int jobStatus2;
//     int waitStatus;
//     int waitStatus2;
//     waitStatus = waitpid(-1*(j->pgid), &jobStatus, option);
// }

void jobs(){
    updateJobs(0, 0); // update the status of the jobs without printing anything
    printJobs(); // print the jobs
    cleanJobs(); // remove all done jobs from the linked list
}

void fg(){
    if(head == NULL){
        printf("yash: fg: current: no such job\n");
        return;
    }
    updateJobs(1,0);
    cleanJobs();
    Job* foregrounded = head;
    while(foregrounded->next != NULL){
        foregrounded = foregrounded->next;
    }
    // printf("Hello\n");
    if(foregrounded->status == STOPPED){
        foregrounded->status = RUNNING;
        kill(-1*(foregrounded->pgid), SIGCONT);
    }
    printf("%s\n",foregrounded->originalCmd);
    tcsetpgrp(STDIN_FILENO,foregrounded->pgid);
    waitJob(foregrounded);
}

void printBgJob(Job* j){
    printf("[%d]+ %s\n", j->jobNum, j->originalCmd);
}

void bg(){
    if(head == NULL){
        printf("yash: bg: current: no such job\n");
        return;
    }
    updateJobs(1,0);
    Job* background = head;
    Job* current = head;
    while(current != NULL){
        if(current->status == STOPPED){
            background = current;
        }
        if(background->status != STOPPED){
            background = current;
        }
        current = current->next;
    }
    if(background->status != STOPPED){
        printf("yash: bg: job %d already in background\n", background->jobNum);
        return;
    }
    background->status = RUNNING;
    background->process1->status = RUNNING;
    if(background->process2 != NULL){
        background->process2->status = RUNNING;
    }
    printBgJob(background);
    kill(-1*(background->pgid), SIGCONT);

}


void init(){
    head = NULL;
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    jobCount = 0;
    process_group_in = tcgetpgrp(0);
    process_group_out = tcgetpgrp(1);
    process_group = getpid();
}


int main(){
    char* cmd;
    char** parsedCommand;
    // setbuf(stdout, NULL);
    // int count = 0;
    init();
    while(1){
        pid_t cpid;
        cmd = readline("# ");
        if(!cmd){
            _exit(0);
        }
        if(strcmp(cmd, "\0") == 0){
            updateJobs(1, 1);
            continue;
        }

        if(strcmp(cmd, "jobs") == 0){
            jobs();
            continue;
        }

        if(strcmp(cmd, "fg") == 0){
            fg();
            continue;
        }

        if(strcmp(cmd, "bg") == 0){
            bg();
            continue;
        }

        Job* job = createJob(cmd);
        if(job == NULL){
            continue;
        }
        executeJob(job);
        updateJobs(1, 1);
    }   
}