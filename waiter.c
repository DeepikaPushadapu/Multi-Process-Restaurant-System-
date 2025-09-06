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
void update_time(int minutes) {
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    
    int curr_time = shm[TIME_OFFSET];
    usleep(minutes * SCALE_FACTOR);
    
    sem_wait(semid, MUTEX);
    if (shm[TIME_OFFSET] < curr_time + minutes) {
        shm[TIME_OFFSET] = curr_time + minutes;
    } else {
        printf("Warning: Setting time failed, current time: %d, attempted new time: %d\n", 
               shm[TIME_OFFSET], curr_time + minutes);
    }
    sem_signal(semid, MUTEX);
    
    shmdt(shm);
}
// Function to get formatted time string
char* get_time_string(int minutes) {
    static char time_str[20];
    int hour = (minutes / 60) + 11;  // Start at 11:00
    int min = minutes % 60;
    char am_pm = (hour < 12) ? 'a' : 'p';
    
    if (hour > 12) hour -= 12;
    
    sprintf(time_str, "[%d:%02d %cm]", hour, min, am_pm);
    return time_str;
}

// Function to get indentation for waiter
char* get_indentation(int waiter_id) {
    static char indent_str[20];
    memset(indent_str, '\t', waiter_id);
    indent_str[waiter_id] = '\0';
    return indent_str;
}



// Waiter implementation
void wmain(int waiter_id) {
    char waiter_name = 'U' + waiter_id;
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }

    // Determine waiter's section in shared memory
    int waiter_offset;
    switch (waiter_id) {
        case 0: waiter_offset = WAITER_U_OFFSET; break;
        case 1: waiter_offset = WAITER_V_OFFSET; break;
        case 2: waiter_offset = WAITER_W_OFFSET; break;
        case 3: waiter_offset = WAITER_X_OFFSET; break;
        case 4: waiter_offset = WAITER_Y_OFFSET; break;
        default: waiter_offset = WAITER_U_OFFSET;
    }

    printf("%s %sWaiter %c is ready\n",
           get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name);

    while (1) {
        // Wait to be woken up by a cook or a new customer
        sem_wait(semid, WAITER_U_SEM + waiter_id);
        sem_wait(semid, MUTEX);

        // Check if end of session
        if (shm[TIME_OFFSET] >= 240 && shm[waiter_offset + FOOD_READY_OFFSET] == 0 && shm[waiter_offset + PENDING_ORDERS_WAITER_OFFSET] == 0) {
            printf("%s %sWaiter %c: Time is after 3:00pm and no pending requests. Terminating.\n",
                   get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name);
            shm[END_SESSION_OFFSET]++;
            sem_signal(semid, MUTEX);
            shmdt(shm);
            exit(0);
        }

        // Check if food is ready for a customer
        if (shm[waiter_offset + FOOD_READY_OFFSET] > 0) {
            int customer_id = shm[waiter_offset + FOOD_READY_OFFSET];
            printf("%s %sWaiter %c: Serving food to Customer %d\n",
                   get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name, customer_id);

            // Reset food ready flag
            shm[waiter_offset + FOOD_READY_OFFSET] = 0;

            sem_signal(semid, MUTEX);

            // Notify the customer that food is ready
            sem_signal(semid, CUSTOMER_BASE_SEM + customer_id);

            // Check termination condition again after serving food
            sem_wait(semid, MUTEX);
            if (shm[TIME_OFFSET] >= 240 && shm[waiter_offset + FOOD_READY_OFFSET] == 0 && shm[waiter_offset + PENDING_ORDERS_WAITER_OFFSET] == 0) {
                printf("%s %sWaiter %c leaving (no more customer to serve).\n",
                       get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name);
                shm[END_SESSION_OFFSET]++;
                sem_signal(semid, MUTEX);
                shmdt(shm);
                exit(0);
            }
            sem_signal(semid, MUTEX);
        }

        // Check if there's a new customer waiting to place order
        else if (shm[waiter_offset + PENDING_ORDERS_WAITER_OFFSET] > 0) {
            // Get customer info from the waiter's queue
            int front = shm[waiter_offset + FRONT_OFFSET];
            int customer_id = shm[waiter_offset + QUEUE_START_OFFSET + front * 2];
            int customer_cnt = shm[waiter_offset + QUEUE_START_OFFSET + front * 2 + 1];

            // Update front of queue
            shm[waiter_offset + FRONT_OFFSET] = (front + 1) % 100;
            shm[waiter_offset + PENDING_ORDERS_WAITER_OFFSET]--;

            printf("%s %sWaiter %c: Taking order from customer %d with %d persons\n",
                   get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name, customer_id, customer_cnt);

            sem_signal(semid, MUTEX);

            // Take order (this takes 1 minute)
            update_time(1);

            sem_wait(semid, MUTEX);

            // Add order to cook queue
            int back = shm[COOK_QUEUE_OFFSET + COOK_BACK_OFFSET];
            shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + back * 3] = waiter_id;
            shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + back * 3 + 1] = customer_id;
            shm[COOK_QUEUE_OFFSET + COOK_QUEUE_START_OFFSET + back * 3 + 2] = customer_cnt;

            // Update back of cook queue
            shm[COOK_QUEUE_OFFSET + COOK_BACK_OFFSET] = (back + 1) % 200;
            shm[PENDING_ORDERS_OFFSET]++;

            printf("%s %sWaiter %c: Placing order for Customer %d (count = %d)\n",
                   get_time_string(shm[TIME_OFFSET]), get_indentation(waiter_id), waiter_name, customer_id, customer_cnt);

            sem_signal(semid, MUTEX);

            // Notify the customer that order has been placed
            sem_signal(semid, CUSTOMER_BASE_SEM + customer_id);

            // Notify a cook that there's a new order
            sem_signal(semid, COOK_SEM);
        } else {
            // No tasks, possibly woken up by end of session signal
            sem_signal(semid, MUTEX);
        }
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
    
    printf("Waiter: IPC resources attached\n");
    printf("Waiter: Starting waiters U, V, W, X, and Y\n");
    
    // Create five waiter processes
    pid_t pid[5];
    for (int i = 0; i < 5; i++) {
        pid[i] = fork();
        if (pid[i] < 0) {
            perror("fork");
            exit(1);
        } else if (pid[i] == 0) {
            wmain(i);  // This never returns
            exit(0);
        }
    }
    
    // Wait for all waiters to terminate
    for (int i = 0; i < 5; i++) {
        waitpid(pid[i], NULL, 0);
    }
    
    printf("Waiter: All waiters have terminated\n");
    
    return 0;
}