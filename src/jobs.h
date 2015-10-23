#ifndef __JOBS_H__
#define __JOBS_H__

struct JOBS
{
    char *jseq;
    pid_t pid_number;
    struct JOBS * next;
    int end;
};
typedef struct JOBS jobs;

// Print jobs and free those which are terminated.
void print_jobs();
// Add a job to the job list.
void add_jobs(pid_t pidj, char * seql);
// Free jobs
void free_jobs();

// Global variable : jobs list
jobs * jlist = NULL;

#endif
