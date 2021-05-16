#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define LINESIZE 1024 
#define NUMLINES 100 
#define NUMPARTITIONS 4 
#define TOTALMEMORY 1000 

static int PARTITIONS[4] = {500, 250, 150, 100};

//the struct that keeps track of a process
typedef struct pcb {
    int pid;
    int arrivalTime;
    int totalCPUTime;
    int ioFreq;
    int ioDur;
    
    int timeRemain; //keeps track of the remaining time of the cpu
    int ioRemain; //keeps track of the remaining time of I/O
    int ioFreqCounter; //keeps track of the time for the next I/O

    int oldState;
    int currentState;

    int priority; //keeps track of the priority of the process
    
    int memory; //keeps track of the memory that the process needs
    int memoryParition; //keeps track of the memory partition that the process uses

    int RRRemainCounter; //keeps track of the round robin time 
    
    struct pcb *next;
} pcb_t;

// the struct that keeps track of the all memory information
typedef struct memory {
    //the memory partitions in Mb (total memory is 1Gb)
    int memoryPartitions[NUMPARTITIONS];// 
    int numFreePartitions;
    int numUsedPartitions;
    int usableMemory; 
    int freeMemory;
} memory_t;

// the struct that keeps track of the timing information
typedef struct timing {
    int numProcesses;
    int cpuBurst;
    int waiting;
    int total;
    int turnaroundTime;
} timing_t;

// the struct that keeps track of the queues
typedef struct {
    pcb_t *front;  // Points to the node at the head of the queue's linked list. 
    pcb_t *rear;   // Points to the node at the tail of the queue's linked list.
    int size;      // The # of nodes in the queue's linked list. 
} queue_t;


// function signatures
pcb_t *pcb_construct(int processInfo[14]);
queue_t *queue_construct(void);
void enqueue(queue_t *queue, pcb_t* p);
void insert(queue_t *queue, pcb_t* p);
pcb_t* dequeue(queue_t *queue);
int readFile (FILE* inputf, char file[NUMLINES][LINESIZE]);
void initializeProcesses(int processes[][14], char file[NUMLINES][LINESIZE], int numCommands);
void addProcessestoNew(queue_t* new_queue, int processes[][14], char file[NUMLINES][LINESIZE], int numCommands);
void sortProcesses(int processes[][14], int numCommands);
void schedule (queue_t *queue, pcb_t* p, int schedule_type);
int allocateMemory(memory_t* mem, pcb_t* p);
void deallocateMemory(memory_t* mem, pcb_t* p);
void displayOutput(int clock, int pid, char state1[], char state2[], FILE* outputf);
void displayMemoryInfo(memory_t* mem, pcb_t* p, FILE* outputf);
void displayTimingInfo(timing_t* timing, FILE* outputf);

