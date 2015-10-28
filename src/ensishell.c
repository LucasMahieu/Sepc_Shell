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
void add_jobs(pid_t *pidj, char * seql, int nb_seq)
{
    int i = 0;
    jobs * toAdd = NULL;
    toAdd = (jobs*) malloc(sizeof(*toAdd));

    toAdd->nb = nb_seq;
    
    // Temps actuel.
    toAdd->begin = (struct timeval*) malloc(sizeof(*(toAdd->begin)));
    (toAdd->begin)->tv_sec = 0;
    (toAdd->begin)->tv_usec = 0;
    gettimeofday(toAdd->begin,NULL);

    // Allocation dynamique du nom de la commande.
    toAdd->jseq = (char*) malloc(sizeof(char) * (strlen(seql) + 1));
    strcpy(toAdd->jseq,seql);

    // Allocation dynamique du tableau de pid.
    toAdd->pid_number = (pid_t*) malloc(sizeof(pid_t) * nb_seq);
    for(i = 0; i < nb_seq; i++) {
        (toAdd->pid_number)[i] = pidj[i];
    }

    if (jlist == NULL)
    {
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
    int i = 0;

    for(tmp = jlist; tmp != NULL; tmp = tmp -> next)
    {
        printf("PID(s) : ");
        for(i = 0; i < tmp->nb - 1; i++) {
            printf("%d, ", (tmp->pid_number)[i]);
        }
        printf("%d.\n", (tmp->pid_number)[tmp->nb - 1]);
        printf("CMD : %s \n", tmp->jseq);
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
            // Si & a été écrit, cmd shell s'affiche directement
            if (cmd->bg) {
                add_jobs(&pid, cpyLine, 1);
            } 
            else {
                waitpid(pid, &status, 0);
            }
            break;
    }
    free(cpyLine);
    return 0;
} 

/*
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
*/
void close_all_fd(int** fd,int nb_cmd){
    int k=0;
    for(k=0;k<nb_cmd;k++){
        close(fd[k][0]);
        close(fd[k][1]);
    }
}
// Retourne le nombre de séquences.
int get_nb_seq( struct cmdline* cmd ) 
{
    int i =0;
    while(cmd->seq[i] != 0) {
        i++;
    }
    return i;
}

// Cas d'execution dans le cas de plusieures pipes.
int exec_multi_pipe(struct cmdline *cmd, char *cpyLine) 
{
    // Nombre de séquences.
    int nb_seq = get_nb_seq(cmd);
    // Nombre de pipes.
    int nb_pipe = nb_seq - 1;
    int i = 0;
    int j = 0;

    // Tableau de PID pour chacun des processus crées.
    pid_t *pid = NULL;
    if ((pid = (pid_t*) malloc(nb_seq * sizeof(*pid))) == NULL) return -1;

    // Tableau de status pour chaque processus.
    int *status = NULL;
    if ((status = (int*) malloc(nb_seq * sizeof(*status))) == NULL) return -1;

    // Tableau des pipes à deux dimensions alloué dynamiquement :
    // fd[i][j] où 0 < i < nb_pipe - 1
    //             0 < j < 2
    int **fd;
    fd = calloc(nb_pipe, sizeof(*fd));
    *fd = calloc(2 * nb_pipe, sizeof(**fd));
    for (i = 1; i < nb_pipe; i++) fd[i] = fd[i-1] + 2;

    // On crée tous les pipes.
    for (i = 0; i < nb_pipe; i++) {
        if (pipe(fd[i])) {
            perror("pipe error");
            return -1;
        }
    }

    // Pour toutes les séquences en partant de la dernière.
    for (i= nb_seq - 1; i >= 0; i--) {
        // On fork chaque séquence à partir du père principal.
        switch(pid[i] = fork()) {
            case -1:
                perror("fork error");
                return -1;
                break;
            case 0:
                // Le fils i execute ce code.
                // si c'est le dernier
                if (i == nb_seq - 1) {
                    dup2(fd[nb_pipe - 1][0], 0);

                    // Si il y a un fichier en sortie.
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

                    for(j = 0; j < nb_pipe; j++) {
                        if (close(fd[j][0])) return -1;
                        if (close(fd[j][1])) return -1;
                    }
                }
                // Si c'est le premier.
                else if (i == 0) {
                    dup2(fd[0][1], 1);

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

                    for(j = 0; j < nb_pipe; j++) {
                        if (close(fd[j][0])) return -1;
                        if (close(fd[j][1])) return -1;
                    }
                }
                // Pour tous les autres.
                else {
                    // On écrit dans le pipe précedent (à droite)
                    // et on le ferme.
                    // A droite du processus i : pipe i.
                    // A gauche du processus i : pipe i-1.
                    dup2(fd[i][1], 1);
                    dup2(fd[i-1][0], 0);
                    for(j = 0; j < nb_pipe; j++) {
                        if (close(fd[j][0])) return -1;
                        if (close(fd[j][1])) return -1;
                    }
                }
                // Et on exec la partie droite du pipe
                if ((execvp(cmd->seq[i][0],cmd->seq[i])) == -1){
                    perror("error in the exec of the children ");
                    return -1;
                }
                break;
            default:
                // On passe à la descendance tous les pipes !
                // On ne les fermera tous dans le père principal qu'à la fin.
                break;
        }
    } 
    // On ferme tous les pipes du père.
    for(j = 0; j < nb_pipe; j++) {
        if (close(fd[j][0])) return -1;
        if (close(fd[j][1])) return -1;
    }
    // On attend tous les processus en commençant par le premier
    // qui écrit.
    if (cmd->bg) {
        add_jobs(pid, cpyLine, nb_seq);
    }
    else {
        for(j = 0; j < nb_seq; j++) {
            waitpid(pid[j], status + j, 0);
        }
    }
    free(pid);
    free(status);
    free(*fd);
    free(fd);
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
    int i = 0;
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
        // On actualise le champ status pour chaque dernier processus
        // de chaque commande de la jlist pour savoir s'il est terminé.
        waitpid((p->pid_number)[p->nb - 1],&status,WNOHANG);

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
            printf("\nCMD:'%s' with PID(s)=", p->jseq);
            for( i = 0; i < p->nb - 1; i++) {
                printf(" %d,", (p->pid_number)[i]);
            }
            printf(" %d is over.\n", (p->pid_number)[p->nb - 1]);
            printf("Duration: %ld.%06ld s\n", sec, usec);

            /*---------------------------*
             * Suppression de la cellule *
             *---------------------------*/

            if (p == jlist) {
                jlist = p->next;
                free(p->jseq);
                free(p->begin);
                free(p->pid_number);
                free(p);
            } 
            else {
                p_prev->next = p->next;
                free(p->jseq);
                free(p->begin);
                free(p->pid_number);
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
        free(cpyLine);
        terminate(0);
    }

    if (cmd->err) {
        /* Syntax error, read another command */
        printf("error: %s\n", cmd->err);
        free(cpyLine);
        return 0;
    }

    // Décommenter pour avoir des infos sur la ligne.
    //print_cmd(cmd);
    
    if (!strcmp(cmd->seq[0][0],"jobs")) {
        print_jobs();
        free(cpyLine);
        return 0;
    }
    // Cas sans pipe 
    if (cmd->seq[1]==0) {
        return exec_simple_cmd(cmd,cpyLine);
    } 
    // Cas avec pipe
    else {
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

int main(int argc, char *argv[]) 
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
    if(argv[1] != NULL) {
        strcat(argv[1]," >");
    }
    char *prompt = (argv[1] == NULL)?"root@pcserveur.ensimag.fr:~#":argv[1];

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
    return 0;
}
