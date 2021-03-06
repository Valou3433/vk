/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TASK_HEAD
#define TASK_HEAD

#include "system.h"
#include "filesystem/fs.h"
#include "io/io.h"
#include "processes/signal.h"

//ELF loading
error_t elf_check(fd_t* file);
void* elf_load(fd_t* file, u32* page_directory, list_entry_t* data_loc, u32* data_size);

#define PROCESS_STATUS_INIT 0 //the process is in INIT state
#define PROCESS_STATUS_RUNNING 1 //the process is on the active process queue, or running currently
#define PROCESS_STATUS_ASLEEP_THREADS 2 //the process is asleep cause all his threads are asleep
#define PROCESS_STATUS_ASLEEP_SIGNAL 3 //the process has been stopped by a signal
#define PROCESS_STATUS_ZOMBIE 10 //the process is dead

#define THREAD_STATUS_INIT 0
#define THREAD_STATUS_RUNNING 1
#define THREAD_STATUS_ASLEEP_TIME 2
#define THREAD_STATUS_ASLEEP_IRQ 3
#define THREAD_STATUS_ASLEEP_IO 5
#define THREAD_STATUS_ASLEEP_CHILD 6
#define THREAD_STATUS_ASLEEP_MUTEX 7
#define THREAD_STATUS_ZOMBIE 10

//Process groups and sessions
typedef struct psession
{
    list_entry_t* groups;
    tty_t* controlling_tty;
} psession_t;

typedef struct pgroup
{
    int gid;
    list_entry_t* processes;
    psession_t* session;
} pgroup_t;

//Signals
void signals_init();
void send_signal(int pid, int sig);
void send_signal_to_group(int gid, int sig);

//Processes
typedef struct THREAD
{
    //registers (backed up every schedule)
    g_regs_t gregs;
    s_regs_t sregs;
    u32 eip;
    u32 esp;
    u32 ebp;
    u32 kesp;
    u32 base_stack;
    u32 base_kstack;
    u32 status;
} __attribute__((packed)) thread_t;
typedef struct PROCESS
{
    //threads
    thread_t* active_thread;
    queue_t* running_threads;
    list_entry_t* waiting_threads;
    u32 flags;
    //page directory of the process
    u32* page_directory;
    //location of all elf data segments in memory (to free them on exit_process)
    list_entry_t* data_loc;
    u32 data_size;
    //heap
    u32 heap_addr;
    u32 heap_size;
    //tty of the process
    tty_t* tty;
    //files opened by the process
    fd_t** files;
    u32 files_size;
    u32 files_count;
    //pid
    int pid;
    pgroup_t* group;
    psession_t* session;
    u32 status;
    list_entry_t* children;
    struct PROCESS* parent;
    //signals
    void* signal_handlers[NSIG];
    //current directory
    char current_dir[100];
} __attribute__((packed)) process_t;

#define PROCESS_INVALID_PID -1
#define PROCESS_KERNEL_PID -2
#define PROCESS_IDLE_PID -3

#define EXIT_CONDITION_USER ((u32)(1 << 8))
#define EXIT_CONDITION_SIGNAL ((u32)(2 << 8))

#define PROCESS_KSTACK_SIZE_DEFAULT 8192
#define PROCESS_STACK_SIZE_DEFAULT 8192

void process_init();
error_t spawn_init_process();
void free_process_memory(process_t* process);
error_t load_executable(process_t* process, fd_t* executable, int argc, char** argv, char** env, int envc);
void exit_process(process_t* process, u32 exitcode);
u32 sbrk(process_t* process, u32 incr);
process_t* fork(process_t* process, u32 old_esp);
int fork_ret();

extern process_t** processes;
extern u32 processes_size;

void groups_init();
pgroup_t* get_group(int gid);
error_t process_setgroup(int gid, process_t* process);
extern pgroup_t* groups;
extern u32 groups_number;

extern process_t* kernel_process;
extern process_t* idle_process;
process_t* init_idle_process();
process_t* init_kernel_process();

//THREADS
thread_t* init_thread();
void free_thread_memory(process_t* process, thread_t* thread);
void scheduler_remove_thread(process_t* process, thread_t* thread);
void scheduler_add_thread(process_t* process, thread_t* thread);

//SCHEDULER
extern bool scheduler_started;
extern process_t* current_process;
void scheduler_init();
void scheduler_start();
void schedule();

//add/remove from queue
void scheduler_add_process(process_t* process);
void scheduler_remove_process(process_t* process);

//sleep/awake
#define SLEEP_WAIT_IRQ 1
#define SLEEP_PAUSED 2
#define SLEEP_TIME 3
#define SLEEP_WAIT_MUTEX 4

void scheduler_wait_thread(process_t* process, thread_t* thread, u8 sleep_reason, u16 sleep_data, u16 sleep_data_2);
void scheduler_irq_wakeup(u32 irq);

#endif