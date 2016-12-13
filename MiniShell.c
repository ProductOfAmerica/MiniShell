/*
 * Inspiration used from:
 * https://github.com/ajay154/My-Mini-Shell/blob/master/myshell.c*
 * Including: Some macros, changing directories, printing jobs, waiting jobs
 * list (jobList) inserting a job into the job list, freeing jobs, and
 * redirection.
 *
 * Multiple sources of online C libraries/functions were used in this project as
 * well. All are cited below.
 *
 * Help from Clint Staley.
 * Help with "source" command. Help with dup2 and redirection.
 *
 * Also help from Nick Flanders.
 * Including: Command struct help (redirStr), job wait bool (shouldWait),
 * waiting on a job, and other small pieces
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "SmartAlloc.h"

/* Max length of commands, arguments, filenames */
#define MAX_WORD_LEN 100
#define WORD_FMT "%100s"
#define REDIR_MF 0x1000000 //1000000000000000000000000
#define OFF_M 0xFFFFFF     //0111111111111111111111111
#define TRUE 1
#define FALSE 0

/* One argument in a commandline (or the command itself) */
typedef struct Arg {
    char value[MAX_WORD_LEN + 1];
    struct Arg *next;
} Arg;

/* One full command: executable, arguments and any file redirections. */
/**
 * Output File Flags
 * OFFs Bits:   0000 000|0|   |0000 0000  0000 0000  0000 0000|
 *             Redir Flag^     -------Output File Flags-------
 */
typedef struct Command {
    Arg *args;
    int argCount;
    char inFile[MAX_WORD_LEN + 1];
    char outFile[MAX_WORD_LEN + 1];
    char redirStr[MAX_WORD_LEN + 1];
    int childPID; //Keep track of PID for listing
    int OFFs;
    struct Command *next;
} Command;

typedef struct Job {
    Command *cmds;
    int totalCmds, shouldWait;
    struct Job *next;
} Job;

/* Make a new Arg, containing "str" */
static Arg *NewArg(char *str) {
   Arg *rtn = malloc(sizeof(Arg));

   strncpy(rtn->value, str, MAX_WORD_LEN);
   rtn->next = NULL;

   return rtn;
}

/* Make a new Command, with just the executable "cmd" */
static Command *NewCommand(char *cmd) {
   Command *rtn = malloc(sizeof(Command));

   rtn->argCount = 1;
   rtn->args = NewArg(cmd);

   rtn->inFile[0] = rtn->outFile[0] = rtn->redirStr[0] = '\0';
   rtn->next = NULL;
   rtn->OFFs = (O_WRONLY | O_CREAT);

   return rtn;
}

/* Delete "cmd" and all its Args. */
static Command *DeleteCommand(Command *cmd) {
   if (!cmd)
      return NULL;

   Arg *temp;
   Command *rtn = cmd->next;

   while (cmd->args != NULL) {
      temp = cmd->args;
      cmd->args = temp->next;
      free(temp);
   }
   free(cmd);

   return rtn;
}

/**
 * *************************************************************
 * My Functions
 * *************************************************************
 */
static Job *jobList; //Linked list of all waiting jobs (credit: @top)

static Job *ReadJob(FILE *in);

static void RunJob(Job *job);

/* Make a new Job */
static Job *NewJob() {
   Job *rtn = malloc(sizeof(Job));

   rtn->cmds = NULL;
   rtn->totalCmds = 0;
   rtn->shouldWait = TRUE;
   rtn->next = NULL;

   return rtn;
}

/* Delete "job" and all its Commands. */
static Job *DeleteJob(Job *job) {
   if (!job)
      return NULL;

   Command *temp = job->cmds;
   Job *rtn = job->next;

   while ((temp = DeleteCommand(temp))) //While job->cmds != null, delete
      ;

   free(job);
   return rtn;
}

/*Return 1 (true) if strcmp returns 0 (meaning the strings match)*/
static int StrEqual(const char *str1, const char *str2) {
   return !strcmp(str1, str2);
}

static void ChDir(Arg *arg) {
   if (arg && arg->value)
      chdir(arg->value);
   else
      chdir(getenv("HOME"));
}

/*https://linux.die.net/man/3/setenv*/
static void SetEnv(Arg *arg) {
   if (arg && arg->next)
      setenv(arg->value, arg->next->value, TRUE); //Overwrite
}

/*https://goo.gl/yF28S1*/
static void UnsetEnv(Arg *arg) {
   if (arg)
      unsetenv(arg->value);
}

/*http://stackoverflow.com/questions/2082743/c-equivalent-to-fstreams-peek*/
static int PeekChar(FILE *const fp) {
   const int c = getc(fp);
   return c == EOF ? EOF : ungetc(c, fp);
}

