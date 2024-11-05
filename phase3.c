#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <string.h>
#include <phase3_usermode.h>
#include <stdio.h>

// Data structures and global variables

typedef struct ProcessData
{
    int (*user_func)(void *);
    void* user_arg;

    struct ProcessData* next;
} ProcessData;

typedef struct Semaphore
{
    int in_use;
    int value;

    int mutex_mailbox;
    int waiting_mailbox;

    int num_waiting;
} Semaphore;

static Semaphore semaphores[MAXSEMS];
static int process_mailboxes[MAXPROC];
static ProcessData process_data[MAXPROC];

// Helpers
void gain_semaphore_lock(Semaphore* semaphore)
{
    MboxSend(semaphore->mutex_mailbox, NULL, 0);
}

void release_semaphore_lock(Semaphore* semaphore)
{
    MboxRecv(semaphore->mutex_mailbox, NULL, 0);
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
    MboxSend(semaphore->waiting_mailbox, NULL, 0);
}

// Frees a resource in the Semaphore
// MboxRecv removes a process from the producer queue if there is one
// Uses MboxCondRecv because SemV is not supposed to block
void free_resource(Semaphore *semaphore)
{
    MboxRecv(semaphore->waiting_mailbox, NULL, 0);
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

            target->mutex_mailbox = MboxCreate(1, 0);
            target->waiting_mailbox = MboxCreate(value, 0);

            target->num_waiting = 0;

            args->arg1 = sid;
            args->arg4 = 0;
        }
    }
}

void semaphore_v(USLOSS_Sysargs *args)
{
    int sid = (int)(long)args->arg1;
    Semaphore *semaphore = &semaphores[sid];

    // Gain the semaphore's mutex lock prior to modifying the semaphore value
    gain_semaphore_lock(semaphore);

    // Release a semaphore resource (increment the value)
    // May also free a blocked process that was waiting for a resource
    semaphore->value++;
    if(semaphore->num_waiting > 0) free_resource(semaphore);

    // Release the semaphore's mutex lock
    release_semaphore_lock(semaphore);
}

void semaphore_p(USLOSS_Sysargs *args)
{
    int sid = (int)(long)args->arg1;
    Semaphore *semaphore = &semaphores[sid];

    // Gain the semaphore's mutex lock prior to modifying the semaphore value
    gain_semaphore_lock(semaphore);

    int did_block = 0;
    // If there are no semaphore resources, release the mutex (avoid deadlock) and block, waiting for a resource
    // SemV will unblock this process when a resource is available
    if(semaphore->value <= 0)
    {
        release_semaphore_lock(semaphore);
        semaphore->num_waiting++;
        did_block = 1;
        wait_resource(semaphore);

        // Regain the global semaphore mutex lock after returning, prior to modifying the semaphore value
        gain_semaphore_lock(semaphore);
    }

    // At this point, semaphore resource is available, so decrement the value and release the global mutex
    semaphore->value--;
    if(did_block) semaphore->num_waiting--;
    release_semaphore_lock(semaphore);
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
    for(int i = 0; i < MAXPROC; i++) // Create the single-slot mailboxes for all processes, for the Spawn-wrapper interaction
        process_mailboxes[i] = MboxCreate(1, 0);
}

void phase3_start_service_processes()
{
    // Unused for this phase
}