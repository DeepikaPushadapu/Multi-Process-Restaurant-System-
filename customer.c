#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>

// Constants
#define SHM_SIZE 2000
#define SCALE_FACTOR 100000  // 100ms = 100,000 microseconds
#define TIME_OFFSET 0
#define EMPTY_TABLES_OFFSET 1
#define NEXT_WAITER_OFFSET 2
#define PENDING_ORDERS_OFFSET 3
#define END_SESSION_OFFSET 4
#define WAITER_U_OFFSET 100
#define WAITER_V_OFFSET 300
#define WAITER_W_OFFSET 500
#define WAITER_X_OFFSET 700
#define WAITER_Y_OFFSET 900
#define COOK_QUEUE_OFFSET 1100

// Semaphore indexes
#define MUTEX 0
#define COOK_SEM 1
#define WAITER_U_SEM 2
#define WAITER_V_SEM 3
#define WAITER_W_SEM 4
#define WAITER_X_SEM 5
#define WAITER_Y_SEM 6
#define CUSTOMER_BASE_SEM 7

// Waiter queue offsets (relative to waiter section)
#define FRONT_OFFSET 0
#define BACK_OFFSET 1
#define FOOD_READY_OFFSET 2
#define PENDING_ORDERS_WAITER_OFFSET 3
#define QUEUE_START_OFFSET 10

// Cook queue offsets
#define COOK_FRONT_OFFSET 0
#define COOK_BACK_OFFSET 1
#define COOK_QUEUE_START_OFFSET 10

// Global variables
int shmid, semid;

// Semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void sem_wait(int semid, int semnum) {
    struct sembuf sb = {semnum, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop wait");
        exit(1);
    }
}

void sem_signal(int semid, int semnum) {
    struct sembuf sb = {semnum, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop signal");
        exit(1);
    }
}

// Function to update time
void update_time(int minutes, int *shm) {
    int curr_time = shm[TIME_OFFSET];
    usleep(minutes * SCALE_FACTOR);
    
    sem_wait(semid, MUTEX);
    if (shm[TIME_OFFSET] < curr_time + minutes) {
        shm[TIME_OFFSET] = curr_time + minutes;
    }
    sem_signal(semid, MUTEX);
}

// Function to format time for output
void format_time(int minutes, int *hours, int *mins, char *am_pm) {
    *hours = 11 + minutes / 60;
    *mins = minutes % 60;
    
    strcpy(am_pm, "am");
    if (*hours >= 12) {
        strcpy(am_pm, "pm");
        if (*hours > 12) {
            *hours -= 12;
        }
    }
}

// Customer implementation
void cmain(int customer_id, int arrival_time, int customer_cnt) {
    // Attach to shared memory
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    
    // Record arrival time for calculating waiting time later
    int arrival_time_actual;
    
    // Set time to arrival time
    sem_wait(semid, MUTEX);
    shm[TIME_OFFSET] = arrival_time;
    arrival_time_actual = arrival_time; 
    
    // Print arrival message with timestamp
    int hours, minutes;
    char am_pm[3];
    format_time(arrival_time_actual, &hours, &minutes, am_pm);
    
    printf("[%d:%02d %s] Customer %d arrives (count = %d)\n", 
           hours, minutes, am_pm, customer_id, customer_cnt);
    
    // Check if it's after 3:00pm (240 minutes after 11:00am)
    if (shm[TIME_OFFSET] >= 240) {
        printf("[%d:%02d %s]\t\t\t\t\t\tCustomer %d leaves (late arrival)\n", 
               hours, minutes, am_pm, customer_id);
        sem_signal(semid, MUTEX);
        shmdt(shm);
        exit(0);
    }
    
    // Check if a table is available
    if (shm[EMPTY_TABLES_OFFSET] <= 0) {
        printf("[%d:%02d %s]\t\t\t\t\t\tCustomer %d leaves (no empty table)\n", 
               hours, minutes, am_pm, customer_id);
        sem_signal(semid, MUTEX);
        shmdt(shm);
        exit(0);
    }
    
    // Use an empty table
    shm[EMPTY_TABLES_OFFSET]--;
    sem_signal(semid, MUTEX);
    
    // Find the waiter to serve
    sem_wait(semid, MUTEX);
    int waiter_num = shm[NEXT_WAITER_OFFSET];
    shm[NEXT_WAITER_OFFSET] = (waiter_num + 1) % 5;
    
    // Determine waiter's section in shared memory
    int waiter_offset;
    switch (waiter_num) {
        case 0: waiter_offset = WAITER_U_OFFSET; break;
        case 1: waiter_offset = WAITER_V_OFFSET; break;
        case 2: waiter_offset = WAITER_W_OFFSET; break;
        case 3: waiter_offset = WAITER_X_OFFSET; break;
        case 4: waiter_offset = WAITER_Y_OFFSET; break;
        default: waiter_offset = WAITER_U_OFFSET;
    }
    
    char waiter_name = 'U' + waiter_num;
    
    // Add customer to waiter's queue
    int back = shm[waiter_offset + BACK_OFFSET];
    shm[waiter_offset + QUEUE_START_OFFSET + back * 2] = customer_id;
    shm[waiter_offset + QUEUE_START_OFFSET + back * 2 + 1] = customer_cnt;
    
    // Update back of queue
    shm[waiter_offset + BACK_OFFSET] = (back + 1) % 100;
    shm[waiter_offset + PENDING_ORDERS_WAITER_OFFSET]++;
    
    sem_signal(semid, MUTEX);
    
    // Signal waiter to take the order
    sem_signal(semid, WAITER_U_SEM + waiter_num);
    
    // Wait for waiter to take order
    sem_wait(semid, CUSTOMER_BASE_SEM + customer_id);
    
    // Print order placed message with timestamp
    sem_wait(semid, MUTEX);
    int current_time = shm[TIME_OFFSET];
    sem_signal(semid, MUTEX);
    
    format_time(current_time, &hours, &minutes, am_pm);
    
    printf("[%d:%02d %s] \tCustomer %d: Order placed to Waiter %c\n", 
           hours, minutes, am_pm, customer_id, waiter_name);
    
    // Wait for food to be served
    sem_wait(semid, CUSTOMER_BASE_SEM + customer_id);
    
    // Print food received message with timestamp and waiting time
    sem_wait(semid, MUTEX);
    current_time = shm[TIME_OFFSET];
    sem_signal(semid, MUTEX);
    
    int waiting_time = current_time - arrival_time_actual;
    format_time(current_time, &hours, &minutes, am_pm);
    
    printf("[%d:%02d %s] \t\tCustomer %d gets food [Waiting time = %d]\n", 
           hours, minutes, am_pm, customer_id, waiting_time);
    
    // Eat food (takes 30 minutes)
    update_time(30, shm);

    // Print message that customer has finished eating and is leaving
    sem_wait(semid, MUTEX);
    current_time = shm[TIME_OFFSET];
    format_time(current_time, &hours, &minutes, am_pm);
    
    printf("[%d:%02d %s] \t\t\tCustomer %d finishes eating and leaves\n", 
        hours, minutes, am_pm, customer_id);
    
    // Free the table
    shm[EMPTY_TABLES_OFFSET]++;
    sem_signal(semid, MUTEX);
    
    // Detach from shared memory and exit
    shmdt(shm);
    exit(0);
}

