#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <fcntl.h>
#include <signal.h>

#define MAXLINE 2060 // Maximum command length, plus 12 chars for PID
#define MAXARG 512 // Maximum number of commands

char* getInput();
void expandInput(char* input);
int parseInput(char* charArray[MAXARG], char input[MAXLINE]);
int parseArgArray(char* charArrayOriginal[MAXARG], int numArgs, char* charArrayNew[MAXARG]);
void getStatus();
void exitProgram(int arrayVar[], int length);
void changeDir(char* charArray[MAXARG], int numArgs, char* home);
void getSigStatus(int childExit);
void resizeArray(int arrayVar[], int i, int length);
void catchSIGTSTP(int sig);

int ExStat = 0; // Exit Status
int termSig = 0; // Termination Signal
int foregroundOnly = 0; // Flag for foreground-only mode

int main(int argc, char* argv[]) {

    char *usrIn; // User input string to store user command

    // PID variables preset to -5
    pid_t childPID = -5;
    int childExitStatus = -5;
    pid_t childPID_Background = -5;
    int childExitStatus_Background = -5;

    pid_t pidList[MAXARG]; // Array for non-terminated child processes
    int childArrayLength = 0; // Tracker for element number in childPID array
    int* p = &pidList[0]; // List of active processes

    // Argument arrays (one with <, >, & and one without)
    char* argArray[MAXARG]; // Command array with <, >, &
    char* argArrayNew[MAXARG]; // Command array without <, >, &
    int argCount; // Count of arguments in argArray
    int argCountNew; // Count of arguments in argArrayNew

    char inFile[MAXARG]; // Input file variable
    char outFile[MAXARG]; // Output file variable

    int i, j; // iteration variables
    int background = 0; // Flag for & character indicating to run in background
    
    // Get user's home directory
    struct passwd *pw = getpwuid(getuid());
    char *homedir = pw->pw_dir;

    // I/O variables
    FILE* inFileOpen, outFileOpen;
    int sourceFD, targetFD, result;
    int devNull, devNullResult1, devNullResult2, devNullResult3;
    
    // Variables for signal handling
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, ignore_action = {0};

    // SIGINT attributes                   
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;

    // SIGTSTP attributes
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;

    // Other attributes
    ignore_action.sa_handler = SIG_IGN;
    
    // Parent process sigaction calls
    sigaction(SIGINT, &SIGINT_action, NULL);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    sigaction(SIGHUP, &ignore_action, NULL);
    sigaction(SIGQUIT, &ignore_action, NULL);

    // Variables for sigprocessmask to hold signal until process completion
    sigset_t mask;
	sigset_t orig_mask;
    sigemptyset (&mask);
    sigaddset(&mask, SIGTSTP);  

    while (1) {
        memset(&argArray[0], 0, sizeof(argArray)); // Set all array elements to 0
        memset(&argArrayNew[0], 0, sizeof(argArrayNew)); // Set all array elements to 0

        // Unblock SIGTSTP signal until process completion
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        // Check for terminated child processes
        childPID_Background = waitpid(-1, &childExitStatus_Background, WNOHANG);

        // If background process terminated
        if (childPID_Background > 0) {
            // Remove process from array and remove array counter
            resizeArray(pidList, childPID_Background, childArrayLength);
            childArrayLength = childArrayLength - 1;

            // Update termination information
            getSigStatus(childExitStatus_Background);

            // Output completion information
            if (WIFEXITED(childExitStatus_Background) != 0) {
                // Output exit status
                printf("background pid %d is done: exit status %d\n", childPID_Background, ExStat);
                fflush(stdout); // Flush output
            }
            else if (WIFSIGNALED(childExitStatus_Background) != 0) {
                // Output termination signal
                printf("background pid %d is done: terminated by signal %d\n", childPID_Background, termSig);
                fflush(stdout); // Flush output
            }
        }

        // Generate user input
        usrIn = getInput();

        // Skip to beginning of program if comment or empty input
        if ((usrIn[0] != '#') && (strlen(usrIn) != 1)) {
            // Parse input into separate arguments
            argCount = parseInput(argArray, usrIn);

            // Evaluate command values
            if (strcmp(argArray[0], "exit") == 0) {
                exitProgram(pidList, childArrayLength); // Exit program
            }
            else if (strcmp(argArray[0], "cd") == 0) {
                changeDir(argArray, argCount, homedir); // Change directory
                
            }
            else if (strcmp(argArray[0], "status") == 0) {
                getStatus(); // Get status of most recent process
            }
            else {
                // Separate all other commands from input/output and background commands
                argCountNew = parseArgArray(argArray, argCount, argArrayNew);

                // Check for background command before calling fork()
                if ((strcmp(argArray[argCount - 1], "&") == 0) && (foregroundOnly == 0)) {
                    // Run in background
                    background = 1;
                }

                // Block new signals until after process completion
                sigprocmask(SIG_BLOCK, &mask, NULL);

                // Create child process
                childPID = fork();

                // Evaluate childPID creation status
                switch (childPID)
                {
                    case -1: // Error creating child process
                        perror("Error creating new child process"); 
                        exit(1);
                        break;
                    case 0: // Child process created successfully
                        // Make adjustments to handler attribute values for SIGINT and SIGTSTP
                        SIGINT_action.sa_handler = SIG_DFL;
                        SIGTSTP_action.sa_handler = SIG_IGN;
                        sigaction(SIGINT, &SIGINT_action, NULL);
                        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

                        if (background == 1) {
                            // Redirect to /dev/null
                            devNull = open("/dev/null", O_WRONLY);

                            // Check for open success
                            if (devNull <= 0) {
                                perror("Error with background process");
                                exit(1);
                            }

                            // Redirect using dup2()
                            devNullResult1 = dup2(devNull, STDOUT_FILENO);
                            devNullResult2 = dup2(devNull, STDIN_FILENO);
                            devNullResult3 = dup2(devNull, STDERR_FILENO);

                            if (devNullResult1 == -1) { // Error redirecting STDOUT
                                perror("devNull dup2()"); 
                                exit(1); 
                            }
                            else if (devNullResult2 == -1) { // Error redirecting STDIN
                                perror("devNull dup2()"); 
                                exit(1); 
                            }
                            else if (devNullResult3 == -1) { // Error redirecting STDERR
                                perror("devNull dup2()"); 
                                exit(1); 
                            }
                        } 

                        // Check for i/o commands
                        for (i = 0; i < argCount; i++) {
                            // Check for input file
                            if (strcmp(argArray[i], "<") == 0) {
                                // Save input file to variable
                                strcpy(inFile, argArray[i + 1]);

                                // Open input file
                                sourceFD = open(inFile, O_RDONLY);
                                
                                // Check for open success
                                if (sourceFD <= 0) {
                                    perror("Cannot open file for input");
                                    exit(1);
                                }
                                else {
                                    // Redirect input
                                    result = dup2(sourceFD, 0);

                                    // Check that redirect was successful
                                    if (result == -1) { 
                                        perror("target dup2()"); 
                                        exit(1); 
                                    }
                                }
                            }
                            // Check for output file
                            else if (strcmp(argArray[i], ">") == 0) {
                                strcpy(outFile, argArray[i + 1]);

                                // Open output file
                                targetFD = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                                // Check for open success
                                if (targetFD <= 0) {
                                    perror("Cannot open file for output");
                                    exit(1);
                                }
                                else {
                                    // Redirect input
                                    result = dup2(targetFD, 1);

                                    // Check that redirect was successful
                                    if (result == -1) { 
                                        perror("target dup2()"); 
                                        exit(1); 
                                    }
                                }
                            }
                        }
                    
                        // Create new execution with appropriate command(s)
                        execvp(argArrayNew[0], argArrayNew);

                        // Error out if child process not terminated
                        printf("%s: ", argArrayNew[0]);
                        fflush(stdout);
                        perror(""); 
                        exit(1);

                        break;
                    default:
                        // Wait for child to terminate if foreground
                        if (background == 0) {
                            
                            // Block parent until child process with specified PID terminates
                            pid_t actualPID = waitpid(childPID, &childExitStatus, 0); 

                            // Update termination information
                            getSigStatus(childExitStatus);

                            // If ^C, output termination status
                            if (termSig == 2) {
                                getStatus(actualPID);
                            }
                            
                        }
                        else { // Process is in background - do not wait
                            // Add childPID to array
                            pidList[childArrayLength] = childPID;
                            
                            // Output PID information
                            printf("Background PID is %d\n", pidList[childArrayLength]);
                            fflush(stdout); // Flush output

                            // Increment array length
                            childArrayLength = childArrayLength + 1;
                        }

                        break;
                }
                
            }
        }

        // Reset background flag and redirection results
        background = 0;
        devNullResult1 = 0;
        devNullResult2 = 0;
        devNullResult3 = 0;

        // Free usrIn memory
        free(usrIn);
    }

    return 0;
}

