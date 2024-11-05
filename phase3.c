#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <string.h>
#include <phase3_usermode.h>
#include <stdio.h>

// Data structures and global variables

typedef struct Semaphore
{
    int in_use;
    int value;
    int mailbox_id;
} Semaphore;

typedef struct ProcessData
{
    int (*user_func)(void *);
    void* user_arg;
} ProcessData;

static Semaphore semaphores[MAXSEMS];
static int process_mailboxes[MAXPROC];
static ProcessData process_data[MAXPROC];

int semaphore_global_mutex; // Mailbox operating as a mutex for all semaphores

// Helpers
void gain_lock_semaphore_global()
{
    MboxSend(semaphore_global_mutex, NULL, 0);
}

void release_lock_semaphore_global()
{
    MboxRecv(semaphore_global_mutex, NULL, 0);
}

// Called by the trampoline - waits until Spawn releases the lock before waking up
void gain_process_lock()
{
    int mbox = process_mailboxes[getpid() % MAXPROC];
    MboxRecv(mbox, NULL, 0);
}

// Called by Spawn - wakes up the trampoline function after Spawn's work is done
// Uses CondSend since otherwise it would block (due to the zero slot mailboxes being used)
void release_process_lock(int pid)
{
    int mbox = process_mailboxes[pid % MAXPROC];
    MboxCondSend(mbox, NULL, 0);
}

// Blocks the current process until a resource is available in the Semaphore
// MboxSend adds process to producer queue
void wait_resource(Semaphore *semaphore)
{
    MboxSend(semaphore->mailbox_id, NULL, 0);
}

// Frees a resource in the Semaphore
// MboxRecv removes a process from the producer queue if there is one
// Uses MboxCondRecv because SemV is not supposed to block
void free_resource(Semaphore *semaphore)
{
    MboxRecv(semaphore->mailbox_id, NULL, 0);
}

// Semaphore syscall handlers
void semaphore_create(USLOSS_Sysargs *args)
{
    int value = (int)(long)args->arg1;

    if (value < 0) // Invalid starting value
    {
        args->arg1 = 0;
        args->arg4 = -1;
    }
    else
    {
        // Search for the first empty semaphore
        Semaphore *target = NULL;
        int sid;
        for (sid = 0; sid < MAXSEMS; sid++)
        {
            if (!semaphores[sid].in_use)
            {
                target = &semaphores[sid];
                break;
            }
        }

        if (target == NULL) // No free semaphores
        {
            args->arg1 = 0;
            args->arg4 = -1;
        }
        else // Initialize semaphore
        {
            target->in_use = 1;
            target->value = value;
            target->mailbox_id = MboxCreate(value, 0);

            args->arg1 = sid;
            args->arg4 = 0;
        }
    }
}

void semaphore_v(USLOSS_Sysargs *args)
{
    // Gain the global semaphore mutex lock
    gain_lock_semaphore_global();

    int sid = (int)(long)args->arg1;
    Semaphore *semaphore = &semaphores[sid];

    // Release a semaphore resource (increment the value)
    // May also free a blocked process that was waiting for a resource
    semaphore->value++;
    free_resource(semaphore);

    // Release the global semaphore mutex lock
    release_lock_semaphore_global();
}

void semaphore_p(USLOSS_Sysargs *args)
{
    // Gain the global semaphore mutex lock
    gain_lock_semaphore_global();

    int sid = (int)(long)args->arg1;
    Semaphore *semaphore = &semaphores[sid];

    // If there are no semaphore resources, release the mutex (avoid deadlock) and block, waiting for a resource
    // SemV will unblock this process when a resource is available
    if(semaphore->value <= 0)
    {
        release_lock_semaphore_global();
        wait_resource(semaphore);

        // Regain the global semaphore mutex lock after returning, prior to modifying the semaphore value
        gain_lock_semaphore_global();
    }

    // At this point, semaphore resource is available, so decrement the value and release the global mutex
    semaphore->value--;
    release_lock_semaphore_global();
}

// System call handlers

void user_process_wrapper() // Trampoline function that handles calling the user mode process
{
    // Wait for the caller Spawn to complete its work and release the lock
    gain_process_lock();

    // Enable user mode
    USLOSS_PsrSet(USLOSS_PsrGet() & ~0x1);

    // Call user mode function
    ProcessData* data = &process_data[getpid() % MAXPROC];
    int status = data->user_func(data->user_arg);

    // Terminate if the above function returns
    Terminate(status);
}

void spawn_handler(USLOSS_Sysargs *args)
{
    int pid = spork(args->arg5, user_process_wrapper, NULL, args->arg3, args->arg4);

    // Store the necessary data the trampoline function will need
    ProcessData* data = &process_data[pid % MAXPROC];
    memset(data, 0, sizeof(ProcessData));
    data->user_func = args->arg1;
    data->user_arg = args->arg2;

    // Return args
    if (pid == -1) // Error creating child
    {
        args->arg1 = -1;
        args->arg4 = 0;
    }
    else
    {
        args->arg1 = pid;
        args->arg4 = 0;
    }

    // Allow trampoline function to wake up before the syscall handler returns
    release_process_lock(pid);
}

void wait_handler(USLOSS_Sysargs *args)
{
    int status = 0;
    int pid = join(&status);

    if (pid == -2) // No children
    {
        args->arg4 = -2;
    }
    else
    {
        args->arg1 = pid;    // PID of cleaned-up process
        args->arg2 = status; // Status of cleaned-up process
        args->arg4 = 0;      // Success
    }
}

void terminate_handler(USLOSS_Sysargs *args)
{
    int status = (int)args->arg1;

    int pid;
    while (join(&pid) != -2)
    {
        // Wait for all child processes to terminate
    }

    quit(status); // This function will never return
}

void get_time_handler(USLOSS_Sysargs *args)
{
    int currentTimeValue = currentTime();  // Get the current time
    args->arg1 = (void *)currentTimeValue; // Store it in arg1 for return
}

void get_pid_handler(USLOSS_Sysargs *args)
{
    int pid = getpid();       // Retrieve the current process's PID
    args->arg1 = (void *)pid; // Store it in arg1 for return
}

// Initializes stuff for phase 3
void phase3_init()
{
    // Clear out shadow process table and semaphores
    memset(semaphores, 0, sizeof(semaphores));
    memset(process_mailboxes, 0, sizeof(process_mailboxes));
    memset(process_data, 0, sizeof(process_data));

    // Assign syscall handlers
    systemCallVec[SYS_SEMCREATE] = semaphore_create;
    systemCallVec[SYS_SEMV] = semaphore_v;
    systemCallVec[SYS_SEMP] = semaphore_p;

    systemCallVec[SYS_SPAWN] = spawn_handler;
    systemCallVec[SYS_WAIT] = wait_handler;
    systemCallVec[SYS_TERMINATE] = terminate_handler;

    systemCallVec[SYS_GETTIMEOFDAY] = get_time_handler;
    systemCallVec[SYS_GETPID] = get_pid_handler;

    // Mailbox creation
    semaphore_global_mutex = MboxCreate(1, 0);

    for(int i = 0; i < MAXPROC; i++) // Create the zero-slot mailboxes for all processes, for the Spawn-wrapper interaction
        process_mailboxes[i] = MboxCreate(1, 0);
}

void phase3_start_service_processes()
{
    // Unused for this phase
}