int main(int argc,char *argv[])
{
    char file[NUMLINES][LINESIZE];

    FILE *inputf = NULL; 
	FILE *outputf = NULL;

   
    inputf = fopen(argv[1], "r");
	outputf = fopen(argv[2], "w");

    int scheduler_type = 0;
    int RRTime = 1000;
    int memory = 0;

    //gets the flags from the input to change the default values 
    for (int i = 3; i < argc; i++){
        if (strcmp(argv[i],"-s") == 0){
            scheduler_type = atoi(argv[i+1]);
        } else if (strcmp(argv[i],"-t") == 0){
            RRTime = atoi (argv[i+1]);
        } else if (strcmp(argv[i],"-m") == 0){
            memory = atoi (argv[i+1]);
        }
    }

    int numCommands = readFile(inputf, file);

    //creating the memory of the system
    memory_t* mem = malloc(sizeof(memory_t));

    for (int i = 0; i < NUMPARTITIONS; i++){
        mem -> memoryPartitions[i] = PARTITIONS[i];
    }
    
    mem -> numFreePartitions = NUMPARTITIONS;
    mem -> numUsedPartitions = 0;
    mem -> freeMemory = TOTALMEMORY;
    mem -> usableMemory = TOTALMEMORY;

    //keeping track of the times for the system
    timing_t* timing = malloc(sizeof(timing_t));
    timing->cpuBurst = 0;
    timing->total = 0;
    timing->turnaroundTime = 0;
    timing->waiting = 0;
    timing->numProcesses = numCommands;

    //creating the three queues
    queue_t* new_queue = queue_construct();
    queue_t* ready_queue = queue_construct();
    queue_t* waiting_queue = queue_construct();

    //creating the 3 types processes in the system
    pcb_t* newProcess;
    pcb_t* runningProcess;
    pcb_t* waitingProcess;

    //initializign the processes    
    int processes[numCommands][14];
    
    initializeProcesses(processes, file, numCommands);

    sortProcesses(processes,numCommands);

    addProcessestoNew(new_queue, processes,file, numCommands);
    
    //initializng the flags
    int clock = 0; //the main clock for the system
    int cpuBusy = 0; //makes sure only one cpu is running at a given time 
    int terminated = 0;
    int waiting = 0;
    int deleted = 0;

    //header of the file
    printf("%5s %8s %12s %10s\n","TIME","PID", "PREVIOUS", "CURRENT");
    fprintf(outputf, "%5s %8s %12s %10s\n\n","TIME","PID", "PREVIOUS", "CURRENT");

    while (terminated < numCommands){ 
        //goes from new to ready
        while (new_queue->size > 0 && clock >= new_queue->front->arrivalTime && (!memory || allocateMemory(mem, new_queue->front))){
            if (memory){
                displayMemoryInfo(mem, new_queue->front, outputf);
            }
            newProcess = dequeue(new_queue);
            newProcess->oldState = 0;
            newProcess->currentState = 1;

            timing->cpuBurst += newProcess->totalCPUTime;
            timing->turnaroundTime -= clock;

            schedule(ready_queue, newProcess, scheduler_type);
            displayOutput(clock,newProcess->pid, "NEW", "READY",outputf);
        }

        //goes from ready to running
        if (cpuBusy==0){
            if (ready_queue->size >0){
                //goes from ready to running
                runningProcess = dequeue(ready_queue);
                runningProcess->oldState = 1;
                runningProcess->currentState = 2;
                displayOutput(clock,runningProcess->pid, "READY", "RUNNING",outputf);
                cpuBusy = 1;
            }
        }

        //process is waiting
        if (waiting_queue->size>0 && waiting==0){
            waitingProcess = dequeue(waiting_queue);
            waiting = 1;
        }

        if (waiting==1){
            waitingProcess->ioRemain-=1;
            if (waitingProcess->ioRemain==0){
                waiting = 0;
                waitingProcess->oldState = 3;
                waitingProcess->currentState = 2;
                waitingProcess->ioRemain = waitingProcess->ioDur;
                schedule(ready_queue, waitingProcess, scheduler_type);
                displayOutput(clock,waitingProcess->pid, "WAITING", "READY",outputf);
            }
        }

        //process is running
        if (cpuBusy==1){
            runningProcess->ioFreqCounter++;

            //process is terminated
            if (runningProcess->timeRemain == 0){
                cpuBusy = 0;
                runningProcess->oldState = 2;
                runningProcess->currentState = 4;
                if (memory){
                    deallocateMemory(mem, runningProcess);
                }
                
                timing->turnaroundTime += clock;

                displayOutput(clock,runningProcess->pid, "RUNNING", "TERMINATED",outputf);
                deleted = 1;
                terminated+=1;
            }

            //process' CPU time is over (goes back to READY only happens in RR)
            else if (scheduler_type == 2 && runningProcess->RRRemainCounter == RRTime + 1){
                cpuBusy = 0;
                runningProcess->oldState = 2;
                runningProcess->currentState = 1;
                runningProcess->RRRemainCounter = 0;
                schedule(ready_queue, runningProcess, scheduler_type);
                displayOutput(clock,runningProcess->pid, "RUNNING", "READY",outputf);
            }

            //goes to waiting
            else if (runningProcess->ioFreq!=0 && runningProcess->ioFreqCounter == runningProcess->ioFreq+1){
                cpuBusy = 0;
                runningProcess->oldState = 2;
                runningProcess->currentState = 3;
                runningProcess->ioFreqCounter = 0;
                schedule(waiting_queue, runningProcess, scheduler_type);
                displayOutput(clock,runningProcess->pid, "RUNNING", "WAITING",outputf);
            }

            else{
                runningProcess->timeRemain-=1;
                runningProcess->RRRemainCounter++;
            }

            if (deleted == 1){
                free(runningProcess);
                deleted = 0;
            }
        }
        
        clock++;
    }

    timing->total = clock;
    timing->waiting = timing->turnaroundTime - timing->cpuBurst;

    displayTimingInfo(timing, outputf);

    //freeing the used memory
    free(mem);
    free(timing);

    //closing the files
    fclose(inputf);
    fclose(outputf);
    
    return 0;
}