// Generates valid user input
// Returns input string with PID expansion
char* getInput() {
    // Allocate memory for string
    size_t bufLen = MAXLINE;
    char* buffer = (char *)malloc(bufLen * sizeof(char));
    memset(buffer, '\0', MAXLINE);
    fflush(stdin);

    // Loop until valid input gathered
    do {
        printf(":");
        fflush(stdout); // Flush output
        getline(&buffer, &bufLen, stdin);
    } while ((strlen(buffer) > (MAXLINE - 12)) || (strlen(buffer) == 0)); // Make sure that original input meets max character requirements

    // Expand to include PID when $$ is typed
    expandInput(buffer);

    // Return input string
    return buffer;
}

// Expands user input to replace $$ with PID, if applicable
void expandInput(char* input) {
    // Save process ID and convert to char
    int pid = getpid();
    char pidChar[6]; 
    int n = sprintf(pidChar, "%d", pid);
    fflush(stdout); // Flush output

    // Create temporary string variable
    char temp[MAXLINE]; // Additional characters included in definition for PID expansion
    int tempLength = 0; // Length tracker for appending characters to string

    // Search for $$
    int i;
    for (i = 0; i < strlen(input); i++) {
        // If $$, add PID to temp string
        if ((input[i] == '$') && ( input[i+1] == '$')) {
            strcat(temp, pidChar);
        }
        // Skip this condition - already replaced in above
        else if ((input[i] == '$') && ( input[i-1] == '$')) {
            // Do nothing
        }
        // Replace last character with character and add to length
        else {
            temp[tempLength] = input[i];
            temp[tempLength + 1] = '\0';
        }

        // Update string length
        tempLength = strlen(temp);
    }

    // Save temp variable over input 
    strcpy(input, temp);

    // Reset temp variable
    strcpy(temp, "");
}

