/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include "variante.h"
#include "readcmd.h"
#include "jobs.h"
void terminate(char *line);

struct timeval* global_time=NULL;


#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

// Add new jobs to the global jobs list.
void add_jobs(pid_t pidj, char * seql)
{
    jobs * toAdd = NULL;
    toAdd = malloc(sizeof(*toAdd));
    
    toAdd->pid_number = pidj;
    toAdd->jseq = malloc(sizeof(char) * (strlen(seql) + 1));
    strcpy(toAdd->jseq,seql);

    if (jlist == NULL)
    {
        jlist = toAdd;
        toAdd->next = NULL;
    }
    else
    {
        toAdd->next = jlist;
        jlist = toAdd;
    }
}

// Print the current job list.
void print_jobs()
{
    int status = 1;
    jobs * tmp = NULL;
    for(tmp = jlist; tmp != NULL; tmp = tmp -> next)
    {
        // WNOHANG so that we don't wait but only check status.
        waitpid(tmp->pid_number, &status, WNOHANG);

        printf("pid : %d | command was : %s \n", tmp->pid_number, tmp->jseq);
    }
}

// Free finished jobs in the job list.
void free_jobs()
{
    int status = 1;
    jobs * tmp = NULL;
    jobs * tmp_prev = NULL;
    if (jlist == NULL)
    {
        return;
    }
    tmp_prev = jlist;
    for(tmp = jlist; tmp != NULL; tmp = tmp->next)
    {
        // WNOHANG so that we don't wait but only check status.
        waitpid(tmp->pid_number, &status, WNOHANG);
        if (!status)
        {
            // A commenter ou non en fonction des tests.
            //printf("%s (PID = %d) is over.\n", tmp->jseq, tmp->pid_number);
            tmp->end = 1;
        }
        else
        {
            tmp->end = 0;
        }
 
        if (tmp->end && tmp == jlist)
        {
            jlist = tmp->next;
            free(tmp->jseq);
            free(tmp);
        }
        else if (tmp->end)
        {
            tmp_prev->next = tmp->next;
            free(tmp->jseq);
            free(tmp);
        }
        tmp_prev = tmp;
    }
}

// Cas d'execution dans le cas sans pipe.
int exec_simple_cmd(struct cmdline *cmd,char *cpyLine) {
    pid_t pid;
    int status;

    // Descripteur pour le fichier eventuel en entrée
    int fd_in;
    // Descripteur pour le fichier eventuel en sortie
    int fd_out;
    
    switch(pid = fork()) {
        case 0:
            // S'il y a un fichier en entrée.
            if (cmd->in != NULL) {
                fd_in = open(cmd->in, O_RDONLY);
                if (fd_in == -1) {
                    return -1;
                }
                dup2(fd_in, 0);
                if (close(fd_in)) return -1;
            }
            // S'il y a un fichier en sortie.
            if (cmd->out != NULL) {
                // If the file does not exist, it is created with all privileges
                fd_out = open(cmd->out, O_WRONLY | O_CREAT, S_IRWXU);
                if (fd_out == -1) {
                    return -1;
                }
                dup2(fd_out, 1);
                if (close(fd_out)) return -1;
            }

            // Le fils execute ce code
            if ((execvp(cmd->seq[0][0],cmd->seq[0])) == -1) {
                perror("error when ecec of chld");
                return -1;
            }
            break;
            // En cas d'erreur, on retourne -1.
        case -1:
            perror("error when fork creation");
            return -1;
            break;
        default:
            // Le père execute ce code
            gettimeofday(global_time,NULL);

            // Si & a été écrit, le shell s'affiche directement
            if (cmd->bg) {
                add_jobs(pid, cpyLine);
            } 
            else {
                waitpid(pid, &status, 0);
            }
            break;
    }
    free(cpyLine);
    return 0;
} 

