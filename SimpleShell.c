/*
COMP 310/ECSE 427
Winter 2015
Simple Shell
Assignment 1 Solution
Author: Ahmed Youssef
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define MAX_HISTORY 10
#define MAX_PATH 1024

typedef struct history_s {
    char command[MAX_LINE];
    int error;
} history_s;

struct background_s {
    char command[MAX_LINE];
    pid_t pid;
    int num_id; //number identifier of background process
    struct background_s* next;
};
typedef struct background_s background_t;

history_s history[MAX_HISTORY]; // history array
int history_in = -1; // index of latest history element
background_t* bg_list_head=NULL; // pointer to the head of the linked list containing the background processes

void put_history(char* inputBuffer);
char* get_history(char first_letter);
void error_history();
void print_history();
void put_background(pid_t pid, char* command);
void print_background();
void add_node(background_t* new_bg);
void rmv_node(background_t* prev);
pid_t find_node(int nid);
int IsBuiltin(char* args[]);

// Parses the input buffer
void setup(char inputBuffer[], char *args[], int *background)
{
  int i = 0,        /* loop index for accessing inputBuffer array */
    start,    /* index where beginning of next command parameter is */
    ct;       /* index of where to place the next parameter into args[] */

  ct = 0;
  start = -1;

  /* examine every character in the inputBuffer */
  while(inputBuffer[i] != '\0') {
    switch (inputBuffer[i]){
    case ' ':
    case '\t':               /* argument separators */
      if(start != -1){
        args[ct] = &inputBuffer[start];    /* set up pointer */
        ct++;
      }
      inputBuffer[i] = '\0'; /* add a null char; make a C string */
      start = -1;
      break;
    case '\n':                 /* should be the final char examined */
      if (start != -1){
        args[ct] = &inputBuffer[start];
        ct++;
      }
      inputBuffer[i] = '\0';
      args[ct] = NULL; /* no more arguments to this command */
      break;
    default :             /* some other character */
      if (inputBuffer[i] == '&'){
        *background  = 1;
        inputBuffer[i] = '\0';
      } else if (start == -1)
        start = i;
    }
    i++;
  }
  args[ct] = NULL; /* just in case the input line was > MAX_LINE */
}

int main(void)
{
    char inputBuffer[MAX_LINE];
    int background, status, length;
    char* args[MAX_LINE/+1];
    pid_t pid;
    char* command;

    while(1)
    {
        background = 0;
        printf("\nCOMMAND-> ");
        fflush(stdout);
        length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

        if (length == 0) {
            /* ctrl-d was entered, quit the shell normally */
            printf("\n");
            exit(0);
        } else if (length < 0) {
            /* somthing wrong; terminate with error code of -1 */
            perror("Reading the command");
            exit(-1);
        } else if(inputBuffer[0] == '\n') continue;

        inputBuffer[length] = '\0';

        if(inputBuffer[0] == 'r')
        {
            if(length == 2) { // Only 'r' was entered followed by \n
                command = get_history(0);
                if(command == NULL) {
                    printf("Latest command resulted in an error.\n");
                    continue;
                }
                strcpy(inputBuffer, command);
            } else {
                command = get_history(inputBuffer[2]);
                if(command == NULL) {
                    printf("Latest command that started with '%c' resulted in an error\n", inputBuffer[2]);
                    continue;
                }
                strcpy(inputBuffer, command);
            }
        }

        put_history(inputBuffer);
        setup(inputBuffer, args, &background);

        if(IsBuiltin(args)) continue;

        pid = fork();

        if(pid == 0)
        {
            execvp(args[0], args);

            printf("Could not launch specified program: %s\n", strerror(errno));
            exit(1);

        } else
        {
            if(background == 0)
            {
                waitpid(pid, &status, 0);
                if(WEXITSTATUS(status) == 1) {
                    error_history();
                }
            } else
            {
                put_background(pid, get_history(0));
            }
        }
    }
}

// Add command to history buffer
void put_history(char* inputBuffer)
{
  history_in++;
  strcpy(history[history_in%MAX_HISTORY].command, inputBuffer);
  history[history_in%MAX_HISTORY].error = 0;
}

