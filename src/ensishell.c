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

// Add new jobs to the global jobs list filling each
// usefull information.
void add_jobs(pid_t pidj, char * seql)
{
    jobs * toAdd = NULL;
    toAdd = (jobs*) malloc(sizeof(*toAdd));
    
    toAdd->begin = (struct timeval*) malloc(sizeof(*(toAdd->begin)));
    (toAdd->begin)->tv_sec = 0;
    (toAdd->begin)->tv_usec = 0;
    gettimeofday(toAdd->begin,NULL);

    toAdd->pid_number = pidj;
    toAdd->jseq = (char*)malloc(sizeof(char) * (strlen(seql) + 1));
    strcpy(toAdd->jseq,seql);

    if (jlist == NULL) {
        jlist = toAdd;
        toAdd->next = NULL;
    }
    else {
        toAdd->next = jlist;
        jlist = toAdd;
    }
}

// Print the current job list.
void print_jobs()
{
    jobs * tmp = NULL;
    for(tmp = jlist; tmp != NULL; tmp = tmp -> next) {
        printf("pid : %d | command was : %s \n", tmp->pid_number, tmp->jseq);
    }
}

// Cas d'execution dans le cas sans pipe.
int exec_simple_cmd(struct cmdline *cmd,char *cpyLine) 
{

    pid_t pid;
    int status;

    
    switch(pid = fork()) {
        case 0:
            // S'il y a un fichier en entrée.
            if (cmd->in != NULL) {
                // Descripteur pour le fichier eventuel en entrée
                int fd_in;
                fd_in = open(cmd->in, O_RDONLY);
                if (fd_in == -1) {
                    return -1;
                }
                dup2(fd_in, 0);
                if (close(fd_in)) return -1;
            }
            // S'il y a un fichier en sortie.
            if (cmd->out != NULL) {
                // Descripteur pour le fichier eventuel en sortie
                int fd_out;
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
            // Si & a été écrit, cmde shell s'affiche directement
            if (cmd->bg) {
                //gettimeofday(global_time,NULL);
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

// Cas d'execution dans le cas avec une pipe.
int exec_pipe_cmd(struct cmdline *cmd, char *cpyLine) 
{
    // pid1 : partie droite
    // pid2 : partie gauche
    pid_t pid1 , pid2;
    int status1, status2;
    int pipefd[2];

    if(pipe(pipefd)) {
        perror("error in the pipe creation");
        return -1;
    }

    // Creation du fils pour faire la partie droite du pipe
    switch( pid1=fork() ) {
        case -1:
            //Cas d'erreur du fork
            perror("1st fork error");
            return -1;
            break;
        case 0:
            // Le premier fils execute ce code (partie droite)

            // S'il y a un fichier en sortie.
            if (cmd->out != NULL) {
                // Descripteur pour le fichier eventuel en sortie
                int fd_out;
                // If the file does not exist, it is created with all privileges
                fd_out = open(cmd->out, O_WRONLY | O_CREAT, S_IRWXU);
                if (fd_out == -1) {
                    return -1;
                }
                dup2(fd_out, 1);
                if (close(fd_out)) return -1;
            }
            else {
            dup2(pipefd[0],0);
            //On ferme le descriteur de fichier en ecriture et en lecture.
            if (close(pipefd[0])) return -1;
            if (close(pipefd[1])) return -1;
            }

            // Et on exec la partie droite du pipe
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
                    perror("2nd fork error");
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

                    // S'il y a un fichier en entrée.
                    if (cmd->in != NULL) {
                        // Descripteur pour le fichier eventuel en entrée
                        int fd_in;
                        fd_in = open(cmd->in, O_RDONLY);
                        if (fd_in == -1) {
                            return -1;
                        }
                        dup2(fd_in, 0);
                        if (close(fd_in)) return -1;
                    }

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
                        //add_jobs(pid2, cpyLine);
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

void close_all_fd(int** fd,int nb_cmd){
    int k=0;
    for(k=0;k<nb_cmd;k++){
        close(fd[k][0]);
        close(fd[k][1]);
    }
}

// Cas d'execution dans le cas de plusieures pipes.
int exec_multi_pipe(struct cmdline *cmd, char *cpyLine) 
{
    int nb_cmd=0;
    for(nb_cmd=0;cmd->seq[nb_cmd] != 0; nb_cmd ++){}
    pid_t* pid=NULL;
    if( (pid = (pid_t*)malloc(nb_cmd*sizeof(*pid))) == NULL ) return -1;
    int* status = NULL;
    if ( (status = (int*)malloc(nb_cmd*sizeof(*status))) == NULL ) return -1;
    int i=0;
    int** fd = NULL;

    if ( (fd = (int**)malloc((nb_cmd)*sizeof(*fd))) == NULL ) return -1;
    for (int j=0;j<nb_cmd;j++){
        if ( (fd[j] = (int*)malloc(2*sizeof(*fd[j]))) == NULL ) return -1;
    }
    for(int k=0;k<nb_cmd;k++){
        pipe(fd[k]);
    }

    for (  i=nb_cmd-1; i>=0 ; i-- ) {
        switch( pid[i]=fork() ) {
            case -1:
                perror("fork error");
                return -1;
                break;
            case 0:
                // Le fils i execute ce code (partie droite)
                if (i>0) {
                    dup2(fd[i][0],0);
//                    if(i==(nb_cmd-1)) {
//                        close_all_fd(fd,nb_cmd);
//                    }
                }
                if (i<nb_cmd-1) {
                    dup2(fd[i+1][1],1);
//                    close_all_fd(fd,nb_cmd);
                }
                // S'il y a un fichier en sortie du dernier pipe.
                if ( (i==(nb_cmd-1))&&(cmd->out != NULL) ) {
                    int fd_out;
                    // If the file does not exist, it is created with all privileges
                    fd_out = open(cmd->out, O_WRONLY | O_CREAT, S_IRWXU);
                    if (fd_out == -1) return -1;
                    dup2(fd_out, 1);
                    if (close(fd_out)) return -1;
                }
                // S'il y a un fichier en entré du 1er pipe
                if ( (i==0) && ((cmd->in)!=NULL) ) {
                        int fd_in;
                        if ( (fd_in=open(cmd->in, O_RDONLY)) ) return -1;
                        dup2(fd_in, 0);
                        if (close(fd_in)) return -1;
                }

                close_all_fd(fd,nb_cmd);
//                printf("fonct: %s\nPID: %d\nfd[0]: %d\nfd[1]: %d\nfd_prev[0]: %d\nfd_prev[1]: %d\n ",*cmd->seq[i],pid[i],fd[0],fd[1],fd_prev[0],fd_prev[1]);
                // Et on exec la partie droite du pipe
                if ((execvp(cmd->seq[i][0],cmd->seq[i])) == -1){
                    perror("error in the exec of the children ");
                    return -1;
                }
                // Le processus va s'executer jusqu'à qu'il n'y ait plus rien
                // en entrée (fils fils i-1) et que le pipe soit fermé partout.
                break;
            default:
                break;
        }
    }
    close_all_fd(fd,nb_cmd);
    for ( i=nb_cmd-1 ; i>=0 ; i--){
        waitpid(pid[i],&(status[i]),0);
    }
    for(int k=0;k<nb_cmd;k++){
        free(fd[k]);
    }
    free(fd); 
    free(pid);
    free(status);
    free(cpyLine);
    return 0;
}

// Affiche des informations sur la séquence rentrée.
void  print_cmd(struct cmdline * cmd)
{
    if (cmd->in) printf("in: %s\n", cmd->in);
    if (cmd->out) printf("out: %s\n", cmd->out);
    if (cmd->bg) printf("background (&)\n");

    /* Display each command of the pipe */
    for (int i=0; cmd->seq[i]!=0; i++) {
        char **cmds = cmd->seq[i];
        printf("seq[%d]: ", i);
        for (int j=0; cmds[j]!=0; j++) {
            printf("'%s' ", cmds[j]);
        }
        printf("\n");
    }

}

// Traitant d'interruption quand un processus fils se termine.
void print_time(int signal) 
{   
    // Uptime du processus : sec + usec
    long sec = 0;
    long usec = 0;

    // begin_t : date à laquelle le processus a démarré.
    // now_t : date actuelle (fin du processus).
    // diff_t : différence de ces deux dates : uptime.
    // Tout est en us.
    long begin_t = 0;
    long diff_t = 0;
    long now_t = 0;

    // Structure timeval pour récupérer le temps actuel.
    struct timeval * now_time = NULL;
    now_time = (struct timeval*) malloc(sizeof(*now_time));
    now_time->tv_sec = 0;
    now_time->tv_usec = 0;
    
    jobs *p = jlist;
    jobs *p_prev = p;
    int status = 1;

    /*-----------------------------------------------------------*
     *  Recherche du jobs qui a fini, et suppresion de la jlist  *
     *-----------------------------------------------------------*/

    if (jlist == NULL) return;
    for (p = jlist; p != NULL; p = p->next) {
        // On actualise le champ status pour chaque processus
        // de la jlist pour savoir s'il est terminé.
        waitpid(p->pid_number,&status,WNOHANG);

        // S'il est terminé :
        //  - on affiche son temps d'execution en informant l'utilisateur ;
        //  - on l'enlève de la jlist.
        if(!status) {
            p->end = 1;
            status = 1;

            /*------------------------------*
             *  Calcul du temps d'execution *
             *------------------------------*/

            gettimeofday(now_time,NULL);
            begin_t = (long) (p->begin->tv_sec * 1000000 + p->begin->tv_usec);
            now_t = (long) (now_time->tv_sec * 1000000 + now_time->tv_usec);
            diff_t = (long) (now_t - begin_t);
            sec = (long) (diff_t / 1000000);
            usec = (long) (diff_t % 1000000);
            printf("\nCMD:'%s' with PID=%d is over.\n", p->jseq, p->pid_number);
            printf("Duration: %ld.%06ld s\n", sec, usec);

            /*---------------------------*
             * Suppression de la cellule *
             *---------------------------*/

            if (p == jlist) {
                jlist = p->next;
                free(p->jseq);
                free(p->begin);
                free(p);
            } 
            else {
                p_prev->next = p->next;
                free(p->jseq);
                free(p->begin);
                free(p);
            }
        }

        // S'il n'est pas terminé, on ne fait rien :)
        else {
            status = 1;
            p->end = 0;
        }

        p_prev = p;
    }
    free(now_time); 
}

// Execution normale
int executer(char *line)
{
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
    
    print_cmd(cmd);
    
    if (!strcmp(cmd->seq[0][0],"jobs")) {
        print_jobs();
        return 0;
    }
    else{
        return exec_multi_pipe(cmd, cpyLine);
    }
}

SCM executer_wrapper(SCM x)
{
    return scm_from_int(executer(scm_to_locale_stringn(x, 0)));
}
#endif

// Terminate in case of problem.
void terminate(char *line)
{
#ifdef USE_GNU_READLINE
    /* rl_clear_history() does not exist yet in centOS 6 */
    clear_history();
#endif
    if (line)
        free(line);
    printf("exit\n");
    exit(0);
}

int main() 
{
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#ifdef USE_GUILE
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    /*********************************************
     * Initialisation du traitant d'interruption *
     *********************************************/

    struct sigaction sig_traitant = {};
    sig_traitant.sa_handler = print_time;
    // Permet d'exlure tous les signaux du set.
    sigemptyset(&(sig_traitant.sa_mask));
    sig_traitant.sa_flags = 0;
    if( sigaction(SIGCHLD, &sig_traitant, NULL) == -1) {
        perror("Sigaction error");
    }

    char *prompt = "coquille>";

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
    //free(global_time);
    return 0;
}
