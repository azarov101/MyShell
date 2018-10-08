# MyShell
Shell that reads and evaluates commands given by the user on standard input
Implemented in C++.
The Shell Supports:
•	a prompt that prints the current working directory, and compresses the home directory part of it into “~”; 
•	exiting the shell via an exit command or via end-of-file on input; 
•	changing the current working directory via a cd command; 
•	substitution of “~” home directory references, “$?” last command exit status references, and “$VAR” environment variable references in input command line; 
•	running a single command with parameters, while waiting for its completion or running it in background using “&” suffix; 
•	reaping zombies — consuming background child processes that have completed their execution, and printing their exit status. 
•	input and output redirection support when running external programs.
•	Implement support for a single pipe between two processes, with standard output of first program being piped into standard input of second program.