// Cas d'execution dans le cas avec pipes.
int exec_pipe_cmd(struct cmdline *cmd, char *cpyLine) {

    // pid1 : partie droite
    // pid2 : partie gauche
    pid_t pid1 , pid2;
    int status1, status2;
    int pipefd[2];

    if(pipe(pipefd)){
        perror("error in the pipe creation");
        return -1;
    }

    // Creation du fils pour faire la partie droite du pipe
    switch( pid1=fork() ) {
        case -1:
            //Cas d'erreur du fork
            return -1;
            break;
        case 0:
            // Le premier fils execute ce code (partie droite)
            // On crée une copie de pipefd[0] qui est STDIN
            // et qui correspond maintenant aussi à l'entrée du pipe.
            // On peut supprimer pipefd[0] car c'est STDIN le descripteur maintenant,
            // pipefd[1] ne sert à rien car on utilise la sortie normale
            // de grep.
            dup2(pipefd[0],0);
            //On ferme le descriteur de fichier en ecriture et en lecture.
            if (close(pipefd[0])) return -1;
            if (close(pipefd[1])) return -1;
            // Et on exec la partie gauche du pipe
            if ((execvp(cmd->seq[1][0],cmd->seq[1])) == -1){
                // Cas d'erreur de l'exec: retourne -1
                perror("error in the exec of the children 1");
                return -1;
            }
            // Le processus va s'executer jusqu'à qu'il n'y ait plus rien
            // en entrée (deuxième fils) et que le pipe soit fermé partout.
            break;
        default:
            // Code executé par le père principal
            switch ( pid2 = fork() ) {
                case -1:
                    // Erreur du fork()
                    return -1;
                    break;
                case 0:
                    // Le deuxième fils execute ce code (partie gauche du pipe)
                    // On crée une copie de pipefd[1] avec STDOUT comme
                    // descripteur.
                    dup2(pipefd[1], 1);
                    // On peut alors supprimer l'entrée du pipe :
                    // même qu'avant et la sortie car on a fait une copie.
                    if (close(pipefd[0])) return -1;
                    if (close(pipefd[1])) return -1;
                    // On execute la partie gauche du pipe.
                    if ((execvp(cmd->seq[0][0],cmd->seq[0])) == -1){
                        // Cas d'erreur de l'exec: retourne -1
                        perror("error in the exec of the children 1");
                        return -1;
                    }
                    break;
                    // Le processus s'execute et produit sa sortie sur
                    // STDOUT lue par le premier fils qui s'execute en
                    // même temps.
                default:
                    // Le père principal execute ce code.
                    // Ces descripteurs du pipe sont toujours ouvert
                    // à ce point !!! On doit les fermer car ils sont inutiles
                    // pour le père et permettent aux processus de se terminer.
                    if (close(pipefd[0])) return -1;
                    if (close(pipefd[1])) return -1;
                    if (cmd->bg) {
                        add_jobs(pid2, cpyLine);
                        add_jobs(pid1, cpyLine);
                    }
                    else {
                        // On attend d'abord que la partie gauche soit finie.
                        waitpid(pid2, &status1, 0);
                        waitpid(pid1, &status2, 0);
                    }
                    break;
            }
            break;
    }
    free(cpyLine);
    return 0;   
}