// Parses passed input and separates commands from space and newline character(s)
// Places each command in passed array
// Returns array length integer
int parseInput(char* charArray[MAXARG], char input[MAXLINE]) {
    char* token; // Character array variable for chunk of text
    char* rest = input; // Copy of passed character array variable
    int argCount = 0; // Counter for array element iteration
  
    // Separate text from whitespace and newline characters
    while ((token = strtok_r(rest, " \n", &rest))) {
        // Append to array and increment array count
        //strcpy(charArray[argCount], token);
        charArray[argCount] = token;
        argCount = argCount + 1;
    }

    // Return array size
    return argCount;
}

// Removes <, > and & from original argument array for passing into execvp
// Returns new array length integer
int parseArgArray(char* charArrayOriginal[MAXARG], int numArgs, char* charArrayNew[MAXARG]) {
    int argCount = 0; // Counter for array element iteration
    int skipFile = 0; // Flag for element immediately following i/o sign
    int i;

    // Iterate through original array and copy to new array if applicable
    for (i = 0; i < numArgs; i++) {
        if ((strcmp(charArrayOriginal[i], "<") == 0) || (strcmp(charArrayOriginal[i], ">") == 0)) {
            skipFile = 1;
        }
        else if (strcmp(charArrayOriginal[i], "&") != 0) {
            // Don't add element to array
            if (skipFile == 1) {
                skipFile = 0;
            }
            else {
                charArrayNew[argCount] = charArrayOriginal[i];
                argCount = argCount + 1;
            }
        }
    }

    // Return array size
    return argCount;
}

// Changes working directory to home or to specified path, if provided
void changeDir(char* charArray[MAXARG], int numArgs, char* home) {
    // Change to home directory if no argument given
    if (numArgs == 1) {
        
        if (chdir(home) != 0) { // Check for failure to change directory
            perror("changeDir() failed");
        }        
    }
    else {
        // Change to specified path
        if (chdir(charArray[1]) != 0) { // Check for failure to change directory
            perror("changeDir() failed");
        }
    }
}

// Prints status of program termination signal or exit status
void getStatus() {
    if (termSig == 0){
        printf("exit value %d\n", ExStat);
    }
    else{
        printf("terminated by signal %d\n", termSig);
    }

    fflush(stdout); // Flush output
}

// Kills all running processes before exiting the program
void exitProgram(int arrayVar[], int length) {
    int i;
    int pid, stat;

    // Kill contents of PID array
    for (i = 0; i < length; i++) {
        if (arrayVar[i] > 0) {
            kill(arrayVar[i], SIGKILL);
        }
    }

    // Final waitpid loop
    while (pid = waitpid(-1, &stat, WNOHANG) > 0) {}

    exit(0);
}

// Updates termination signal and exit status in accordance with most recent call
void  getSigStatus(int childExit) {
    // Get exit status and terminating signal of child process
    if (WIFEXITED(childExit) != 0) {
        ExStat = WEXITSTATUS(childExit);

        // Reset termination signal flag
        termSig = 0;
    }
    else if (WIFSIGNALED(childExit) != 0) {
        termSig = WTERMSIG(childExit);

        // Reset exit status flag
        ExStat = 0;
    }
}

// Removes passed element from array
void resizeArray(int arrayVar[], int i, int length) {
    pid_t pidListNew[MAXARG];
    int newLength = 0;
    int j;

    // Create new array without element at passed PID i
    for (j = 0; j < length; j++) {
        if (arrayVar[j] != i) {
            // Set element to old array value
            pidListNew[newLength] = arrayVar[j];

            // Increment length of new array
            newLength = newLength + 1;
        }
    }

    // Set passed array elements to new values
    for (j = 0; j < newLength; j++) {
        // Copy new values to passed array
        arrayVar[j] = pidListNew[j];
    }

}

// Catches SIGTSTP signals
// Outputs appropriate message and sets foregroundOnly flag
void catchSIGTSTP(int sig) {
    // Messages for output
    char* message1 = "\nEntering foreground-only mode (& is now ignored)\n:";
    char* message2 = "\nExiting foreground-only mode\n:";
    
    if (foregroundOnly == 0) { // ^Z was not set: Set to foreground-only mode
        foregroundOnly = 1;
        write(STDOUT_FILENO, message1, 51);
    }
    else { // ^Z was set: Revert to regular mode
        foregroundOnly = 0;
        write(STDOUT_FILENO, message2, 31);
    }
}