// Returns the latest command that was inputed that started with first_letter
// if first_letter = 0, returns latest command
char* get_history(char first_letter)
{
    int limit, i;

    if(history_in == -1) return NULL;

    if(first_letter == 0) {
        return (history[history_in%MAX_HISTORY].error == 1)? NULL: history[history_in%MAX_HISTORY].command;
    } else {
        limit = (history_in - MAX_HISTORY + 1) > 0 ? (history_in - MAX_HISTORY + 1) : 0;
        for(i = history_in; i >= limit; i--) {
            if(history[i%MAX_HISTORY].command[0] == first_letter) {
                return (history[i%MAX_HISTORY].error == 1)? NULL: history[i%MAX_HISTORY].command;
            }
        }
        return NULL;
    }
}

// Sets error of the last command in the history buffer. Only called on foreground processes
void error_history()
{
    history[history_in%MAX_HISTORY].error = 1;
}

void print_history()
{
    int limit, i;

    limit = (history_in - MAX_HISTORY + 1) > 0 ? (history_in - MAX_HISTORY + 1) : 0;
    for(i = history_in; i >= limit; i--) {
        printf("[%d] %s error = %d\n", i, history[i%MAX_HISTORY].command, history[i%MAX_HISTORY].error);
    }
}

// creates new node and adds it to the linked list of background processes
void put_background(pid_t pid, char* command)
{
    background_t* new_bg = (background_t*) malloc(sizeof(background_t));

    new_bg->pid = pid;
    strcpy(new_bg->command, command);
    new_bg->num_id = 0;
    new_bg->next = NULL;

    add_node(new_bg);
}

// Prints the list of background processes
void print_background()
{
    background_t* curr;
    background_t* prev=NULL;
    int i=1;
    pid_t result;

    for(curr= bg_list_head; curr != NULL; curr = curr->next)
    {
        result = waitpid(curr->pid, NULL, WNOHANG);
        if(result == curr->pid) {
            rmv_node(prev);
        } else {
            curr->num_id = i;
            printf("[%d] %s", i, curr->command);
            i++;
        }
        prev = curr;
    }
}

// Adds new_bg to the linkedlist of background processes
void add_node(background_t* new_bg)
{
    background_t* curr;

    if(bg_list_head == NULL) {
        bg_list_head = new_bg;
        return;
    }

    for(curr=bg_list_head; curr->next != NULL; curr = curr->next);

    curr->next = new_bg;
}

// removes prev from linkedlist
void rmv_node(background_t* prev)
{
    background_t* rmv;

    if(prev == NULL) {
        free(bg_list_head);
        bg_list_head = NULL;
        return;
    }

    rmv = prev->next;
    prev->next = rmv->next;

    free(rmv);
}

// returns the pid of the process with ID nid
pid_t find_node(int nid)
{
    background_t* curr;

    for(curr= bg_list_head; curr != NULL; curr = curr->next)
    {
        if(curr->num_id == nid) return curr->pid;
    }

    return 0;
}

// Executes built-in command otherwise returns 0
int IsBuiltin(char* args[])
{
    char cwd[MAX_PATH];
    pid_t pid;

    if(strcmp(args[0], "cd")==0)
    {
        if(args[1] != NULL) {
            if (chdir(args[1]) == -1) {
                perror("cd: ");
                error_history();
            }

        } else {
            if (chdir(getenv("HOME")) == -1) {
                perror("cd: ");
                error_history();
            }
        }
        return 1;

    } else if(strcmp(args[0], "pwd")==0)
    {
        if (getcwd (cwd, MAX_PATH) == cwd) {
            printf("%s\n", cwd);
        } else {
            perror("getting cwd");
            error_history();
        }
        return 1;

    } else if(strcmp(args[0], "exit")==0)
    {
        exit(0);
    } else if(strcmp(args[0], "history")==0)
    {
        print_history();
        return 1;

    } else if(strcmp(args[0], "jobs")==0)
    {
        print_background();
        return 1;

    } else if(strcmp(args[0], "fg")==0)
    {
        pid = find_node(atoi(args[1]));
        if(pid == 0) {
            printf("No background process with this ID\n");
        } else {
            waitpid(pid, NULL, 0);
        }
        return 1;
    }

    return 0;
}
