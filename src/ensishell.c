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
#include <sys/wait.h>
#include <sys/types.h>
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
/*
 *Je pense que tu dois creer une fonction delete_jobs ici pour que dans la fonction executer on puisse supprimer le job une fois qu'il sera terminer 
 *
 */
void print_jobs()
{
    int status = 1;
    jobs * tmp = NULL;
    for(tmp = jlist; tmp != NULL; tmp = tmp -> next)
    {
        // WNOHANG so that we don't wait but only check status.
        waitpid(tmp->pid_number, &status, WNOHANG);

        printf("pid : %d | command was : %s ", tmp->pid_number, tmp->jseq);
        //if (WIFEXITED(status))
        if (!status)
        {
            printf("| processus is over.");
            tmp->end = 1;
        }
        else
        {
            printf("| processus is active.");
            tmp->end = 0;
        }
        printf("\n");
    }
    free_jobs();
}

void free_jobs()
{
    jobs * tmp = NULL;
    jobs * tmp_prev = NULL;
    if (jlist == NULL)
    {
        return;
    }
    tmp_prev = jlist;
    for(tmp = jlist; tmp != NULL; tmp = tmp->next)
    {
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


int exec_simple_cmd(struct cmdline *cmd,char *cpyLine) {
pid_t pid;
int status;
    switch(pid = fork()) {
            case 0:
                // Le fils execute ce code
                if ((execvp(cmd->seq[0][0],cmd->seq[0])) == -1)
                {
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
                // Si & a été écrit, le shell s'affiche directement
                if (cmd->bg) {
                    add_jobs(pid,cpyLine);
                } else{
                    waitpid(pid, &status, 0);
                }
                break;
        }
        free(cpyLine);
        return 0;
    } 

int exec_pipe_cmd(struct cmdline *cmd) {
    pid_t pid1 , pid2;
    int status1, status2;
    int pid_wait;
    int pipefd[2];

        if(pipe(pipefd)){
            perror("error in the pipe creation");
            return -1;
        }
        // Creation du FIlS1 pour faire la partie droite du pipe
        switch( pid1=fork() ) {
            case -1:
                //Case d'erreur du fork
                return -1;
                break;
            case 0:
                // Le fils execute ce code
                //On dit au fils qu'il va devoir lire son entrée par le pipe et plus sur le STDIN
                dup2(pipefd[0],0);
                //On ferme le descriteur de fichier en ecriture et en lecture 
                if (close(pipefd[0])) return -1;
                if (close(pipefd[1])) return -1;
                // Et on exec la partie gauche du pipe
                if ((execvp(cmd->seq[1][0],cmd->seq[1])) == -1){
                    // Cas d'erreur de l'exec: retourne -1
                    perror("error in the exec of the children 1");
                    return -1;
                }
                break;
            default:
                // Le père execute ce code
                // Creation du second processus pour la partie gauche du pipe
                if((pid2=fork()) == 0){
                    //Le Fils 2 exec ce code
                    // On dit au Fils2 qu'il ne va plus ecrir sur le STDOUT mais dans le pipe
                    dup2(pipefd[1],1);
                    //On ferme les acces au pipe 
                    if (close(pipefd[1])) return -1;
                    if (close(pipefd[0])) return -1;
                    //Et on execute la partie droite du pipe
                    if ((execvp(cmd->seq[0][0],cmd->seq[0])) == -1){
                        // Cas d'erreur de l'exec: retourne -1
                        perror("error in the exec of the children 2");
                        return -1;
                    }
                } else if(pid2 == -1){
                        perror("error in the fork of the children 2");
                        return -1;
                }
                else{
                    //Le Pere exec ceci 
                }
                pid_wait = waitpid(pid2,&status2,0);
                printf("pere: %d ---  pid1 = %d --- pid2 = %d \n",getpid(), pid1, pid2);
                printf("PID du proc qui se termine = %d avec status = %d \n",pid_wait, status2);
                pid_wait = waitpid(pid1,&status1,WNOWAIT);
                printf("PID du proc qui se termine = %d avec status = %d \n",pid_wait,status1);
                break;
        }
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
        free(cpyLine);
        return exec_pipe_cmd(cmd);
    }
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

    while (1) {
        char *line=0;
        char *prompt = "ensishell>";

        /* Readline use some internal memory structure that
         *         can not be cleaned at the end of the program. Thus
         *                 one memory leak per command seems unavoidable yet */
        line = readline(prompt);
        if (line == 0 || ! strncmp(line,"exit", 4) || !strncmp(line,"\0",1) ) {
            terminate(line);
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
