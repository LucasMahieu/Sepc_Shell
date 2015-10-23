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
    //int status = 0;
    jobs * tmp = NULL;
    for(tmp = jlist; tmp != NULL; tmp = tmp -> next)
    {
        printf("pid : %d | command was : %s", tmp->pid_number, tmp->jseq);
        printf("\n");
    }
}

int executer(char *line)
{
    int i = 0, j = 0;
    int status;
    pid_t pid;
    char * cpyLine = malloc(sizeof(char) * (strlen(line) + 1));
    strcpy(cpyLine, line);
    struct cmdline *cmd = 0;
    // Parse cmd and free line
    cmd = parsecmd(&line);

    // If input stream is closed
    if (!cmd)
    {
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

    if (!strcmp(cmd->seq[0][0],"jobs"))
    {
        print_jobs();
        return 0;
    }

    // On fork pour créer un nouveau processus.
    switch(pid = fork())
    {
        case 0:
            // Le fils execute ce code
            if ((execvp(cmd->seq[0][0],cmd->seq[0])) == -1)
            {
                return -1;
            }
            break;
        // En cas d'erreur, on retourne -1.
        case -1:
            return -1;
            break;
        default:
            // Le père execute ce code
            // Si & a été écrit, le shell s'affiche directement
            if (cmd->bg) 
            {
                add_jobs(pid, cpyLine);
            }
            else
            {
                waitpid(pid, &status, 0);
            }
            break;
    }

    free(cpyLine);
//Je pense que c'est ici qu'il faudra retirer le jobs de la liste : 
/*
            if (cmd->bg) 
            {
                delete_jobs(pid, cpyLine);
            }
 */
    return 0;
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
        if (line == 0 || ! strncmp(line,"exit", 4)) {
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

}
