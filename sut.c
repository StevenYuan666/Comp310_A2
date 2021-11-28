#include "sut.h"
#include "queue.h"
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#include "help.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <fcntl.h>

// C-EXEC
pthread_t cpuExec;
pthread_t cpuExec2;

// I-EXEC
pthread_t ioExec;

// Mutexes for C-EXEC and I-EXEC
pthread_mutex_t cpuMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ioMutex = PTHREAD_MUTEX_INITIALIZER;

// Main user thread, next cpu thread, next io thread
ucontext_t mainContext;
ucontext_t nextCpu;
ucontext_t mainContext2;
ucontext_t nextCpu2;

// Global variable to count the number of threads
int numThreads;

// Global variable to express the number of cpus
int numOfCpu = 2;

// Queues for cpu and IO
struct queue taskReadyQueue;
struct queue waitQueue;

// Flag to exit
bool ifExit;

void *cexecScheduler(){
    // An infinite loop
    while(true){
        if(ifExit){
            pthread_exit(NULL);
        }

        // Avoid the race condition
        pthread_mutex_lock(&cpuMutex);
        // Pick the next task
        struct queue_entry *nextTask = queue_pop_head(&taskReadyQueue);
        pthread_mutex_unlock(&cpuMutex);

        // Execute the task if the next task is not empty
        if(nextTask){
            nextCpu = *(ucontext_t *)nextTask->data;
            swapcontext(&mainContext, &nextCpu);
        }
        // Sleep for a while
        nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
}

void *cexecScheduler2(){
    // An infinite loop
    while(true){
        if(ifExit){
            pthread_exit(NULL);
        }
        // Avoid the race condition
        pthread_mutex_lock(&cpuMutex);
        // Pick the next task
        struct queue_entry *nextTask = queue_pop_head(&taskReadyQueue);
        pthread_mutex_unlock(&cpuMutex);

        // Execute the task if the next task is not empty
        if(nextTask){
            nextCpu2 = *(ucontext_t *)nextTask->data;
            swapcontext(&mainContext2, &nextCpu2);
        }
        // Sleep for a while
        nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
}

void *iexecScheduler(){
    // Pick the next task
    struct queue_entry *nextTask;

    // An infinite loop
    while(true){
        if(ifExit){
            pthread_exit(NULL);
        }
        // Avoid the race condition
        pthread_mutex_lock(&ioMutex);
        nextTask = queue_pop_head(&waitQueue);
        pthread_mutex_unlock(&ioMutex);

        // Execute the task if the next task is not empty
        if(nextTask){
            iodesc *nextIO = (iodesc *)nextTask->data;
            // sut_open()
            if(nextIO->openFlag){
                int fd = open(nextIO->fileName, O_RDWR);
                nextIO->fd = fd;
                nextIO->ifDone = true;
            }
            // sut_write()
            else if(nextIO->writeFlag){
                char *buf = nextIO->buff;
                write(nextIO->fd, buf, nextIO->buffSize);
                nextIO->ifDone = true;
            }
            // sut_read()
            else if(nextIO->readFlag){
                read(nextIO->fd, nextIO->buff, nextIO->buffSize);
                nextIO->ifDone = true;
            }
            // sut_close()
            else if(nextIO->closeFlag){
                close(nextIO->fd);
                nextIO->ifDone = true;
            }
        }
        // Sleep for a while
        nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
}

void sut_init(){
    // Initialize the number of threads
    numThreads = 0;

    // Initialize two queues
    taskReadyQueue = queue_create();
    queue_init(&taskReadyQueue);
    waitQueue = queue_create();
    queue_init(&waitQueue);

    // Initialize the flag
    ifExit = false;

    // Get context to set the main user thread
    getcontext(&mainContext);
    getcontext(&mainContext2);

    // Initialize two kernel threads as cpu and io
    pthread_create(&cpuExec, NULL, cexecScheduler, NULL);
    pthread_create(&ioExec, NULL, iexecScheduler, NULL);
    // Initialize the second cpu if need
    if(numOfCpu == 2){
        pthread_create(&cpuExec2, NULL, cexecScheduler2, NULL);
    }
}

//Similar to TA's example
bool sut_create(sut_task_f fn){
    // Thread descriptor
    threaddesc *taskDescriptor;
    taskDescriptor = malloc(sizeof(threaddesc));
    

    // Create an user level thread
    // Save the current state
    getcontext(&(taskDescriptor->threadcontext));
    taskDescriptor->threadid = numThreads;
    taskDescriptor->threadstack = (char *) malloc(THREAD_STACK_SIZE);
    taskDescriptor->threadcontext.uc_stack.ss_sp = taskDescriptor->threadstack;
    taskDescriptor->threadcontext.uc_stack.ss_size = THREAD_STACK_SIZE;
    taskDescriptor->threadcontext.uc_link = 0;
    taskDescriptor->threadcontext.uc_stack.ss_flags = 0;
    taskDescriptor->threadfunc = &fn;

    // Make context here
    makecontext(&(taskDescriptor->threadcontext), fn, 0);

    // Increase the number of threads
    numThreads ++;

    // Use the mutex to avoid the race condition
    struct queue_entry *newTask = queue_new_node(&(taskDescriptor->threadcontext));
    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, newTask);
    pthread_mutex_unlock(&cpuMutex);


    // Check if the insertion is successful
    struct queue_entry *p;
    bool result;
    STAILQ_FOREACH(p, &taskReadyQueue, entries) {
        result = (p == newTask);
    }
    return result;
}

// preempt the task and put it back to the queue
void sut_yield(){
    
    pthread_t tid = pthread_self();

    // put the current context back to the queue
    ucontext_t currentContext;
    getcontext(&currentContext);
    struct queue_entry *tmp = queue_new_node(&currentContext);

    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, tmp);
    pthread_mutex_unlock(&cpuMutex);

    // Determine which cpu we should swap back
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&currentContext, &mainContext);
    }
    else{
        swapcontext(&currentContext, &mainContext2);
    }
}

