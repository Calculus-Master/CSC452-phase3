#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>

// Data structures and global variables

typedef struct Semaphore {
    int id;
} Semaphore;

static Semaphore semaphores[MAXSEMS];

// Semaphore stuff

// System call handlers
void spawnHandler(USLOSS_Sysargs* args)
{

}

// Initializes stuff for phase 3
void phase3_init()
{
    // Assign syscall handlers
    systemCallVec[SYS_SPAWN] = spawnHandler;
}

void phase3_start_service_processes()
{
    // Unused for this phase
}