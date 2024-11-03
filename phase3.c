#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <string.h>
#include <phase3_usermode.h>

// Data structures and global variables

typedef struct Semaphore {
    int in_use;
    int value;
} Semaphore;

typedef struct ShadowProcess {

} ShadowProcess;

static Semaphore semaphores[MAXSEMS];
static ShadowProcess process_table[MAXPROC];

// Semaphore syscall handlers
void semaphore_create(USLOSS_Sysargs* args)
{
    int value = (int)(long)args->arg1;

    if(value < 0) // Invalid starting value
    {
        args->arg1 = -1;
        args->arg4 = -1;
    }
    else
    {
        // Search for the first empty semaphore
        Semaphore* target = NULL;
        int sid;
        for(sid = 0; sid < MAXSEMS; sid++)
        {
            if(!semaphores[sid].in_use)
            {
                target = &semaphores[sid];
                break;
            }
        }

        if(target == NULL) // No free semaphores
        {
            args->arg1 = -1;
            args->arg4 = -1;
        }
        else // Initialize semaphore
        {
            target->in_use = 1;
            target->value = value;

            args->arg1 = sid;
            args->arg4 = 0;
        }
    }
}

void semaphore_v(USLOSS_Sysargs* args)
{

}

void semaphore_p(USLOSS_Sysargs* args)
{

}

// System call handlers

void user_process_wrapper(USLOSS_Sysargs* args) // Trampoline function that handles calling the user mode process
{
    // Enable user mode
    USLOSS_PsrSet(USLOSS_PsrGet() | ~USLOSS_PSR_CURRENT_MODE);

    // Call user mode function
    int (*user_func)(void*) = (int (*)(void*))args->arg1;
    int status = user_func(args->arg2);

    // Terminate if the above function returns
    Terminate(status);
}

void spawn_handler(USLOSS_Sysargs* args)
{
    int pid = spork(args->arg5, user_process_wrapper, args, args->arg3, args->arg4);

    if(pid == -1) // Error creating child
    {
        args->arg1 = -1;
        args->arg4 = 0;
    }
    else
    {
        args->arg1 = pid;
        args->arg4 = 0;
    }
}

void wait_handler(USLOSS_Sysargs* args)
{

}

void terminate_handler(USLOSS_Sysargs* args)
{

}

void get_time_handler(USLOSS_Sysargs* args)
{

}

void get_pid_handler(USLOSS_Sysargs* args)
{

}

// Initializes stuff for phase 3
void phase3_init()
{
    // Clear out shadow process table and semaphores
    memset(semaphores, 0, sizeof(semaphores));
    memset(process_table, 0, sizeof(process_table));

    // Assign syscall handlers
    systemCallVec[SYS_SEMCREATE] = semaphore_create;
    systemCallVec[SYS_SEMV] = semaphore_v;
    systemCallVec[SYS_SEMP] = semaphore_p;

    systemCallVec[SYS_SPAWN] = spawn_handler;
    systemCallVec[SYS_WAIT] = wait_handler;
    systemCallVec[SYS_TERMINATE] = terminate_handler;
}

void phase3_start_service_processes()
{
    // Unused for this phase
}