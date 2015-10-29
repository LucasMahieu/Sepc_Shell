#ifndef __JOBS_H__
#define __JOBS_H__

struct JOBS
{
    // Commande rentrée
    char *jseq;
    // Tableau des pids
    pid_t *pid_number;
    // Date de début d'execution de la commande.
    struct timeval* begin;
    // Nombre de séquences dans la commande.
    int nb;
    struct JOBS * next;
};
typedef struct JOBS jobs;

// Print jobs and free those which are terminated.
void print_jobs();
// Add a job to the job list.
void add_jobs(pid_t *pidj, char * seql, int nb_seq);

// Global variable : jobs list
jobs * jlist = NULL;

#endif