pcb_t *pcb_construct(int processInfo[14]){
        pcb_t* process = malloc(sizeof(pcb_t));

        process->pid = processInfo[0];
        process->arrivalTime = processInfo[1];
        process->totalCPUTime = processInfo[2];
        process->ioFreq = processInfo[3];
        process->ioDur = processInfo[4];
        process->timeRemain = processInfo[5];
        process->ioRemain = processInfo[6];
        process->ioFreqCounter  = processInfo[7];
        process->oldState = processInfo[8];
        process->currentState = processInfo[9];
        process->priority = processInfo[10];
        process->memory = processInfo[11];
        process->RRRemainCounter = processInfo[12];
        process->memoryParition = processInfo[13];
}

queue_t *queue_construct(void)
{
    queue_t *queue = malloc(sizeof(queue_t));
    // assert(queue != NULL);

    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    return queue;
}

/* adds an element to the end of the queue */
void enqueue(queue_t *queue, pcb_t* p)
{
    //assert(queue != NULL);
    if (queue->front == NULL) {
        queue->front = p;
    } else {
        queue->rear->next = p;
    }

    queue->rear = p;
    queue->size += 1;
}

/* inserts an element to the queue based on its priority */
void insert(queue_t *queue, pcb_t* p)
{
    //assert(queue != NULL);
    if (queue->size == 0) {
        queue->front = p;
        queue->rear = p;
    } else {
        pcb_t* prev = NULL;
        
        int inserted = 0; 

        for (pcb_t* curr = queue->front; curr!=NULL; curr = curr->next){
            if (p->priority < curr->priority && inserted == 0){
                inserted = 1;
                if (prev == NULL){
                    queue->front = p;
                    queue->front->next = curr;
                }
                else{
                    prev->next = p;
                    p->next = curr;
                }
            }
            prev = curr;
        }

        if (!inserted){
            queue->rear->next = p;
            queue->rear = p;
        }
    }

    queue->size += 1;
}

/* removes an element from the front of the queue */
pcb_t* dequeue(queue_t *queue)
{
    // assert(queue != NULL);
    int process[14];
    process[0]= queue->front->pid;
    process[1]= queue->front->arrivalTime;
    process[2]= queue->front->totalCPUTime;
    process[3]= queue->front->ioFreq;
    process[4]= queue->front->ioDur;
    process[5]= queue->front->timeRemain;
    process[6]= queue->front->ioRemain;
    process[7]= queue->front->ioFreqCounter;
    process[8]= queue->front->oldState;
    process[9]= queue->front->currentState;
    process[10]= queue->front->priority;
    process[11]= queue->front->memory;
    process[12]= queue->front->RRRemainCounter;
    process[13]= queue->front->memoryParition;

    pcb_t *node_to_delete = queue->front;
    queue->front = queue->front->next;
    
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    
    queue->size -= 1;

    pcb_t*node_deleted =pcb_construct(process); 

    return node_deleted;
}

/* Loads the values of the test file into an array of lines */
int readFile (FILE* inputf, char file[NUMLINES][LINESIZE]){
    int i =0;
    
    while(fgets(file[i], LINESIZE, inputf)) 
    {
        file[i][strlen(file[i]) - 1] = '\0';
        i++;
    }

    return i;
}

/* Loads the values into the array of line to an array of processes */
void initializeProcesses(int processes[][14], char file[NUMLINES][LINESIZE], int numCommands){
    printf("number of commands %d\n", numCommands);

    for (int i=0; i<numCommands; i++)
    {
        pcb_t* process = malloc(sizeof(pcb_t));
        sscanf(file[i] , "%d %d %d %d %d %d %d", &processes[i][0], &processes[i][1], &processes[i][2], &processes[i][3], &processes[i][4], &processes[i][10], &processes[i][11]);
        processes[i][5] = processes[i][2];
        processes[i][6] = processes[i][4];
        processes[i][7] = 0;
        processes[i][8] = 0;
        processes[i][9] = 0;
        processes[i][12] = 0;
        processes[i][13] = 0;
    }
}

/* adds all the processes from the array to the new queue */
void addProcessestoNew(queue_t* new_queue, int processes[][14], char file[NUMLINES][LINESIZE], int numCommands){
    for (int i=0; i<numCommands; i++)
    {
        pcb_t* process = pcb_construct(processes[i]);

        enqueue(new_queue,process);
    }
}

/* Sorts the processes based on the time they arrive at */
void sortProcesses(int processes[][14], int numCommands){
    for (int i=0; i<numCommands-1; i++){
        for (int j=i+1; j<numCommands; j++){
            if (processes[i][1] > processes[j][1]){
                for (int k=0; k<14;k++){
                    int temp = processes[i][k];
                    processes[i][k] = processes[j][k];
                    processes[j][k] = temp;
                }     
            }
        }
    }
}

