#ifndef PSINFO_H
#define PSINFO_H

#define MAX_PROCS 64 // MAX_PROCS = NPROC (param.h)
#define MAX_CPU 8 // MAX_CPU = NCPU (param.h)

struct psinfo {
    int pid;
    char name[16];
    int state;
    char parent_name[16];
    char cpu_process_name[16];
};

#endif // PSINFO_H

