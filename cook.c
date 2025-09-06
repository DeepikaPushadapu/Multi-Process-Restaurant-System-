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

// Global variables
int shmid, semid;
int *shm;

// Function to update time
void update_time(int minutes) {
    int curr_time = shm[TIME_OFFSET];
    usleep(minutes * SCALE_FACTOR);
    
    sem_wait(semid, MUTEX);
    if (shm[TIME_OFFSET] < curr_time + minutes) {
        shm[TIME_OFFSET] = curr_time + minutes;
    } else {
      //  printf("Warning: Setting time failed, current time: %d, attempted new time: %d\n",
         //   shm[TIME_OFFSET], curr_time + minutes);
     
    }
    sem_signal(semid, MUTEX);
}
char* get_time_string(int minutes) {
    static char time_str[20];
    int hour = (minutes / 60) + 11; // Start at 11:00
    int min = minutes % 60;
    char am_pm = (hour < 12) ? 'a' : 'p';
    if (hour > 12) hour -= 12;
    sprintf(time_str, "[%d:%02d %cm]", hour, min, am_pm);
    return time_str;
}

// Cook implementation
void cmain(int cook_id) {
    char cook_name = (cook_id == 0) ? 'C' : 'D';

    // Initial ready message
    if (cook_id == 0) {
        printf("[11:00 am] Cook %c is ready\n", cook_name);
    } else {
        printf("[11:00 am] \tCook %c is ready\n", cook_name);
    }

    while (1) {
        // Wait for cooking request
        sem_wait(semid, COOK_SEM);
        sem_wait(semid, MUTEX);

        // Check if it's end of session time
        if (shm[TIME_OFFSET] >= 240 && shm[PENDING_ORDERS_OFFSET] == 0) {
            int current_time = shm[TIME_OFFSET];
            // Determine AM/PM and correct hour
            int hour = (11 + current_time / 60);
            int min = current_time % 60;
            char *ampm = (hour < 12) ? "am" : "pm";
            if (hour > 12) hour -= 12; // Convert from 24-hour to 12-hour format

            // Print leaving message
            if (cook_id == 0) {
                printf("[%d:%02d %s] Cook %c: Leaving\n", hour, min, ampm, cook_name);
            } else {
                printf("[%d:%02d %s] \tCook %c: Leaving\n", hour, min, ampm, cook_name);
            }

            shm[END_SESSION_OFFSET]++;
            for (int i = 0; i < 5; i++) {
                sem_signal(semid, WAITER_U_SEM + i);
            }

            sem_signal(semid, MUTEX);
            shmdt(shm);
            exit(0);
        }

        // Get a cooking request from the queue
        int cook_front = shm[COOK_QUEUE_OFFSET + COOK_FRONT_OFFSET];
        int waiter_id = shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + cook_front * 3];
        int customer_id = shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + cook_front * 3 + 1];
        int customer_cnt = shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + cook_front * 3 + 2];

        // Update front of queue
        shm[COOK_QUEUE_OFFSET + COOK_FRONT_OFFSET] = (cook_front + 1) % 200;
        shm[PENDING_ORDERS_OFFSET]--;

        char waiter_name = 'U' + waiter_id; // Convert ID to letter

        int current_time = shm[TIME_OFFSET];
        // Print "Preparing order" message
        int hour = (11 + current_time / 60);
        int min = current_time % 60;
        char *ampm = (hour < 12) ? "am" : "pm";
        if (hour > 12) hour -= 12; // Convert from 24-hour to 12-hour format

        if (cook_id == 0) {
            printf("[%d:%02d %s] Cook %c: Preparing order (Waiter %c, Customer %d, Count %d)\n",
                   hour, min, ampm, cook_name, waiter_name, customer_id, customer_cnt);
        } else {
            printf("[%d:%02d %s] \tCook %c: Preparing order (Waiter %c, Customer %d, Count %d)\n",
                   hour, min, ampm, cook_name, waiter_name, customer_id, customer_cnt);
        }

        sem_signal(semid, MUTEX);

        // Cook the food (5 minutes per person)
        update_time(customer_cnt * 5);

        // Food is ready, notify waiter
        sem_wait(semid, MUTEX);

        // Find waiter's section in shared memory
        int waiter_offset;
        switch (waiter_id) {
            case 0: waiter_offset = WAITER_U_OFFSET; break;
            case 1: waiter_offset = WAITER_V_OFFSET; break;
            case 2: waiter_offset = WAITER_W_OFFSET; break;
            case 3: waiter_offset = WAITER_X_OFFSET; break;
            case 4: waiter_offset = WAITER_Y_OFFSET; break;
            default: waiter_offset = WAITER_U_OFFSET;
        }

        // Set food ready indicator for the waiter
        shm[waiter_offset + FOOD_READY_OFFSET] = customer_id;

        current_time = shm[TIME_OFFSET];
        // Print "Prepared order" message
        hour = (11 + current_time / 60);
        min = current_time % 60;
        ampm = (hour < 12) ? "am" : "pm";
        if (hour > 12) hour -= 12; // Convert from 24-hour to 12-hour format

        if (cook_id == 0) {
            printf("[%d:%02d %s] Cook %c: Prepared order (Waiter %c, Customer %d, Count %d)\n",
                   hour, min, ampm, cook_name, waiter_name, customer_id, customer_cnt);
        } else {
            printf("[%d:%02d %s] \tCook %c: Prepared order (Waiter %c, Customer %d, Count %d)\n",
                   hour, min, ampm, cook_name, waiter_name, customer_id, customer_cnt);
        }

        sem_signal(semid, MUTEX);

        // Wake up the waiter
        sem_signal(semid, WAITER_U_SEM + waiter_id);
    }
}