/* Adds the process to the queue based on the proper scheduling algorithm */ 
void schedule (queue_t *queue, pcb_t* p, int schedule_type){
    // if fcfs or RR it just picks enqueue
    if (schedule_type == 0 || schedule_type == 2){
        enqueue(queue, p);
    }
    // if the scheduling is priority
    else if (schedule_type == 1 ){
        insert(queue, p);
    }
}

/* checks to see if it can allocated memory to the process */
int allocateMemory(memory_t* mem, pcb_t* p){
    for (int i = 0; i < NUMPARTITIONS; i++){
        if (mem->memoryPartitions[i] >= p->memory && mem->memoryPartitions[i] == PARTITIONS[i]){
            mem->usableMemory-= PARTITIONS[i];
            mem->numUsedPartitions++;
            mem->numFreePartitions--;

            p->memoryParition  = i;
            mem->memoryPartitions[i] -= p->memory;

            mem->freeMemory-= p->memory;
            return 1;
        }
    }
    return 0;
}

/* returns the memory that the process was using */
void deallocateMemory(memory_t* mem, pcb_t* p){
    mem->memoryPartitions[p->memoryParition] += p->memory;
    mem->numUsedPartitions--;
    mem->numFreePartitions++;
    mem->usableMemory+=PARTITIONS[p->memoryParition];
    mem->freeMemory+=p->memory;
    p->memoryParition = -1;
}

/* Printing the current state of the processes and saving it in a file */
void displayOutput(int clock, int pid, char state1[], char state2[], FILE* outputf){
    printf("%5d %10d %10s %10s\n",clock,pid,state1, state2);
    fprintf(outputf, "%5d %10d %10s %10s\n\n",clock,pid,state1, state2);   
}

/* displays information about the memory partitions after each allocation */
void displayMemoryInfo(memory_t* mem, pcb_t* p, FILE* outputf){
    printf("\nMEMORY HAS BEEN ALLOCATED TO PROCESS %d\n", p->pid);
    fprintf(outputf, "\nMEMORY HAS BEEN ALLOCATED TO PROCESS %d\n", p->pid);

    printf("Total used memory: %d Mb\n", TOTALMEMORY - mem->freeMemory);
    fprintf(outputf, "Total used memory: %d Mb\n", TOTALMEMORY - mem->freeMemory);

    printf("Used memory partitions: %d\n", mem->numUsedPartitions);
    fprintf(outputf, "Used memory partitions: %d\n", mem->numUsedPartitions);

    printf("Free memory partitions: %d\n", mem->numFreePartitions);
    fprintf(outputf, "Free memory partitions: %d\n", mem->numFreePartitions);

    printf("Total amount of free memory: %d MB \n", mem->freeMemory);
    fprintf(outputf, "Total amount of free memory: %d MB \n", mem->freeMemory);

    printf("Total amount of free usable memory: %d Mb\n\n", mem->usableMemory);
    fprintf(outputf, "Total amount of free usable memory: %d Mb\n\n", mem->usableMemory);
}

void displayTimingInfo(timing_t* timing, FILE* outputf){
    printf("\nNUMBER OF PROCESSES >>> %d\n", timing->numProcesses);
    fprintf(outputf,"\nNUMBER OF PROCESSES >>> %d\n", timing->numProcesses);

    printf("THROUGHPUT >>> %.2lf ms/process\n", timing->total * 1.0 / timing->numProcesses);
    fprintf(outputf, "THROUGHPUT >>> %.2lf ms/process\n", timing->total * 1.0 / timing->numProcesses);

    printf("AVERAGE TURNAROUND TIME >>> %.2lf ms/process\n", timing->turnaroundTime * 1.0 / timing->numProcesses);
    fprintf(outputf, "AVERAGE TURNAROUND TIME >>> %.2lf ms/process\n", timing->turnaroundTime * 1.0 / timing->numProcesses);

    printf("TOTAL WAIT TIME >>> %d ms\n", timing->waiting);
    fprintf(outputf, "TOTAL WAIT TIME >>> %d ms\n", timing->waiting);

    printf("AVERAGE WAIT TIME >>> %.2lf ms/process\n", timing->waiting * 1.0 / timing->numProcesses);
    fprintf(outputf, "AVERAGE WAIT TIME >>> %.2lf ms/process\n", timing->waiting * 1.0 / timing->numProcesses);

    printf("AVERAGE CPU BURST TIME >>> %.2lf ms/process\n", timing->cpuBurst * 1.0 / timing->numProcesses);
    fprintf(outputf, "AVERAGE CPU BURST TIME >>> %.2lf ms/process\n", timing->cpuBurst * 1.0 / timing->numProcesses);
}
