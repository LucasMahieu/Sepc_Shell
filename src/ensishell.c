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

#include "variante.h"
#include "readcmd.h"


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

int executer(char *line)
{
    struct cmdline *cmdl;
    int status; 
    pid_t pid = 0;
    int ret=0;
    char path[50];

    cmdl = parsecmd(&line);
//    if (!cmdl) terminate(0);

    if (cmdl->err) {
        /* Syntax error, read another command */
        printf("error: %s\n", cmdl->err);
        return -1;
    }

    switch(pid=fork()){
        case -1:
            perror("fork failed:");
            break;
        case 0:
            printf("Je suis le fils qui va faire la cmd\n");
            strcpy(path,"/bin/");
            strcat(path,*cmdl->seq[0]);
            ret = execvp(path,cmdl->seq[0]);
            if (ret == -1) perror("ls failed:");
            break;
        default:
            printf("Je suis le père et j'attends que mon fils finisse\n");
            waitpid(pid,&status,0);
            break;
    }
return 0;
}
 /*   int i=0;
    char *cmd[50];
    for(i=0;line[i]!=0;i++){
        strcpy(cmd[i],line[i]);
    }
    cmd[i]=NULL;
    pid_t pid=0;
//    if(strcmp("ls",cmd)==0){
       int status; 
        switch(pid=fork()){
            case -1:
                perror("fork failed:");
            break;
            case 0:
                printf("Je suis le fils qui va faire la cmd\n");
                char path[]="/bin/";
                strcat(cmd[0],path);
                int ret = execvp(cmd[0],cmd);
                if (ret == -1) perror("ls failed:");
            break;
            default:
                printf("Je suis le père et j'attends que mon fils finisse\n");
                waitpid(pid,&status,0);
            break;
        }
    return 0;
//    }
//    else return 0; 
    


    


	// * Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */
//	printf("Not implemented: can not execute %s\n", line);

	/* Remove this line when using parsecmd as it will free it */
	//free(line);
	
//	return 0;
//}

SCM executer_wrapper(SCM x)
{
    return scm_from_int(executer(scm_to_locale_stringn(x,0)));
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
        executer(line);
    }

}