int main() {
    key_t key_shm, key_sem;
    
    // Generate keys for IPC
    key_shm = ftok("cook.c", 'R');
    key_sem = ftok("cook.c", 'S');
    
    if (key_shm == -1 || key_sem == -1) {
        perror("ftok");
        exit(1);
    }
    
    // Create shared memory
    shmid = shmget(key_shm, SHM_SIZE * sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    // Attach to shared memory
    shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    
    // Initialize shared memory
    shm[TIME_OFFSET] = 0;              // Starting time (11:00am)
    shm[EMPTY_TABLES_OFFSET] = 10;     // 10 empty tables
    shm[NEXT_WAITER_OFFSET] = 0;       // First waiter is U (index 0)
    shm[PENDING_ORDERS_OFFSET] = 0;    // No pending orders initially
    shm[END_SESSION_OFFSET] = 0;       // End of session flag
    
    // Initialize queues
    for (int i = 0; i < 5; i++) {
        int offset = WAITER_U_OFFSET + i * 200;
        shm[offset + FRONT_OFFSET] = 0;
        shm[offset + BACK_OFFSET] = 0;
        shm[offset + FOOD_READY_OFFSET] = 0;
        shm[offset + PENDING_ORDERS_WAITER_OFFSET] = 0;
    }
    
    // Initialize cook queue
    shm[COOK_QUEUE_OFFSET + COOK_FRONT_OFFSET] = 0;
    shm[COOK_QUEUE_OFFSET + COOK_BACK_OFFSET] = 0;
    
    // Create semaphores
    // Need: mutex, cook, 5 waiters, and up to 200 customers
    semid = semget(key_sem, CUSTOMER_BASE_SEM + 200, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    
    // Initialize semaphores
    union semun arg;
    
    // Mutex = 1 (available)
    arg.val = 1;
    if (semctl(semid, MUTEX, SETVAL, arg) == -1) {
        perror("semctl: MUTEX");
        exit(1);
    }
    
    // Cook = 0 (no cooking requests initially)
    arg.val = 0;
    if (semctl(semid, COOK_SEM, SETVAL, arg) == -1) {
        perror("semctl: COOK_SEM");
        exit(1);
    }
    
    // Waiters = 0 (no requests initially)
    for (int i = WAITER_U_SEM; i <= WAITER_Y_SEM; i++) {
        if (semctl(semid, i, SETVAL, arg) == -1) {
            perror("semctl: WAITER_SEM");
            exit(1);
        }
    }
    
    // Customers = 0 (no signals initially)
    for (int i = 0; i < 200; i++) {
        if (semctl(semid, CUSTOMER_BASE_SEM + i, SETVAL, arg) == -1) {
            perror("semctl: CUSTOMER_SEM");
            exit(1);
        }
    }
    
    printf("Cook: IPC resources initialized\n");
    printf("Cook: Starting cooks C and D\n");
    
    // Create two cook processes
    pid_t pid[2];
    for (int i = 0; i < 2; i++) {
        pid[i] = fork();
        if (pid[i] < 0) {
            perror("fork");
            exit(1);
        } else if (pid[i] == 0) {
            cmain(i);  // This never returns
            exit(0);
        }
    }
    
    // Wait for cooks to terminate
    for (int i = 0; i < 2; i++) {
        waitpid(pid[i], NULL, 0);
    }
    
    printf("Cook: Both cooks have terminated. Keeping IPC resources for customers to clean up.\n");
    
    // Note: We don't clean up IPC resources here. 
    // The customer's parent process is responsible for that after all processes finish.
    
    return 0;
}