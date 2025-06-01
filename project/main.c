#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h> // Corrected: Added .h
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <math.h>
#include <stdarg.h>
#include <termios.h>
#include <ctype.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdint.h> // For intptr_t

#define LOG_FILE "process_manager.log"
#define MAX_PROCESSES 100
#define MAX_THREADS_PER_PROCESS 20
#define MAX_THREADS (MAX_PROCESSES * MAX_THREADS_PER_PROCESS)
#define MAX_PIPES 50
#define MAX_NAME_LEN 50
#define SHM_KEY 0x1234
#define FIFO_NAME "/tmp/process_manager_fifo"
#define UNIX_SOCKET_PATH "/tmp/process_manager_socket"
#define MSG_QUEUE_KEY 0x5678

typedef struct {
    pthread_t tid;
    char name[MAX_NAME_LEN];
    int active;
    pid_t process_id; // For condition variable threads, this stores the CV index
} ThreadInfo;

typedef struct {
    int id;
    char name[MAX_NAME_LEN];
    int fd[2];
    pid_t process1;
    pid_t process2;
    int active;
} PipeInfo;

typedef struct {
    pid_t pid;
    pid_t ppid;
    char name[MAX_NAME_LEN];
    int active;
    ThreadInfo threads[MAX_THREADS_PER_PROCESS];
    int thread_count;
} ProcessInfo;

typedef struct {
    sem_t sem;
    char name[MAX_NAME_LEN];
    int value;
    int active;
} SemaphoreInfo;

typedef struct {
    pthread_mutex_t mutex;
    char name[MAX_NAME_LEN];
    int active;
} MutexInfo;

typedef struct {
    pthread_cond_t cond;
    char name[MAX_NAME_LEN];
    int active;
} CondVarInfo;

typedef struct {
    long mtype;
    char mtext[100];
} Message;

volatile sig_atomic_t sigint_received = 0;
ProcessInfo processes[MAX_PROCESSES];
ThreadInfo threads[MAX_THREADS];
PipeInfo pipes[MAX_PIPES];
SemaphoreInfo semaphores[10];
MutexInfo mutexes[10];
CondVarInfo cond_vars[10];
int process_count = 0;
int thread_count = 0;
int pipe_count = 0;
int semaphore_count = 0;
int mutex_count = 0;
int cond_var_count = 0;

// =================== Function Prototypes (Forward Declarations) ===================
// Utility
void clear_screen();
void log_msg(const char* level, const char* format, ...);
void init_logging();
void close_logging();
void handle_sigint(int sig);
void setup_signal_handlers();
void press_enter_to_continue();
int find_process_index(pid_t pid);

// Process Management
void create_process();
void list_processes();
void show_process_hierarchy();
void wait_demo();
void terminate_process();

// Thread Management
void* thread_function(void* arg);
void* shared_counter_thread(void* arg);
void* condition_var_thread(void* arg);
void create_thread();
void list_threads();
void terminate_thread();

// Pipe Management
void create_pipe();
void list_pipes();
void use_pipe();

// Synchronization Primitives
void create_semaphore();
void list_semaphores();
void semaphore_wait();
void semaphore_post();
void create_mutex();
void list_mutexes();
void mutex_lock();
void mutex_unlock();
void create_condition_variable();
void list_condition_variables(); // Added prototype
void signal_condition_variable();

// IPC Mechanisms
void shared_memory_demo();
void named_pipe_demo();
void unix_socket_demo();
void message_queue_demo();

// Main Menu & Cleanup
void display_menu();
void cleanup();


// =================== Utility Functions ===================
void clear_screen() {
    printf("\033[H\033[J");
}

void log_msg(const char* level, const char* format, ...) {
    va_list args;
    va_start(args, format);
   
    time_t now;
    time(&now);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[24] = '\0';
   
    FILE* log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        fprintf(log_file, "[%s] %s: ", time_str, level);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fclose(log_file);
    }
   
    va_end(args);
}

void init_logging() {
    FILE* log_file = fopen(LOG_FILE, "w");
    if (log_file) {
        fclose(log_file);
    }
}

void close_logging() {
    // Nothing special needed
}

void handle_sigint(int sig) {
    (void)sig;
    sigint_received = 1;
    log_msg("INFO", "SIGINT received, shutting down gracefully");
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

void press_enter_to_continue() {
    printf("\nPress Enter to continue...");
    // Clear input buffer before reading new char
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int find_process_index(pid_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid && processes[i].active) {
            return i;
        }
    }
    return -1;
}

