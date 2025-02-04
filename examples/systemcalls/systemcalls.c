#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int rc = system(cmd);
    return rc == 0 ;
/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/

   
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    int status;
    pid_t pid = fork();
    if (pid == -1)
    	return false; // fork failed
    else if (pid == 0) {
        execv(command[0], command);
        exit(-1); // exit if execv fails
    
    }

    va_end(args);

    if (waitpid(pid, &status, 0) == -1)
    	return false; // waitpid failed
    else if (WIFEXITED(status))
    	return WEXITSTATUS(status) == 0; // return true on success

    return false;

}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
int fdRedirect = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(fdRedirect == -1){
		perror("open");
		return false;
	}
	
	fflush(stdout);
	pid_t pid = fork();
	if(pid == -1){
		perror("fork");
		return false;
	}
	if(!pid){
		//We are the child, we'll setup the opened file descriptor as the STDOUT
		if(dup2(fdRedirect,1) < 0){
			perror("dup2");
			return false;
		}
		//Close our handle to the file (it will remain open since the descriptor was dup'd
		close(fdRedirect);
		
		//Execute the actual command using the new STDOUT file descriptor
		execv(command[0], command);
		
		// Should never get here, unless execv errors out
		printf("*ERROR* - execv() call failed\n");
		exit(-1);
	}
	
	close(fdRedirect);
	
	//Wait for the child to complete
	int status;
	if(waitpid(pid, &status, 0) == -1){
		//An error occured while waiting... we're unable to get the return status code
		return false;
	}
	
	else if(WIFEXITED(status))
		//The process has completed, we'll return true only if the exit status was 0
		return !WEXITSTATUS(status);
	
	//Some other error must have occurred
    return false;
}