static void Source(Arg *arg) {
   if (arg && arg->value) {
      //Open source for reading. Online help -- http://bit.ly/2g4tMR4
      FILE *in = fopen(arg->value, "r");

      if (!in) { //...and this
         fprintf(stderr, "Error in opening for reading. File doesn't exist?\n");
         return;
      }

      while (!feof(in)) { //More help -- http://bit.ly/2fPgpGR
         Job *job;
         if ((job = ReadJob(in)))
            RunJob(job);
      }
   }
}

/* DisplayAllJobs() - Matched this output:
* [1]   Running                 sleep 100 &
* [2]-  Running                 sleep 100 &
* [3]+  Running                 sleep 100 &
*/
static void DisplayAllJobs(Job *listOfJobs) {
   int jobNum = 1;

   while (listOfJobs) {
      printf("[%i]%-3s%s%-17s", jobNum++, "", "Running", "");

      Command *tempCmd = listOfJobs->cmds;
      while (tempCmd) {
         Arg *tempArg = tempCmd->args;

         while (tempArg) {
            printf("%s ", tempArg->value);
            tempArg = tempArg->next;
         }

         if (tempCmd->outFile[0]) { //If outFile is not empty
            printf("%s ", tempCmd->redirStr);
            printf("%s ", tempCmd->outFile);
         }

         if (tempCmd->inFile[0]) {
            printf("< %s ", tempCmd->inFile);
         }

         if (tempCmd->next)
            printf(tempCmd->OFFs & OFF_M ? "|& " : "| ");

         tempCmd = tempCmd->next;
      }

      printf(!listOfJobs->shouldWait ? "&\n" : "\n");

      listOfJobs = listOfJobs->next;
   }
}

static int CheckAllFunctions(char *value, Arg *arg) {
   if (arg && value) {//Might as well do validation now...?
      if (StrEqual(value, "cd")) {
         ChDir(arg);
         return TRUE;
      } else if (StrEqual(value, "setenv")) {
         SetEnv(arg);
         return TRUE;
      } else if (StrEqual(value, "unsetenv")) {
         UnsetEnv(arg);
         return TRUE;
      } else if (StrEqual(value, "source")) {
         Source(arg);
         return TRUE;
      }
   } else if (value && StrEqual(value, "jobs")) {
      DisplayAllJobs(jobList);
      return TRUE;
   }

   return FALSE;
}

static void WaitOnAllChildren(Job *job) {
   if (job->shouldWait) {
      int childPID;
      Command *cmd = job->cmds;

      while (job->totalCmds--) {
         /*Wait for child whose process group ID equals to the calling process*/
         waitpid(cmd->childPID, NULL, WAIT_MYPGRP);
         cmd = cmd->next;
      }

      DeleteJob(job);

      while ((childPID = waitpid(-1, NULL, WNOHANG)) > 0) {
         Job *temp = jobList, *prevJob = NULL;

         while (temp && childPID) {
            cmd = temp->cmds;
            while (cmd) {
               if (childPID == cmd->childPID) {
                  childPID = 0;
                  temp->totalCmds--;
                  break;
               }
               cmd = cmd->next;
            }

            if (!temp->totalCmds) {
               Job *nextJob = DeleteJob(temp);
               if (prevJob)
                  prevJob->next = nextJob;
               else
                  jobList = nextJob;
               break;
            }

            prevJob = temp;
            temp = temp->next;
         }
      }

   } else {
      job->next = jobList; //Append jobList to job's next
      jobList = job; //Set the job as head of jobList
   }
}

static void logger(char *ch) { //This is used for debugging
   fprintf(stdout, "%s\n", ch);
}

/**
 * *************************************************************
 * My Functions
 * *************************************************************
 */

/* Read from "in" a single commandline, comprising one more pipe-connected
   commands.  Return head pointer to the resultant list of Commmands */
