#ifndef _JOBS_
#define _JOBS_

struct jobs
{
    char *jseq;
    pid_t pid_number;
    int end;
    struct jobs * next;
};
typedef struct jobs jobs;

// Print jobs and free those which are terminated.
void print_jobs();
// Add a job to the job list.
void add_jobs(pid_t pidj, char * seql);
// Free jobs
void free_jobs();

// Global variable : jobs list
jobs * jlist = NULL;

#endif