// kill the running task and no longer put it back
void sut_exit(){
    nanosleep((const struct timespec[]){{0, 100000L}}, NULL);

    // termiante all kernel thread if there is no more tasks
    pthread_t tid = pthread_self();
    pthread_mutex_lock(&cpuMutex);
    pthread_mutex_lock(&ioMutex);
    if(queue_peek_front(&taskReadyQueue) == NULL && queue_peek_front(&waitQueue) == NULL){
        ifExit = true;
        return;
    }
    pthread_mutex_unlock(&ioMutex);
    pthread_mutex_unlock(&cpuMutex);

    // Determine which cpu we should swap back
    ucontext_t currentContext;
    getcontext(&currentContext);
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&currentContext, &mainContext);
    }
    else{
        swapcontext(&currentContext, &mainContext2);
    }
}

int sut_open(char *fname){
    pthread_t tid = pthread_self();

    // struct the io thread
    iodesc *nextIO;
    nextIO = malloc(sizeof(iodesc));
    nextIO->fileName = fname;
    nextIO->openFlag = true;
    nextIO->readFlag = false;
    nextIO->writeFlag = false;
    nextIO->closeFlag = false;
    nextIO->ifDone = false;
    nextIO->buff = NULL;
    

    struct queue_entry *tmp = queue_new_node(nextIO);
    pthread_mutex_lock(&ioMutex);
    queue_insert_tail(&waitQueue, tmp);
    pthread_mutex_unlock(&ioMutex);

    ucontext_t current;
    getcontext(&current);
    nextIO->ioContext = current;

    struct queue_entry *putBack = queue_new_node(&current);
    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, putBack);
    pthread_mutex_unlock(&cpuMutex);

    // Determine which cpu we should swap back
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&current, &mainContext);
    }
    else{
        swapcontext(&current, &mainContext2);
    }

    while(!(nextIO->ifDone));
    int result = nextIO->fd;
    free(nextIO);
    return result;
}