static Job *ReadJob(FILE *in) {
   int nextChar;
   char nextWord[MAX_WORD_LEN + 1];
   Command *lastCmd;
   Arg *lastArg;
   Job *job = NewJob();

   /* If there is an executable, create a Command for it, else return NULL. */
   if (1 == fscanf(in, WORD_FMT, nextWord)) {
      job->cmds = lastCmd = NewCommand(nextWord);
      lastArg = lastCmd->args;
   } else
      return NULL;

   /* Repeatedly process the next blank delimited string */
   do {
      while ((nextChar = getc(in)) == ' ')   /* Skip whitespace */
         ;

      /* If the line is not over */
      if (nextChar != '\n' && nextChar != EOF) {

         /* A pipe indicates a new command */
         if (nextChar == '|') {
            if (PeekChar(in) == '&') {
               lastCmd->OFFs |= REDIR_MF; //Set redirect flag
               getc(in); //Eat the character
            }

            if (1 == fscanf(in, WORD_FMT, nextWord)) {
               lastCmd = lastCmd->next = NewCommand(nextWord);
               lastArg = lastCmd->args;
            }
         }
            /* Otherwise, it's either a redirect, or a commandline arg */
         else {
            ungetc(nextChar, in);
            fscanf(in, WORD_FMT, nextWord);

            if (StrEqual(nextWord, "<")) {
               fscanf(in, WORD_FMT, lastCmd->inFile);
            } else if (StrEqual(nextWord, ">")) {
               lastCmd->OFFs |= O_EXCL;
               fscanf(in, WORD_FMT, lastCmd->outFile);
               strcpy(lastCmd->redirStr, nextWord);
            } else if (StrEqual(nextWord, ">!")) {
               lastCmd->OFFs |= O_TRUNC;
               fscanf(in, WORD_FMT, lastCmd->outFile);
               strcpy(lastCmd->redirStr, nextWord);
            } else if (StrEqual(nextWord, ">>")) {
               lastCmd->OFFs |= O_APPEND;
               fscanf(in, WORD_FMT, lastCmd->outFile);
               strcpy(lastCmd->redirStr, nextWord);
            } else if (StrEqual(nextWord, ">&")) {
               lastCmd->OFFs |= (O_EXCL | REDIR_MF); //Set redirect flag too
               fscanf(in, WORD_FMT, lastCmd->outFile);
               strcpy(lastCmd->redirStr, nextWord);
            } else if (StrEqual(nextWord, "&")) {
               job->shouldWait = FALSE;
            } else {
               lastArg = lastArg->next = NewArg(nextWord);
               lastCmd->argCount++;
            }
         }
      }
   } while (nextChar != '\n' && nextChar != EOF);

   return job;
}

static void RunJob(Job *job) {
   Command *cmd;
   char **cmdArgs, **thisArg;
   Arg *arg;
   int pipeFDs[2]; /* Pipe fds for pipe between this command and the next */
   int outFD = -1; /* If not -1, FD of pipe or file to use for stdout */
   int inFD = -1;  /* If not -1, FD of pipe or file to use for stdin */

   for (cmd = job->cmds; cmd != NULL; cmd = cmd->next) {
      if (CheckAllFunctions(cmd->args->value, cmd->args->next))
         return;

      if (inFD < 0 && cmd->inFile[0]) /* If no in-pipe, but input redirect */
         inFD = open(cmd->inFile, O_RDONLY);

      if (cmd->next != NULL)  /* If there's a next command, make an out-pipe */
         pipe(pipeFDs);

      if ((cmd->childPID = fork()) < 0)
         fprintf(stderr, "Error, cannot fork.\n");
      else if (cmd->childPID) {      /* We are parent */
         job->totalCmds++;
         close(inFD);           /* Parent doesn't use inFd; child does */
         if (cmd->next != NULL) {
            close(pipeFDs[1]);  /* Parent doesn't use out-pipe; child does */
            inFD = pipeFDs[0];  /* Next child's inFD will be out-pipe reader */
         }
      } else {                         /* We are child */
         if (inFD >= 0) {            /* If special input fd is set up ...  */
            dup2(inFD, 0);           /*   Move it to fd 0 */
            close(inFD);             /*   Close original fd in favor of fd 0 */
         }

         outFD = -1;                 /* Set up special stdout, if any */
         if (cmd->next != NULL) {    /* if our parent arranged an out-pipe.. */
            outFD = pipeFDs[1];      /*   Save its write fd as our outFD */
            close(pipeFDs[0]);       /*   Close read fd; next cmd will read */
         }

         if (outFD < 0 &&
             cmd->outFile[0]) {  /* If no out-pipe, but a redirect */
            if (-1 == (outFD = open(cmd->outFile, cmd->OFFs & OFF_M, 0644))) {
               fprintf(stderr, "Redirection would overwrite %s\n",
                       cmd->outFile);
               //Error won't be printed to console without this exit
               exit(EXIT_FAILURE);
            }
         }

         /* If above code results in special stdout, dup this to fd 1 */
         if (outFD >= 0) {
            dup2(outFD, STDOUT_FILENO); //replace standard out with outFD
            if (cmd->OFFs & REDIR_MF) //Check if redir flag was set
               dup2(outFD, STDERR_FILENO); //replace standard error with outFD
            close(outFD);
         }

         /* Build a commandline arguments array, and point it to Args content */
         cmdArgs = thisArg = calloc(sizeof(char *), cmd->argCount + 1);
         for (arg = cmd->args; arg != NULL; arg = arg->next)
            *thisArg++ = arg->value;

         /* Exec the command, with given args, and with stdin and stdout
            remapped if we did so above */
         execvp(*cmdArgs, cmdArgs);
         exit(EXIT_FAILURE); //Failed
      }
   }

   /* Wait on all children */
   WaitOnAllChildren(job);
}

int main() {
   Job *tempJob;
   jobList = NULL;

   /* Repeatedly print a prompt, read a commandline, run it, and delete it */
   while (!feof(stdin)) {
      printf(">> ");

      if ((tempJob = ReadJob(stdin))) { //If we actually have commands to run
         RunJob(tempJob); //Execute all commands
      }
   }

   while ((jobList = DeleteJob(jobList))) //User has exited, kill all jobs
      ;
}