int main() {
    FILE *fp;
    int customer_id, arrival_time, customer_cnt;
    int last_arrival_time = 0;
    
    key_t key_shm, key_sem;
    
    // Generate keys for IPC
    key_shm = ftok("cook.c", 'R');
    key_sem = ftok("cook.c", 'S');
    
    if (key_shm == -1 || key_sem == -1) {
        perror("ftok");
        exit(1);
    }
    
    // Get shared memory
    shmid = shmget(key_shm, SHM_SIZE * sizeof(int), 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    // Get semaphores
    semid = semget(key_sem, CUSTOMER_BASE_SEM + 200, 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    
    printf("Customer: IPC resources attached\n");
    
    // Open customer file
    fp = fopen("customers.txt", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }
    
    printf("Customer: Processing customer arrivals from customers.txt\n");
    
    // Array to store child PIDs
    pid_t *child_pids = NULL;
    int num_customers = 0;
    
    // Read customer information from file
    while (fscanf(fp, "%d %d %d", &customer_id, &arrival_time, &customer_cnt) == 3) {
        if (customer_id == -1) {
            break;  // End of file marker
        }
        
        // Validate customer data
        if (customer_id <= 0 || arrival_time < 0 || customer_cnt < 1 || customer_cnt > 4) {
            printf("Invalid customer data: ID=%d, arrival=%d, count=%d. Skipping.\n",
                   customer_id, arrival_time, customer_cnt);
            continue;
        }
        
        // Wait for the specified interval between customers
        if (num_customers > 0) {
            int wait_time = arrival_time - last_arrival_time;
            if (wait_time > 0) {
                usleep(wait_time * SCALE_FACTOR);
            }
        }
        
        last_arrival_time = arrival_time;
        
        // Expand the array of PIDs
        num_customers++;
        child_pids = realloc(child_pids, num_customers * sizeof(pid_t));
        if (child_pids == NULL) {
            perror("realloc");
            exit(1);
        }
        
        // Fork a new process for this customer
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            // Child process (customer)
            fclose(fp);  // Close the file in the child
            free(child_pids);  // Free the array in the child
            
            cmain(customer_id, arrival_time, customer_cnt);  // This never returns
            exit(0);
        } else {
            // Parent process
            child_pids[num_customers - 1] = pid;
        }
    }
    
    fclose(fp);
    
    // Wait for all child processes to terminate
    for (int i = 0; i < num_customers; i++) {
        waitpid(child_pids[i], NULL, 0);
    }
    
    free(child_pids);
    
   int *shm = (int *)shmat(shmid, NULL, 0);
   if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }

    sem_wait(semid, MUTEX);
    if (shm[TIME_OFFSET] >= 240) {
        sem_signal(semid, COOK_SEM);
        sem_signal(semid, COOK_SEM); // Signal both cooks
    }
    sem_signal(semid, MUTEX);

   while (shm[END_SESSION_OFFSET] < 7) {
       usleep(100000);  // Sleep for a short time
   }
   shmdt(shm);
   
    // Clean up IPC resources
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl");
    }
    
    if (semctl(semid, 0, IPC_RMID, 0) == -1) {
        perror("semctl");
    }
    
    printf("Customer: IPC resources cleaned up\n");
    
    return 0;
}