int executer(char *line)
{
    int i = 0, j = 0;
    char * cpyLine = malloc(sizeof(char) * (strlen(line) + 1));
    strcpy(cpyLine, line);
    struct cmdline *cmd = 0;

    // Parse cmd and free line
    cmd = parsecmd(&line);

    // If input stream is closed
    if (!cmd) {
        terminate(0);
    }

    if (cmd->err) {
        /* Syntax error, read another command */
        printf("error: %s\n", cmd->err);
        return 0;
    }

    if (cmd->in) printf("in: %s\n", cmd->in);
    if (cmd->out) printf("out: %s\n", cmd->out);
    if (cmd->bg) printf("background (&)\n");

    /* Display each command of the pipe */
    for (i=0; cmd->seq[i]!=0; i++) {
        char **cmds = cmd->seq[i];
        printf("seq[%d]: ", i);
        for (j=0; cmds[j]!=0; j++) {
            printf("'%s' ", cmds[j]);
        }
        printf("\n");
    }

    if (!strcmp(cmd->seq[0][0],"jobs")) {
        print_jobs();
        return 0;
    }
    // Cas sans pipe 
    if (cmd->seq[1]==0) {
        return exec_simple_cmd(cmd,cpyLine);
    } 
    // Cas avec pipe
    else {
        //free(cpyLine);
        return exec_pipe_cmd(cmd, cpyLine);
    }
}

void print_time(int signal) 
{   
    long sec = 0;
    long usec = 0;
    long global_t = 0;
    long diff_t = 0;
    long now_t = 0;
    struct timeval * now_time=NULL;
    now_time = (struct timeval*)malloc(sizeof(*now_time));
    now_time->tv_sec = 0;
    now_time->tv_usec = 0;
    gettimeofday(now_time,NULL);
    global_t = (long)( (global_time->tv_sec)*100000 + global_time->tv_usec);
    now_t = (long)( (now_time->tv_sec)*100000 + now_time->tv_usec);
    diff_t = (long)(now_t - global_t);
    sec = (long)(diff_t / 100000);
    usec = (long)(  (long)diff_t - ((long)sec)*100000  ) ;
    
    printf("\nTemps de l'execution : %ld sec et %ld usec\n",sec,usec);
    free(now_time); 
}

SCM executer_wrapper(SCM x)
{
    return scm_from_int(executer(scm_to_locale_stringn(x, 0)));
}
#endif

void terminate(char *line) {
#ifdef USE_GNU_READLINE
    /* rl_clear_history() does not exist yet in centOS 6 */
    clear_history();
#endif
    if (line)
        free(line);
    if(global_time)
        free(global_time);
    printf("exit\n");
    exit(0);
}

int main() {
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#ifdef USE_GUILE
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    /*
     * Init of the sigaction
    */
    global_time= (struct timeval*)malloc(sizeof(*global_time));
    global_time->tv_sec = 0;
    global_time->tv_usec = 0;
    gettimeofday(global_time,NULL);
    struct sigaction sig_traitant = {};
    sig_traitant.sa_handler = print_time;
    sigemptyset(&sig_traitant.sa_mask);
    // SA_NOCLDWAIT = si le signal est SIGCHLD, ses fils qui se terminent ne deviennent pas zombis
    // SA_RESTART = Les appels systeme interrompus par un signal capté sont relancés au lieu de renvoyer -1
    sig_traitant.sa_flags = 0;// SA_NOCLDWAIT | SA_RESTART;
    if( sigaction(SIGCHLD,&sig_traitant,0) == -1) {
        perror("Sigaction error");
    }


    char *prompt = "ensishell>";

    while (1) {
        char *line=0;
        /* Readline use some internal memory structure that
         *         can not be cleaned at the end of the program. Thus
         *                 one memory leak per command seems unavoidable yet */
        line = readline(prompt);
        if (line == 0 || ! strncmp(line,"exit", 4) ) {
            terminate(line);
        }
        if (!strncmp(line,"\0",1)) {
            continue;
        }

#ifdef USE_GNU_READLINE
        add_history(line);
#endif


#ifdef USE_GUILE
        /* The line is a scheme command */
        if (line[0] == '(') {
            char catchligne[strlen(line) + 256];
            sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
            scm_eval_string(scm_from_locale_string(catchligne));
            free(line);
            continue;
        }
#endif
        // free the finished jobs
        free_jobs();

        switch (executer(line))
        {
            case 0:
                continue;
                break;
            case -1:
                perror("error: ");
                break;
            default:
                break;
        } 
    }
    free(global_time);
    return 0;
}