// =================== Process Management ===================
void create_process() {
    if (process_count >= MAX_PROCESSES) {
        printf("Maximum number of processes reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    pid_t ppid = -1;
   
    printf("Enter process name: ");
    scanf("%49s", name);
   
    if (process_count > 0) {
        printf("Current active processes:\n");
        for (int i = 0; i < process_count; i++) {
            if (processes[i].active) {
                printf("PID: %d, Name: %s\n", processes[i].pid, processes[i].name);
            }
        }
        printf("Enter parent PID (-1 for no parent): ");
        scanf("%d", &ppid);
    }
    while (getchar() != '\n'); // Consume newline

    if (ppid != -1 && find_process_index(ppid) == -1) {
        printf("Invalid parent PID, creating as root process\n");
        ppid = -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        printf("Child process %s (PID %d, PPID %d) running\n",
               name, getpid(), getppid());
        log_msg("INFO", "Child process %s (PID %d, PPID %d) started", name, getpid(), getppid());
       
        while (!sigint_received) {
            sleep(1);
        }
       
        exit(0);
    } else if (pid > 0) {
        // Parent process
        processes[process_count].pid = pid;
        processes[process_count].ppid = ppid;
        strncpy(processes[process_count].name, name, MAX_NAME_LEN-1);
        processes[process_count].name[MAX_NAME_LEN-1] = '\0'; // Ensure null termination
        processes[process_count].active = 1;
        processes[process_count].thread_count = 0;
        process_count++;
       
        printf("Created process %s with PID %d (PPID %d)\n", name, pid, ppid);
        log_msg("INFO", "Created process %s with PID %d (PPID %d)", name, pid, ppid);
    } else {
        perror("fork");
        log_msg("ERROR", "Failed to create process %s", name);
    }
}

void list_processes() {
    printf("\n=== Active Processes ===\n");
    printf("%-10s %-10s %-20s %-10s %-10s\n", "PID", "PPID", "Name", "Threads", "Status");
    printf("--------------------------------------------------------\n");
   
    int active_count = 0;
    for (int i = 0; i < process_count; i++) {
        if (processes[i].active) {
            int status;
            pid_t result = waitpid(processes[i].pid, &status, WNOHANG);
           
            if (result == 0) {
                // Process is still running
                printf("%-10d %-10d %-20s %-10d %-10s\n",
                       processes[i].pid,
                       processes[i].ppid,
                       processes[i].name,
                       processes[i].thread_count,
                       "Running");
                active_count++;
            } else {
                // Process has terminated or waitpid failed
                if (result == -1 && errno == ECHILD) {
                    // Process already reaped, mark as terminated
                    printf("%-10d %-10d %-20s %-10d %-10s (Reaped)\n",
                           processes[i].pid,
                           processes[i].ppid,
                           processes[i].name,
                           processes[i].thread_count,
                           "Terminated");
                } else if (result > 0) {
                    // Process terminated normally
                    printf("%-10d %-10d %-20s %-10d %-10s\n",
                           processes[i].pid,
                           processes[i].ppid,
                           processes[i].name,
                           processes[i].thread_count,
                           "Terminated");
                }
                processes[i].active = 0; // Mark as inactive regardless
            }
        }
    }
   
    if (active_count == 0) {
        printf("No active processes\n");
    }
}

void show_process_hierarchy() {
    printf("\n=== Process Hierarchy ===\n");
   
    // Find root processes (no parent or parent not in our list)
    for (int i = 0; i < process_count; i++) {
        if (!processes[i].active) continue;
       
        int is_root = 1;
        if (processes[i].ppid != -1) {
            for (int j = 0; j < process_count; j++) {
                if (i != j && processes[j].active && processes[j].pid == processes[i].ppid) {
                    is_root = 0;
                    break;
                }
            }
        }
       
        if (is_root) {
            // Recursive print with indentation
            void print_tree(int pid, int level) {
                for (int k = 0; k < level; k++) printf("  ");
                for (int k = 0; k < process_count; k++) {
                    if (processes[k].pid == pid && processes[k].active) {
                        printf("└─ %s (PID: %d)\n", processes[k].name, processes[k].pid);
                        break;
                    }
                }
               
                // Find children
                for (int k = 0; k < process_count; k++) {
                    if (processes[k].ppid == pid && processes[k].active) {
                        print_tree(processes[k].pid, level + 1);
                    }
                }
            }
           
            print_tree(processes[i].pid, 0);
        }
    }
    if (process_count == 0) {
        printf("No processes to display hierarchy.\n");
    }
}

void wait_demo() {
    printf("\n=== wait() System Call Demo ===\n");
   
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        printf("Child process (PID %d) starting\n", getpid());
        sleep(2);
        printf("Child process (PID %d) exiting\n", getpid());
        exit(42);
    } else if (pid > 0) {
        // Parent process
        printf("Parent process (PID %d) waiting for child (PID %d)...\n", getpid(), pid);
        int status;
        pid_t child_pid = wait(&status);
       
        if (child_pid == -1) {
            perror("wait");
        } else {
            if (WIFEXITED(status)) {
                printf("Child (PID %d) exited with status %d\n", child_pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Child (PID %d) killed by signal %d\n", child_pid, WTERMSIG(status));
            }
        }
    } else {
        perror("fork");
    }
}

void terminate_process() {
    list_processes();
   
    if (process_count == 0) {
        printf("No processes to terminate\n");
        return;
    }
   
    pid_t pid;
    printf("Enter PID of process to terminate: ");
    if (scanf("%d", &pid) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n'); // Clear input
        return;
    }
    while (getchar() != '\n'); // Consume newline
   
    int index = find_process_index(pid);
    if (index != -1) {
        // First terminate all threads in this process
        for (int i = 0; i < processes[index].thread_count; i++) {
            threads[i].active = 0; // Mark thread as inactive
            pthread_cancel(processes[index].threads[i].tid);
            pthread_join(processes[index].threads[i].tid, NULL); // Wait for thread to finish cancellation
        }
        processes[index].thread_count = 0; // Reset thread count for the process

        if (kill(pid, SIGTERM) == 0) {
            printf("Sent termination signal to process %d (%s)\n", pid, processes[index].name);
            log_msg("INFO", "Sent SIGTERM to process %d (%s)", pid, processes[index].name);
            // Wait for the process to actually terminate
            waitpid(pid, NULL, 0);
            processes[index].active = 0;
        } else {
            perror("kill");
            log_msg("ERROR", "Failed to terminate process %d (%s)", pid, processes[index].name);
        }
    } else {
        printf("Process with PID %d not found or not active\n", pid);
    }
}

// =================== Thread Management ===================
void* thread_function(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
    // Set cancellation type and state
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    printf("Thread %s (TID %lu) in process %d started\n",
           info->name, (unsigned long)info->tid, info->process_id);
    log_msg("INFO", "Thread %s (TID %lu) in process %d started",
            info->name, (unsigned long)info->tid, info->process_id);

    for (int i = 0; i < 5 && !sigint_received && info->active; i++) {
        printf("Thread %s (TID %lu) in process %d running iteration %d\n",
               info->name, (unsigned long)info->tid, info->process_id, i);
        sleep(1);
        pthread_testcancel(); // Allow cancellation points
    }

    printf("Thread %s (TID %lu) in process %d exiting normally\n",
           info->name, (unsigned long)info->tid, info->process_id);
    log_msg("INFO", "Thread %s (TID %lu) in process %d exiting normally",
            info->name, (unsigned long)info->tid, info->process_id);
    return NULL;
}

void* shared_counter_thread(void* arg) {
    // This counter is shared among threads of the same process, not across processes.
    // To share across processes, you'd need shared memory.
    static atomic_int shared_counter = 0; // Use atomic for thread-safe increments
    ThreadInfo* info = (ThreadInfo*)arg;
   
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    printf("Thread %s (TID %lu) started, shared counter = %d\n",
           info->name, (unsigned long)info->tid, atomic_load(&shared_counter));
    log_msg("INFO", "Thread %s (TID %lu) started, shared counter = %d",
            info->name, (unsigned long)info->tid, atomic_load(&shared_counter));

    for (int i = 0; i < 5 && !sigint_received && info->active; i++) {
        atomic_fetch_add(&shared_counter, 1);
        printf("Thread %s (TID %lu) incremented counter to %d\n",
               info->name, (unsigned long)info->tid, atomic_load(&shared_counter));
        sleep(1);
        pthread_testcancel();
    }

    printf("Thread %s (TID %lu) exiting, final counter = %d\n",
           info->name, (unsigned long)info->tid, atomic_load(&shared_counter));
    log_msg("INFO", "Thread %s (TID %lu) exiting, final counter = %d",
            info->name, (unsigned long)info->tid, atomic_load(&shared_counter));
    return NULL;
}

void* condition_var_thread(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
    int cond_var_idx = (int)(intptr_t)info->process_id; // Retrieve the stored index

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
   
    // Ensure mutex and condition variable exist and are active
    if (cond_var_idx < 0 || cond_var_idx >= cond_var_count || !cond_vars[cond_var_idx].active ||
        mutex_count == 0 || !mutexes[0].active) { // Assumes mutexes[0] is the general mutex for CV demo
        printf("Error: Condition variable or mutex not properly initialized for thread %s. Exiting thread.\n", info->name);
        log_msg("ERROR", "Condition variable or mutex not properly initialized for thread %s", info->name);
        return NULL;
    }

    printf("Condition variable thread %s (TID %lu) waiting on %s\n",
           info->name, (unsigned long)info->tid, cond_vars[cond_var_idx].name);
    log_msg("INFO", "Condition variable thread %s (TID %lu) waiting on %s",
            info->name, (unsigned long)info->tid, cond_vars[cond_var_idx].name);
   
    pthread_mutex_lock(&mutexes[0].mutex); // Acquire the mutex before waiting
    pthread_cond_wait(&cond_vars[cond_var_idx].cond, &mutexes[0].mutex);
    pthread_mutex_unlock(&mutexes[0].mutex); // Release the mutex after being signaled
   
    printf("Condition variable thread %s (TID %lu) signaled and resumed\n",
           info->name, (unsigned long)info->tid);
    log_msg("INFO", "Condition variable thread %s (TID %lu) signaled and resumed",
            info->name, (unsigned long)info->tid);
   
    return NULL;
}


void create_thread() {
    if (process_count == 0) {
        printf("No processes available to create thread in\n");
        return;
    }

    if (thread_count >= MAX_THREADS) {
        printf("Maximum number of threads reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    pid_t process_id;
    int type;
   
    printf("Enter thread name: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0'; // Ensure null termination
   
    list_processes();
    printf("Enter process PID this thread belongs to: ");
    if (scanf("%d", &process_id) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
   
    printf("Select thread type:\n1. Normal\n2. Shared Counter\n3. Condition Variable\nChoice: ");
    if (scanf("%d", &type) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Consume newline

    int process_index = find_process_index(process_id);
    if (process_index == -1) {
        printf("Invalid process PID\n");
        return;
    }

    if (processes[process_index].thread_count >= MAX_THREADS_PER_PROCESS) {
        printf("Maximum threads reached for this process\n");
        return;
    }

    threads[thread_count].active = 1;
    strncpy(threads[thread_count].name, name, MAX_NAME_LEN-1);
    threads[thread_count].name[MAX_NAME_LEN-1] = '\0';
    threads[thread_count].process_id = process_id; // Default for normal/shared threads

    void* (thread_func)(void) = thread_function;
    if (type == 2) {
        thread_func = shared_counter_thread;
    } else if (type == 3) {
        thread_func = condition_var_thread;
        list_condition_variables(); // Call the function (now declared)
        if (cond_var_count == 0) {
            printf("No condition variables available. Create one first.\n");
            threads[thread_count].active = 0; // Don't create the thread
            return;
        }
        char cv_name[MAX_NAME_LEN];
        printf("Enter the name of the condition variable this thread will wait on: ");
        scanf("%49s", cv_name);
        cv_name[MAX_NAME_LEN-1] = '\0';
        while (getchar() != '\n');

        int cv_idx = -1;
        for (int i = 0; i < cond_var_count; i++) {
            if (strcmp(cond_vars[i].name, cv_name) == 0 && cond_vars[i].active) {
                cv_idx = i;
                break;
            }
        }
        if (cv_idx == -1) {
            printf("Condition variable '%s' not found or not active.\n", cv_name);
            threads[thread_count].active = 0; // Don't create the thread
            return;
        }
        // Store the index of the condition variable in process_id for the thread_function
        threads[thread_count].process_id = (pid_t)(intptr_t)cv_idx;
       
        // Also ensure a mutex exists for condition variable usage
        if (mutex_count == 0) {
            printf("No mutexes available. A mutex is required for condition variables. Create one first.\n");
            threads[thread_count].active = 0; // Don't create the thread
            return;
        }
    }

    if (pthread_create(&threads[thread_count].tid, NULL, thread_func, &threads[thread_count]) == 0) {
        // Add to process's thread list
        processes[process_index].threads[processes[process_index].thread_count++] = threads[thread_count];
       
        printf("Created thread %s with TID %lu in process %d\n",
               name, (unsigned long)threads[thread_count].tid, process_id);
        log_msg("INFO", "Created thread %s with TID %lu in process %d",
                name, (unsigned long)threads[thread_count].tid, process_id);
        thread_count++;
    } else {
        perror("pthread_create");
        log_msg("ERROR", "Failed to create thread %s in process %d", name, process_id);
        threads[thread_count].active = 0; // Mark as inactive if creation fails
    }
}

void list_threads() {
    printf("\n=== Active Threads ===\n");
    printf("%-20s %-10s %-20s %-10s\n", "TID", "PID", "Name", "Status");
    printf("------------------------------------------------\n");

    int active_thread_count = 0;
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].active) {
            // If it's a condition variable thread, process_id holds the CV index.
            // We print the actual process_id that it "belongs" to (which we set at creation)
            pid_t actual_process_id = 0; // Default to 0, or find the actual PID if stored
            for(int p_idx = 0; p_idx < process_count; p_idx++) {
                if (processes[p_idx].active) {
                    for (int t_idx = 0; t_idx < processes[p_idx].thread_count; t_idx++) {
                        if (processes[p_idx].threads[t_idx].tid == threads[i].tid) {
                            actual_process_id = processes[p_idx].pid;
                            break;
                        }
                    }
                }
                if (actual_process_id != 0) break;
            }

            printf("%-20lu %-10d %-20s %-10s\n",
                   (unsigned long)threads[i].tid,
                   actual_process_id, // Display the actual process PID
                   threads[i].name,
                   "Running");
            active_thread_count++;
        }
    }
    if (active_thread_count == 0) {
        printf("No active threads\n");
    }
}

void terminate_thread() {
    list_threads();

    if (thread_count == 0) {
        printf("No threads to terminate\n");
        return;
    }

    pthread_t tid;
    printf("Enter TID of thread to terminate: ");
    if (scanf("%lu", &tid) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Consume newline

    int found = 0;
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].tid == tid && threads[i].active) {
            found = 1;
            threads[i].active = 0; // Mark as inactive immediately

            // Attempt to cancel and join the thread
            if (pthread_cancel(tid) == 0) {
                printf("Sent cancellation request to thread %lu (%s)\n", (unsigned long)tid, threads[i].name);
                log_msg("INFO", "Sent cancellation request to thread %lu (%s)", (unsigned long)tid, threads[i].name);
                if (pthread_join(tid, NULL) == 0) {
                    printf("Terminated and joined thread %lu (%s)\n", (unsigned long)tid, threads[i].name);
                    log_msg("INFO", "Terminated and joined thread %lu (%s)", (unsigned long)tid, threads[i].name);
                } else {
                    perror("pthread_join");
                    log_msg("ERROR", "Failed to join thread %lu (%s)", (unsigned long)tid, threads[i].name);
                }
            } else {
                perror("pthread_cancel");
                log_msg("ERROR", "Failed to cancel thread %lu (%s)", (unsigned long)tid, threads[i].name);
            }
           
            // Remove from process's thread list (linear scan, could be optimized)
            for (int j = 0; j < process_count; j++) {
                if (processes[j].active) { // Only check active processes
                    for (int k = 0; k < processes[j].thread_count; k++) {
                        if (processes[j].threads[k].tid == tid) {
                            // Shift elements to the left to fill the gap
                            for (int l = k; l < processes[j].thread_count - 1; l++) {
                                processes[j].threads[l] = processes[j].threads[l+1];
                            }
                            processes[j].thread_count--;
                            log_msg("INFO", "Removed thread %lu from process %d's list", (unsigned long)tid, processes[j].pid);
                            break; // Thread found and removed from this process's list
                        }
                    }
                }
            }
            // Now, remove from global threads array (shift elements)
            for (int k = i; k < thread_count - 1; k++) {
                threads[k] = threads[k+1];
            }
            thread_count--; // Decrement global thread count
            break; // Thread found and processed
        }
    }

    if (!found) {
        printf("Thread with TID %lu not found or not active\n", (unsigned long)tid);
    }
}


// =================== Pipe Management ===================
void create_pipe() {
    if (process_count < 2) {
        printf("Need at least 2 processes to create a pipe\n");
        return;
    }

    if (pipe_count >= MAX_PIPES) {
        printf("Maximum number of pipes reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    pid_t process1, process2;
   
    printf("Enter pipe name: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
   
    list_processes();
    printf("Enter first process PID (will write to pipe): ");
    if (scanf("%d", &process1) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
   
    printf("Enter second process PID (will read from pipe): ");
    if (scanf("%d", &process2) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Consume newline

    if (process1 == process2) {
        printf("Cannot create pipe between the same process\n");
        return;
    }

    int process1_index = find_process_index(process1);
    int process2_index = find_process_index(process2);
    if (process1_index == -1 || process2_index == -1) {
        printf("Invalid process PIDs. Both processes must be active.\n");
        return;
    }

    if (pipe(pipes[pipe_count].fd) == -1) {
        perror("pipe");
        log_msg("ERROR", "Failed to create pipe %s", name);
        return;
    }

    pipes[pipe_count].id = pipe_count;
    strncpy(pipes[pipe_count].name, name, MAX_NAME_LEN-1);
    pipes[pipe_count].name[MAX_NAME_LEN-1] = '\0';
    pipes[pipe_count].process1 = process1;
    pipes[pipe_count].process2 = process2;
    pipes[pipe_count].active = 1;
    pipe_count++;

    printf("Created pipe %s with ID %d between process %d (write) and %d (read)\n",
           name, pipes[pipe_count-1].id, process1, process2);
    log_msg("INFO", "Created pipe %s with ID %d between process %d and %d",
            name, pipes[pipe_count-1].id, process1, process2);
}

void list_pipes() {
    printf("\n=== Active Pipes ===\n");
    printf("%-10s %-20s %-10s %-10s\n", "ID", "Name", "Writer", "Reader");
    printf("------------------------------------------------\n");
   
    int active_pipe_count = 0;
    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i].active) {
            printf("%-10d %-20s %-10d %-10d\n",
                   pipes[i].id,
                   pipes[i].name,
                   pipes[i].process1,
                   pipes[i].process2);
            active_pipe_count++;
        }
    }
    if (active_pipe_count == 0) {
        printf("No active pipes\n");
    }
}

void use_pipe() {
    list_pipes();
   
    if (pipe_count == 0) {
        printf("No pipes available\n");
        return;
    }
   
    int pipe_id;
    printf("Enter pipe ID to use: ");
    if (scanf("%d", &pipe_id) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Consume newline
   
    if (pipe_id < 0 || pipe_id >= pipe_count || !pipes[pipe_id].active) {
        printf("Invalid pipe ID\n");
        return;
    }
   
    printf("Pipe %s (ID %d) between process %d (writer) and %d (reader)\n",
           pipes[pipe_id].name, pipe_id, pipes[pipe_id].process1, pipes[pipe_id].process2);
   
    printf("\n--- Demonstrating Pipe Communication (Illustrative) ---\n");
    printf("Imagine process %d writes 'Hello Pipe!' to its write end (fd[1]).\n", pipes[pipe_id].process1);
    printf("Imagine process %d reads from its read end (fd[0]) and receives 'Hello Pipe!'.\n", pipes[pipe_id].process2);
    printf("In a real system, these operations would be performed by the actual processes.\n");
}

// =================== Synchronization Primitives ===================
void create_semaphore() {
    if (semaphore_count >= 10) {
        printf("Maximum number of semaphores reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    int value;
   
    printf("Enter semaphore name: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    printf("Enter initial value: ");
    if (scanf("%d", &value) != 1) {
        printf("Invalid input. Please enter a number.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Consume newline

    if (sem_init(&semaphores[semaphore_count].sem, 0, value) == 0) {
        strncpy(semaphores[semaphore_count].name, name, MAX_NAME_LEN-1);
        semaphores[semaphore_count].name[MAX_NAME_LEN-1] = '\0';
        semaphores[semaphore_count].value = value;
        semaphores[semaphore_count].active = 1;
        semaphore_count++;
       
        printf("Created semaphore %s with initial value %d\n", name, value);
        log_msg("INFO", "Created semaphore %s with value %d", name, value);
    } else {
        perror("sem_init");
        log_msg("ERROR", "Failed to create semaphore %s", name);
    }
}

void list_semaphores() {
    printf("\n=== Active Semaphores ===\n");
    printf("%-20s %-10s\n", "Name", "Value");
    printf("------------------------------\n");
   
    int active_sem_count = 0;
    for (int i = 0; i < semaphore_count; i++) {
        if (semaphores[i].active) {
            int sval;
            if (sem_getvalue(&semaphores[i].sem, &sval) == 0) {
                printf("%-20s %-10d\n", semaphores[i].name, sval);
                semaphores[i].value = sval; // Update stored value
            } else {
                printf("%-20s %-10s (Error getting value)\n", semaphores[i].name, "N/A");
                log_msg("ERROR", "Failed to get value for semaphore %s", semaphores[i].name);
            }
            active_sem_count++;
        }
    }
    if (active_sem_count == 0) {
        printf("No active semaphores\n");
    }
}

void semaphore_wait() {
    list_semaphores();
   
    if (semaphore_count == 0) {
        printf("No semaphores available\n");
        return;
    }
   
    char name[MAX_NAME_LEN];
    printf("Enter semaphore name to wait on: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline
   
    for (int i = 0; i < semaphore_count; i++) {
        if (strcmp(semaphores[i].name, name) == 0 && semaphores[i].active) {
            printf("Attempting to wait on semaphore %s...\n", name);
            if (sem_wait(&semaphores[i].sem) == 0) {
                int sval;
                sem_getvalue(&semaphores[i].sem, &sval);
                semaphores[i].value = sval;
                printf("Acquired semaphore %s (new value: %d)\n", name, semaphores[i].value);
                log_msg("INFO", "Acquired semaphore %s (value: %d)", name, semaphores[i].value);
            } else {
                perror("sem_wait");
                log_msg("ERROR", "Failed to wait on semaphore %s", name);
            }
            return;
        }
    }
   
    printf("Semaphore %s not found or not active\n", name);
}

void semaphore_post() {
    list_semaphores();
   
    if (semaphore_count == 0) {
        printf("No semaphores available\n");
        return;
    }
   
    char name[MAX_NAME_LEN];
    printf("Enter semaphore name to post: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline
   
    for (int i = 0; i < semaphore_count; i++) {
        if (strcmp(semaphores[i].name, name) == 0 && semaphores[i].active) {
            printf("Attempting to post to semaphore %s...\n", name);
            if (sem_post(&semaphores[i].sem) == 0) {
                int sval;
                sem_getvalue(&semaphores[i].sem, &sval);
                semaphores[i].value = sval;
                printf("Posted to semaphore %s (new value: %d)\n", name, semaphores[i].value);
                log_msg("INFO", "Posted to semaphore %s (value: %d)", name, semaphores[i].value);
            } else {
                perror("sem_post");
                log_msg("ERROR", "Failed to post to semaphore %s", name);
            }
            return;
        }
    }
   
    printf("Semaphore %s not found or not active\n", name);
}

void create_mutex() {
    if (mutex_count >= 10) {
        printf("Maximum number of mutexes reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    printf("Enter mutex name: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline

    if (pthread_mutex_init(&mutexes[mutex_count].mutex, NULL) == 0) {
        strncpy(mutexes[mutex_count].name, name, MAX_NAME_LEN-1);
        mutexes[mutex_count].name[MAX_NAME_LEN-1] = '\0';
        mutexes[mutex_count].active = 1;
        mutex_count++;
       
        printf("Created mutex %s\n", name);
        log_msg("INFO", "Created mutex %s", name);
    } else {
        perror("pthread_mutex_init");
        log_msg("ERROR", "Failed to create mutex %s", name);
    }
}

void list_mutexes() {
    printf("\n=== Active Mutexes ===\n");
    printf("%-20s\n", "Name");
    printf("--------------------\n");
   
    int active_mutex_count = 0;
    for (int i = 0; i < mutex_count; i++) {
        if (mutexes[i].active) {
            printf("%-20s\n", mutexes[i].name);
            active_mutex_count++;
        }
    }
    if (active_mutex_count == 0) {
        printf("No active mutexes\n");
    }
}

void mutex_lock() {
    list_mutexes();
   
    if (mutex_count == 0) {
        printf("No mutexes available\n");
        return;
    }
   
    char name[MAX_NAME_LEN];
    printf("Enter mutex name to lock: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline
   
    for (int i = 0; i < mutex_count; i++) {
        if (strcmp(mutexes[i].name, name) == 0 && mutexes[i].active) {
            printf("Attempting to lock mutex %s...\n", name);
            if (pthread_mutex_lock(&mutexes[i].mutex) == 0) {
                printf("Locked mutex %s\n", name);
                log_msg("INFO", "Locked mutex %s", name);
            } else {
                perror("pthread_mutex_lock");
                log_msg("ERROR", "Failed to lock mutex %s", name);
            }
            return;
        }
    }
   
    printf("Mutex %s not found or not active\n", name);
}

void mutex_unlock() {
    list_mutexes();
   
    if (mutex_count == 0) {
        printf("No mutexes available\n");
        return;
    }
   
    char name[MAX_NAME_LEN];
    printf("Enter mutex name to unlock: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline
   
    for (int i = 0; i < mutex_count; i++) {
        if (strcmp(mutexes[i].name, name) == 0 && mutexes[i].active) {
            printf("Attempting to unlock mutex %s...\n", name);
            if (pthread_mutex_unlock(&mutexes[i].mutex) == 0) {
                printf("Unlocked mutex %s\n", name);
                log_msg("INFO", "Unlocked mutex %s", name);
            } else {
                perror("pthread_mutex_unlock");
                log_msg("ERROR", "Failed to unlock mutex %s", name);
            }
            return;
        }
    }
   
    printf("Mutex %s not found or not active\n", name);
}

void create_condition_variable() {
    if (cond_var_count >= 10) {
        printf("Maximum number of condition variables reached!\n");
        return;
    }

    char name[MAX_NAME_LEN];
    printf("Enter condition variable name: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline

    if (pthread_cond_init(&cond_vars[cond_var_count].cond, NULL) == 0) {
        strncpy(cond_vars[cond_var_count].name, name, MAX_NAME_LEN-1);
        cond_vars[cond_var_count].name[MAX_NAME_LEN-1] = '\0';
        cond_vars[cond_var_count].active = 1;
        cond_var_count++;
       
        printf("Created condition variable %s\n", name);
        log_msg("INFO", "Created condition variable %s", name);
    } else {
        perror("pthread_cond_init");
        log_msg("ERROR", "Failed to create condition variable %s", name);
    }
}

void list_condition_variables() {
    printf("\n=== Active Condition Variables ===\n");
    printf("%-20s\n", "Name");
    printf("--------------------\n");
   
    int active_cv_count = 0;
    for (int i = 0; i < cond_var_count; i++) {
        if (cond_vars[i].active) {
            printf("%-20s\n", cond_vars[i].name);
            active_cv_count++;
        }
    }
    if (active_cv_count == 0) {
        printf("No active condition variables\n");
    }
}

void signal_condition_variable() {
    list_condition_variables();
   
    if (cond_var_count == 0) {
        printf("No condition variables available\n");
        return;
    }
   
    char name[MAX_NAME_LEN];
    printf("Enter condition variable name to signal: ");
    scanf("%49s", name);
    name[MAX_NAME_LEN-1] = '\0';
    while (getchar() != '\n'); // Consume newline
   
    for (int i = 0; i < cond_var_count; i++) {
        if (strcmp(cond_vars[i].name, name) == 0 && cond_vars[i].active) {
            // In a real application, you would typically lock a mutex before signaling
            // and the waiting threads would hold the same mutex.
            // For this demo, we illustrate the signal action itself.
            printf("Signaling condition variable %s...\n", name);
            if (pthread_cond_signal(&cond_vars[i].cond) == 0) {
                printf("Signaled condition variable %s (one waiting thread, if any, woken up)\n", name);
                log_msg("INFO", "Signaled condition variable %s", name);
            } else {
                perror("pthread_cond_signal");
                log_msg("ERROR", "Failed to signal condition variable %s", name);
            }
            return;
        }
    }
   
    printf("Condition variable %s not found or not active\n", name);
}

// =================== IPC Mechanisms ===================
void shared_memory_demo() {
    printf("\n=== Shared Memory Demo ===\n");
   
    int shmid = shmget(SHM_KEY, 1024, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        log_msg("ERROR", "shmget failed");
        return;
    }
   
    pid_t pid = fork();
    if (pid == 0) {
        // Child - Reader
        char* shm = (char*)shmat(shmid, NULL, 0);
        if (shm == (char*)-1) {
            perror("shmat child");
            exit(EXIT_FAILURE);
        }
       
        printf("Child (PID %d) read from shared memory: %s\n", getpid(), shm);
        log_msg("INFO", "Child (PID %d) read from shared memory: %s", getpid(), shm);
       
        shmdt(shm);
        exit(0);
    } else if (pid > 0) {
        // Parent - Writer
        char* shm = (char*)shmat(shmid, NULL, 0);
        if (shm == (char*)-1) {
            perror("shmat parent");
            log_msg("ERROR", "shmat parent failed");
            shmctl(shmid, IPC_RMID, NULL); // Clean up shared memory on parent error
            return;
        }
       
        const char* msg = "Hello from shared memory!";
        strncpy(shm, msg, 1023); // Copy with limit
        shm[1023] = '\0'; // Ensure null termination
       
        printf("Parent (PID %d) wrote to shared memory\n", getpid());
        log_msg("INFO", "Parent (PID %d) wrote to shared memory", getpid());
       
        waitpid(pid, NULL, 0); // Wait for child to finish
        shmdt(shm);
        shmctl(shmid, IPC_RMID, NULL); // Clean up shared memory
    } else {
        perror("fork");
        log_msg("ERROR", "fork failed in shared memory demo");
        shmctl(shmid, IPC_RMID, NULL);
    }
}

void named_pipe_demo() {
    printf("\n=== Named Pipe Demo ===\n");
   
    // Create the named pipe (FIFO)
    if (mkfifo(FIFO_NAME, 0666) == -1) {
        if (errno != EEXIST) { // Ignore if it already exists
            perror("mkfifo");
            log_msg("ERROR", "mkfifo failed");
            return;
        }
    }
   
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - Reader
        int fd = open(FIFO_NAME, O_RDONLY);
        if (fd < 0) {
            perror("open fifo read");
            exit(EXIT_FAILURE);
        }
       
        char buf[100];
        int n = read(fd, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = '\0';
            printf("Named pipe reader (PID %d) got: %s\n", getpid(), buf);
            log_msg("INFO", "Named pipe reader (PID %d) got: %s", getpid(), buf);
        } else if (n == 0) {
            printf("Named pipe reader (PID %d): End of file (writer closed pipe).\n", getpid());
        } else {
            perror("read fifo");
        }
       
        close(fd);
        exit(0);
    } else if (pid > 0) {
        // Parent process - Writer
        // Give child a moment to open the read end
        sleep(1);
        int fd = open(FIFO_NAME, O_WRONLY);
        if (fd < 0) {
            perror("open fifo write");
            log_msg("ERROR", "open fifo write failed");
            // Clean up the named pipe if opening fails for the writer
            unlink(FIFO_NAME);
            return;
        }
       
        const char* msg = "Hello from named pipe!";
        write(fd, msg, strlen(msg) + 1); // +1 to include null terminator
       
        printf("Named pipe writer (PID %d) sent: %s\n", getpid(), msg);
        log_msg("INFO", "Named pipe writer (PID %d) sent: %s", getpid(), msg);
       
        close(fd);
        waitpid(pid, NULL, 0); // Wait for the child process to finish
        unlink(FIFO_NAME);    // Clean up the named pipe file
    } else {
        perror("fork");
        log_msg("ERROR", "fork failed in named pipe demo");
        unlink(FIFO_NAME); // Clean up the named pipe in case of fork failure
    }
}

void unix_socket_demo() {
    printf("\n=== Unix Domain Socket Demo ===\n");

    int server_sock, client_sock, len;
    struct sockaddr_un server_addr, client_addr;

    // Create socket
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("socket");
        log_msg("ERROR", "socket creation failed");
        return;
    }

    // Remove any existing socket file to prevent bind errors
    unlink(UNIX_SOCKET_PATH);

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, UNIX_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    server_addr.sun_path[sizeof(server_addr.sun_path) - 1] = '\0'; // Ensure null termination
   
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        log_msg("ERROR", "socket bind failed");
        close(server_sock);
        return;
    }

    // Listen for connections
    if (listen(server_sock, 5) == -1) {
        perror("listen");
        log_msg("ERROR", "socket listen failed");
        close(server_sock);
        unlink(UNIX_SOCKET_PATH);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child - Client
        close(server_sock); // Client doesn't need server's listening socket
        client_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_sock == -1) {
            perror("client socket");
            exit(EXIT_FAILURE);
        }

        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sun_family = AF_UNIX;
        strncpy(client_addr.sun_path, UNIX_SOCKET_PATH, sizeof(client_addr.sun_path) - 1);
        client_addr.sun_path[sizeof(client_addr.sun_path) - 1] = '\0';

        // Wait for server to be ready to accept
        int connect_attempts = 0;
        while (connect(client_sock, (struct sockaddr *)&client_addr, sizeof(client_addr)) == -1) {
            if (errno == ENOENT || errno == ECONNREFUSED) { // Server socket not yet created or listening
                if (connect_attempts++ < 10) {
                    sleep(1); // Wait a bit and retry
                } else {
                    perror("client connect (max retries)");
                    close(client_sock);
                    exit(EXIT_FAILURE);
                }
            } else {
                perror("client connect");
                close(client_sock);
                exit(EXIT_FAILURE);
            }
        }
       
        char buffer[100];
        ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Unix socket client (PID %d) received: %s\n", getpid(), buffer);
            log_msg("INFO", "Unix socket client (PID %d) received: %s", getpid(), buffer);
        } else if (bytes_received == 0) {
            printf("Unix socket client (PID %d): Server closed connection.\n", getpid());
        } else {
            perror("recv");
        }
        close(client_sock);
        exit(0);
    } else if (pid > 0) {
        // Parent - Server
        printf("Unix socket server (PID %d) waiting for connection...\n", getpid());
        len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr )&client_addr, (socklen_t)&len);
        if (client_sock == -1) {
            perror("accept");
            log_msg("ERROR", "socket accept failed");
            close(server_sock);
            unlink(UNIX_SOCKET_PATH);
            return;
        }

        const char* msg = "Hello from Unix socket!";
        send(client_sock, msg, strlen(msg) + 1, 0);

        printf("Unix socket server (PID %d) sent: %s\n", getpid(), msg);
        log_msg("INFO", "Unix socket server (PID %d) sent: %s", getpid(), msg);

        close(client_sock);
        close(server_sock);
        waitpid(pid, NULL, 0);
        unlink(UNIX_SOCKET_PATH); // Clean up socket file
    } else {
        perror("fork");
        log_msg("ERROR", "fork failed in unix socket demo");
        close(server_sock);
        unlink(UNIX_SOCKET_PATH);
    }
}

void message_queue_demo() {
    printf("\n=== Message Queue Demo ===\n");

    int msgid;
    key_t key = MSG_QUEUE_KEY;

    // Create message queue
    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        log_msg("ERROR", "msgget failed");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child - Receiver
        Message recv_msg;
        // The last argument (0) means block until a message of type 1 is available
        ssize_t bytes_received = msgrcv(msgid, &recv_msg, sizeof(recv_msg.mtext), 1, 0);
        if (bytes_received == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }
        recv_msg.mtext[bytes_received] = '\0'; // Ensure null termination
        printf("Message queue receiver (PID %d) got: %s\n", getpid(), recv_msg.mtext);
        log_msg("INFO", "Message queue receiver (PID %d) got: %s", getpid(), recv_msg.mtext);
        exit(0);
    } else if (pid > 0) {
        // Parent - Sender
        Message send_msg;
        send_msg.mtype = 1; // Message type
        strncpy(send_msg.mtext, "Hello from message queue!", sizeof(send_msg.mtext) - 1);
        send_msg.mtext[sizeof(send_msg.mtext) - 1] = '\0'; // Ensure null termination

        // Send message with flag 0 (block if queue is full)
        if (msgsnd(msgid, &send_msg, strlen(send_msg.mtext) + 1, 0) == -1) {
            perror("msgsnd");
            log_msg("ERROR", "msgsnd failed");
            msgctl(msgid, IPC_RMID, NULL); // Clean up on error
            return;
        }
        printf("Message queue sender (PID %d) sent: %s\n", getpid(), send_msg.mtext);
        log_msg("INFO", "Message queue sender (PID %d) sent: %s", getpid(), send_msg.mtext);

        waitpid(pid, NULL, 0); // Wait for child to finish
        msgctl(msgid, IPC_RMID, NULL); // Clean up message queue
    } else {
        perror("fork");
        log_msg("ERROR", "fork failed in message queue demo");
        msgctl(msgid, IPC_RMID, NULL);
    }
}


// =================== Main Menu & Cleanup ===================
void display_menu() {
    clear_screen();
    printf("=======================================\n");
    printf("        Advanced Process Manager       \n");
    printf("=======================================\n");
    printf("  Process Management:\n");
    printf("    1. Create Process\n");
    printf("    2. List Processes\n");
    printf("    3. Show Process Hierarchy\n");
    printf("    4. Terminate Process\n");
    printf("    5. Wait() System Call Demo\n");
    printf("  Thread Management:\n");
    printf("    6. Create Thread\n");
    printf("    7. List Threads\n");
    printf("    8. Terminate Thread\n");
    printf("  IPC Mechanisms:\n");
    printf("    9. Create Pipe\n");
    printf("   10. List Pipes\n");
    printf("   11. Use Pipe (Illustrative)\n");
    printf("   12. Shared Memory Demo\n");
    printf("   13. Named Pipe (FIFO) Demo\n");
    printf("   14. Unix Domain Socket Demo\n");
    printf("   15. Message Queue Demo\n");
    printf("  Synchronization Primitives:\n");
    printf("   16. Create Semaphore\n");
    printf("   17. List Semaphores\n");
    printf("   18. Semaphore Wait (P operation)\n");
    printf("   19. Semaphore Post (V operation)\n");
    printf("   20. Create Mutex\n");
    printf("   21. List Mutexes\n");
    printf("   22. Mutex Lock\n");
    printf("   23. Mutex Unlock\n");
    printf("   24. Create Condition Variable\n");
    printf("   25. List Condition Variables\n");
    printf("   26. Signal Condition Variable\n");
    printf("   27. Clear Screen\n");
    printf("   28. Exit\n");
    printf("---------------------------------------\n");
    printf("Enter your choice: ");
}

void cleanup() {
    log_msg("INFO", "Starting cleanup...");

    // Terminate all active processes
    for (int i = 0; i < process_count; i++) {
        if (processes[i].active) {
            log_msg("INFO", "Terminating process %d (%s)", processes[i].pid, processes[i].name);
            kill(processes[i].pid, SIGTERM);
            // Use WNOHANG to avoid blocking indefinitely if process doesn't exit quickly
            waitpid(processes[i].pid, NULL, WNOHANG);
            processes[i].active = 0; // Mark as inactive
        }
    }

    // Destroy all active threads
    for (int i = 0; i < thread_count; i++) {
        if (threads[i].active) {
            log_msg("INFO", "Canceling thread %lu (%s)", (unsigned long)threads[i].tid, threads[i].name);
            pthread_cancel(threads[i].tid);
            pthread_join(threads[i].tid, NULL); // Join to ensure resources are released
            threads[i].active = 0; // Mark as inactive
        }
    }

    // Close and unlink pipes (named and unnamed)
    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i].active) {
            close(pipes[i].fd[0]);
            close(pipes[i].fd[1]);
            log_msg("INFO", "Closed pipe %s", pipes[i].name);
            pipes[i].active = 0;
        }
    }
    if (access(FIFO_NAME, F_OK) == 0) { // Check if FIFO file exists
        unlink(FIFO_NAME); // Named pipe cleanup
        log_msg("INFO", "Cleaned up named pipe file %s", FIFO_NAME);
    }


    // Destroy semaphores
    for (int i = 0; i < semaphore_count; i++) {
        if (semaphores[i].active) {
            sem_destroy(&semaphores[i].sem);
            log_msg("INFO", "Destroyed semaphore %s", semaphores[i].name);
            semaphores[i].active = 0;
        }
    }

    // Destroy mutexes
    for (int i = 0; i < mutex_count; i++) {
        if (mutexes[i].active) {
            pthread_mutex_destroy(&mutexes[i].mutex);
            log_msg("INFO", "Destroyed mutex %s", mutexes[i].name);
            mutexes[i].active = 0;
        }
    }

    // Destroy condition variables
    for (int i = 0; i < cond_var_count; i++) {
        if (cond_vars[i].active) {
            pthread_cond_destroy(&cond_vars[i].cond);
            log_msg("INFO", "Destroyed condition variable %s", cond_vars[i].name);
            cond_vars[i].active = 0;
        }
    }

    // Clean up shared memory segment (if it exists)
    int shmid_check = shmget(SHM_KEY, 0, 0); // Get ID without creating
    if (shmid_check != -1) {
        shmctl(shmid_check, IPC_RMID, NULL);
        log_msg("INFO", "Cleaned up shared memory segment with key %x", SHM_KEY);
    }

    // Clean up Unix domain socket file
    if (access(UNIX_SOCKET_PATH, F_OK) == 0) { // Check if file exists
        unlink(UNIX_SOCKET_PATH);
        log_msg("INFO", "Cleaned up Unix domain socket file %s", UNIX_SOCKET_PATH);
    }

    // Clean up message queue (if it exists)
    int msgid_check = msgget(MSG_QUEUE_KEY, 0); // Get ID without creating
    if (msgid_check != -1) {
        msgctl(msgid_check, IPC_RMID, NULL);
        log_msg("INFO", "Cleaned up message queue with key %x", MSG_QUEUE_KEY);
    }

    close_logging();
    printf("Cleanup complete. Exiting.\n");
}

int main() {
    init_logging();
    setup_signal_handlers();

    int choice;
    do {
        display_menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            // Clear invalid input from buffer
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            choice = 0; // Set to an invalid choice to loop again
        } else {
            // Consume the rest of the line including the newline
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }

        if (sigint_received) {
            break; // Exit loop if SIGINT was received
        }

        switch (choice) {
            case 1: create_process(); break;
            case 2: list_processes(); break;
            case 3: show_process_hierarchy(); break;
            case 4: terminate_process(); break;
            case 5: wait_demo(); break;
            case 6: create_thread(); break;
            case 7: list_threads(); break;
            case 8: terminate_thread(); break;
            case 9: create_pipe(); break;
            case 10: list_pipes(); break;
            case 11: use_pipe(); break;
            case 12: shared_memory_demo(); break;
            case 13: named_pipe_demo(); break;
            case 14: unix_socket_demo(); break;
            case 15: message_queue_demo(); break;
            case 16: create_semaphore(); break;
            case 17: list_semaphores(); break;
            case 18: semaphore_wait(); break;
            case 19: semaphore_post(); break;
            case 20: create_mutex(); break;
            case 21: list_mutexes(); break;
            case 22: mutex_lock(); break;
            case 23: mutex_unlock(); break;
            case 24: create_condition_variable(); break;
            case 25: list_condition_variables(); break;
            case 26: signal_condition_variable(); break;
            case 27: clear_screen(); break;
            case 28: break; // Exit
            default: printf("Invalid choice. Please try again.\n"); break;
        }
        if (choice != 27 && choice != 28 && !sigint_received) { // Don't pause on clear screen or exit
            press_enter_to_continue();
        }
    } while (choice != 28 && !sigint_received);

    cleanup();
    return 0;
}