void sut_write(int fd, char *buf, int size){
    pthread_t tid = pthread_self();

    // struct the io thread
    iodesc *nextIO = malloc(sizeof(iodesc));
    nextIO->writeFlag = true;
    nextIO->openFlag = false;
    nextIO->readFlag = false;
    nextIO->closeFlag = false;
    nextIO->fd = fd;
    nextIO->buff = buf;
    nextIO->buffSize = size;
    nextIO->ifDone = false;

    struct queue_entry *tmp = queue_new_node(nextIO);
    pthread_mutex_lock(&ioMutex);
    queue_insert_tail(&waitQueue, tmp);
    pthread_mutex_unlock(&ioMutex);

    ucontext_t current;
    getcontext(&current);
    nextIO->ioContext = current;

    struct queue_entry *putBack = queue_new_node(&current);
    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, putBack);
    pthread_mutex_unlock(&cpuMutex);
    // Determine which cpu we should swap back
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&current, &mainContext);
    }
    else{
        swapcontext(&current, &mainContext2);
    }

    while(!(nextIO->ifDone));
    free(nextIO);
}

void sut_close(int fd){
    pthread_t tid = pthread_self();
    // struct the io thread
    iodesc *nextIO = malloc(sizeof(iodesc));
    nextIO->closeFlag = true;
    nextIO->writeFlag = false;
    nextIO->openFlag = false;
    nextIO->readFlag = false;
    nextIO->fd = fd;
    nextIO->ifDone = false;

    struct queue_entry *tmp = queue_new_node(nextIO);
    pthread_mutex_lock(&ioMutex);
    queue_insert_tail(&waitQueue, tmp);
    pthread_mutex_unlock(&ioMutex);

    ucontext_t current;
    getcontext(&current);
    nextIO->ioContext = current;

    struct queue_entry *putBack = queue_new_node(&current);
    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, putBack);
    pthread_mutex_unlock(&cpuMutex);

    // Determine which cpu we should swap back
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&current, &mainContext);
    }
    else{
        swapcontext(&current, &mainContext2);
    }


    while(!(nextIO->ifDone));
    free(nextIO);
}

char *sut_read(int fd, char *buf, int size){
    pthread_t tid = pthread_self();
    // struct the io thread
    iodesc *nextIO = malloc(sizeof(iodesc));
    nextIO->readFlag = true;
    nextIO->writeFlag = false;
    nextIO->openFlag = false;
    nextIO->closeFlag = false;
    nextIO->fd = fd;
    nextIO->buff = buf;
    nextIO->buffSize = size;
    nextIO->ifDone = false;

    struct queue_entry *tmp = queue_new_node(nextIO);
    pthread_mutex_lock(&ioMutex);
    queue_insert_tail(&waitQueue, tmp);
    pthread_mutex_unlock(&ioMutex);

    ucontext_t current;
    getcontext(&current);
    nextIO->ioContext = current;
    struct queue_entry *putBack = queue_new_node(&current);
    pthread_mutex_lock(&cpuMutex);
    queue_insert_tail(&taskReadyQueue, putBack);
    pthread_mutex_unlock(&cpuMutex);
    // Determine which cpu we should swap back
    if(pthread_equal(tid, cpuExec)){
        swapcontext(&current, &mainContext);
    }
    else{
        swapcontext(&current, &mainContext2);
    }


    while(!(nextIO->ifDone));
    char *result = nextIO->buff;
    free(nextIO);
    return result;
}

void sut_shutdown(){
    pthread_join(cpuExec, NULL);
    pthread_join(ioExec, NULL);
    // Join the second 
    if(numOfCpu == 2){
        pthread_join(cpuExec2, NULL);
    }
    pthread_mutex_destroy(&cpuMutex);
    pthread_mutex_destroy(&ioMutex);
}
