#include <iostream>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MaxSizePath 80 //max size of path 
#define shellName "OS SHell:"
#define NONE  -1
#define STDIN  0
#define STDOUT 1
#define STDERR 2

using namespace std; 

bool externalCommand = false; // running external commands in background
pid_t pidToIgnore = 0; // when running pipe with background command (need to ignore exit status of the first pid)
string pidToPrint = "";

vector<string> splitInputIntoVector(string input);
void executeTokens(vector<string>& tokens,int& errorStatus);
void executeCd(vector<string>& tokens,int& errorStatus);
void printCurrentDirectory(string error="");
void replaceTildaToHome(vector<string>& tokens);
void replaceVariableToValue(vector<string>& tokens,int& errorStatus);
char ** createArrayFromVector(vector<string>& tokens);
void deleteArray(char *** array);

//HW6
int searchForRedirection(char **tokens, int &redirectionPlace);
void startingPositionToRemove(char **tokens, int &index, int &charsToRemove, int redirectionPlace);
void removeTokens(char *** tokens, int index, int charsToRemove);
int getNumberOfFD(char number);
int searchForPipe(char **tokens);
void makePipeSingleToken(char **tokens, int index, int &charsBeforePipe, int &charsAfterPipe);
void addTokens(char ***tokens, int index, int numOfChars, string when);

int main()
{
	cout<<"Welcome to OS SHell!"<< endl;
	int errorStatus=0;
	vector<string> tokens;
	string input;
	do 
	{
		externalCommand = false; // reset flag for running external commands in background 
		printCurrentDirectory();

		getline(cin,input);

		if( input =="exit" || cin.eof())
		{	
			cout<<"C ya!"<<endl;
			break;
		}
		tokens=splitInputIntoVector(input);
		executeTokens(tokens,errorStatus);
	}while (true);

	return errorStatus;
}

vector<string> splitInputIntoVector(string input)
{
	vector<string> tokens;
	string buf;
	stringstream ss(input); //insert the input into a stream
	while (ss >> buf)
	{
		if (buf == "&")
			externalCommand = true;
		else
			tokens.push_back(buf);
	}
	return tokens;
}

void executeTokens(vector<string>& tokens,int& errorStatus)
{
	replaceVariableToValue(tokens,errorStatus);
	replaceTildaToHome(tokens);
	pid_t pid1=-1, pid2=-1, pid_son;
	int status;
	int fd[2];
	int pipeIndex = NONE;

	if(tokens.size()==0)
		return;

	char **args = createArrayFromVector(tokens); // allocate memory for the command 'execvp'

	pipeIndex = searchForPipe(args); // if there is pipe - pipeIndex value will be diffrent from 'NONE'

	while((pid_son = waitpid(-1,&status,WNOHANG)) > 0) // will check if any zombie-children exist
	{
		if (WIFSIGNALED(status)) // exit by signal
			errorStatus = WTERMSIG(status) + 128;

		if (pid_son == pidToIgnore)
			pidToIgnore = 0; // reset pidToIgnore
		else
			pidToPrint = "[" + to_string(pid_son) + "] : exited, status=" + to_string(errorStatus); // print later
		
		errorStatus = 0; // exit status is reset to 0
	}

	if(tokens[0] == "cd" && pipeIndex == NONE)
		executeCd(tokens,errorStatus);

	else
	{
		int childID = 0; // use only in pipe (to recognize the processes)

		///////////////// there is a pipe /////////////////
		if (pipeIndex != NONE)
		{
			if (pipe(fd) == -1)
			{ // if pipe fail
				cerr<<"Pipe command failed"<<endl;
			       	deleteArray(&args); // delete allocated memory that was used for command 'execvp'
			       	exit(1);
			}

			int charsBeforePipe = 0,charsAfterPipe = 0;
			// check if there are chars linked to the pipe token
			makePipeSingleToken(args, pipeIndex, charsBeforePipe, charsAfterPipe);
 
			// seperate the pipe from other chars (if pipe linked to char)
			if (charsBeforePipe > 0)
				addTokens(&args, pipeIndex, charsBeforePipe, "BEFORE");
			if (charsAfterPipe > 0)
				addTokens(&args, pipeIndex, charsAfterPipe, "AFTER");

			// create 2 sons
			pid1 = fork();
			childID++; // first child will get id 1
			if (externalCommand == true)
				pidToIgnore = pid1;
			if (pid1 == -1)
			{
			       cerr<<"Error in fork"<<endl;
			       deleteArray(&args); // delete allocated memory that was used for command 'execvp'
			       exit(1);
			}

			if (pid1 != 0)
			{
				pid2=fork();
				childID++; // second child will get id 2

				if (pid2 == -1)
				{
					cerr<<"Error in fork"<<endl;
			       		deleteArray(&args); // delete allocated memory that was used for command 'execvp'
				       	exit(1);
				}
			}
		}
		///////////////// end if there is pipe /////////////////

		else // there is no pipe - create only one child
		{
			pid1=fork();
			if (pid1 == -1)
			{
			       cerr<<"Error in fork"<<endl;
			       deleteArray(&args); // delete allocated memory that was used for command 'execvp'
			       exit(1);
			}
		}

		if(pid1 == 0 || pid2 == 0) //son
		{
			if (pipeIndex != NONE) // there is a pipe
			{ // each child process updates his own command (one command for each process)
				int numOfTokens = 0;
				for (int i=0; args[i] != NULL; ++i)
					numOfTokens++;

				if (childID==1)
				{
					// remove all tokens after pipe
					removeTokens(&args, pipeIndex, numOfTokens-pipeIndex);

					close(fd[STDIN]);
				}
				else // (childID==2)
				{
					// remove all tokens before pipe
					removeTokens(&args, 0, pipeIndex+1); 

					// make second process to wait until the first will finish
					close(fd[STDOUT]);
				}
			}
			int redirectionIndex = 0, redirectionPlace = 0;

			// get the index of the first redirection. else return 0
			while ((redirectionIndex = searchForRedirection(args,redirectionPlace))>=0) 
			{ // this loop will run as long as there are redirections in the command

				char fileName[256], redirectionType;
				int file = NONE, numberOfFD = NONE; // -1

				////////// update information about: filename, redirectionType, numberOfFD //////////
				if (redirectionPlace == 1) // 0< or 1> only (with number linked to it in the left side)	
				{
					// there should be a number in left side
					numberOfFD = getNumberOfFD(args[redirectionIndex][0]); // get the fd number to redirect
					if (numberOfFD == NONE)
					{
						cerr << "OS SHell: token before redirection is not a number" << endl;
			       			deleteArray(&args); // delete allocated memory that was used for command 'execvp'
						exit(1); // to change later
					}
					redirectionType = args[redirectionIndex][1]; // get the redirection type

					if (strlen(args[redirectionIndex]) > 2) // there should be a filename (example 2>a)
						strncpy(fileName,args[redirectionIndex] + 2,strlen(args[redirectionIndex]));
					else
						strcpy(fileName, args[redirectionIndex+1]);
				}

				else // redirectionPlace==0 : < or > only (without number linked to it in the left side)	
				{
					numberOfFD = getNumberOfFD(args[redirectionIndex-1][0]);// get the fd number to redirect
					
					redirectionType = args[redirectionIndex][0]; // get the redirection type

					if (strlen(args[redirectionIndex]) > 1) // there should be a filename (example >a)
						strncpy(fileName,args[redirectionIndex] + 1,strlen(args[redirectionIndex]));
					else
						strcpy(fileName, args[redirectionIndex+1]);
				}
				///////////////////////////// done updating information ////////////////////////////

				if (redirectionType =='<') 
				{	
					// open file for read
    					file = open(fileName, O_RDONLY, S_IRWXO | S_IRWXG | S_IRWXU); 

					if (file < 0)
					{
						cerr << "OS SHell: cannot open file: "<< fileName <<endl;
			       			deleteArray(&args); 
						exit(1);
					}

					if (numberOfFD == NONE) // there was not an input of std redirect number
						numberOfFD = STDIN; // redirect from default (stdin)

					if (numberOfFD == STDIN) 
						close(STDIN); // close stdin
					
					dup2(file, numberOfFD);
					close(file);
				}

				else // open for write ">"
				{
					// open file for write
			    		file = open(fileName, O_WRONLY| O_CREAT |O_TRUNC, S_IRWXO | S_IRWXG | S_IRWXU); 

					if (file < 0)
					{
						cerr << "OS SHell: cannot open file: "<< fileName <<endl;
			       			deleteArray(&args); 
						exit(1);
					}

					if (numberOfFD == NONE) // there was not an input of std redirect number
						numberOfFD = STDOUT; // redirect from default (stdout)
					if (numberOfFD == STDERR)
						close(STDERR); // close stderr
					else
						close(STDOUT); // close stdout
	
					dup2(file, numberOfFD);
					close(file);
				}
			
				int charsToRemove = 2;

				//remove these tokens (after done dealing with the redirection)
				startingPositionToRemove(args, redirectionIndex, charsToRemove, redirectionPlace);
				removeTokens(&args, redirectionIndex, charsToRemove);						
			}

			if (pipeIndex != NONE) // there is a pipe
			{
				if (childID==1)
				{
					close(STDOUT);
					dup2(fd[STDOUT], STDOUT);
					close(fd[STDOUT]);
				}
				else // (childID==2)
				{
					close(STDIN);
					dup2(fd[STDIN], STDIN);
					close(fd[STDIN]);
				}
			}

			if (execvp(args[0],args) == -1)
			{
				cerr << "OS SHell: " << args[0] << ": command not found" << endl;
				deleteArray(&args); // delete allocated memory that was used for command 'execvp'
				exit(127);
			}
		} // child process done //
		
		else // father
		{
			if (pipeIndex != NONE) // if there was a pipe - close fd (otherwise child will run for infinity)
			{
				close(fd[STDIN]);
				close(fd[STDOUT]);
			}
			if(externalCommand) // if there is a need to run background command
			{
				if (pipeIndex != NONE) // pipe - show only the second command's pid
					cout<<"["<<pid2<<"]"<<endl;
				else // no pipe
					cout<<"["<<pid1<<"]"<<endl;
			}
				
			else // normal run (not background)
			{				
				waitpid(pid1,&status,0);	
				if (pipeIndex != NONE)
					waitpid(pid2,&status,0);


				deleteArray(&args); // delete allocated memory that was used for command 'execvp'

				if(WIFEXITED(status)) // normal exit
					errorStatus = WEXITSTATUS(status); // returns the exit status of the child
				
				else if (WIFSIGNALED(status)) // exit by signal
					errorStatus = WTERMSIG(status) + 128;
			}
			if((pid_son = waitpid(-1,&status,WNOHANG)) > 0) // will check if any zombie-children exist
			{
				if (WIFSIGNALED(status)) // exit by signal
					errorStatus = WTERMSIG(status) + 128;
				cout << "[" << pid_son << "] : exited, status=" << errorStatus << endl; 
		
				errorStatus = 0; // exit status is reset to 0
				pidToPrint = "";
			}

		}
	}
	if (pidToPrint != "") // if there was a zombie child - print exit status
	{
		cout<<pidToPrint<<endl;
		pidToPrint = "";
	}	
}

void makePipeSingleToken(char **tokens, int index, int &charsBeforePipe, int &charsAfterPipe)
{
	/***
	this function checks if there are chars linked to the PIPE.
	if yes - it will find how many and where those chars are. 
	***/

	// check if pipe linked to other chars
	if (tokens[index][0] == '|' && tokens[index][1] == '\0')
		return;
	
	bool beforePipe = false, afterPipe = false;
	int charsBeforePipe_TEMP = 0, charsAfterPipe_TEMP = 0;

	if (tokens[index][0] != '|')
		beforePipe = true;
	for (int i=0; tokens[index][i] != '|'; ++i)
		charsBeforePipe_TEMP++;

	if (tokens[index][charsBeforePipe_TEMP+1] != '\0')
		afterPipe = true;
	for (int i=charsBeforePipe_TEMP+1; tokens[index][i] != '\0'; ++i)
		charsAfterPipe_TEMP++;


	if (beforePipe)
		charsBeforePipe = charsBeforePipe_TEMP;
	if (afterPipe)
		charsAfterPipe = charsAfterPipe_TEMP;
}

void addTokens(char ***tokens, int index, int numOfChars, string when)
{
	/***
	this function adds one more token.
	we will use this function when we have PIPE | linked to more chars.
	Examples:	1) cat 0<a|grep .	2) cat 0<a| grep .	3) cat 0<a |grep .
	***/
	int numOfTokensToCopy = 1;
	for (int i=0; (*tokens)[i] != NULL; ++i)
		numOfTokensToCopy++;
	
	char **temp = new char*[numOfTokensToCopy+1]; // create temp char array

	for (int i=0, j=0; (*tokens)[i] != NULL; ++i) // copy the old array to the temp array
	{ 
		int size = strlen((*tokens)[i]);

		if (i != index)
		{
			temp[j] = new char[size+1];
 
			for (int k=0; k<size; ++k) 
				temp[j][k] = (*tokens)[i][k];

			temp[j][size] = '\0';
			j++;
		}
		else
		{ // i is equal to the index value
			if (when == "BEFORE")
			{
				temp[j] = new char[numOfChars+1];

				for (int k=0; k<numOfChars; ++k) 
					temp[j][k] = (*tokens)[i][k];
				temp[j][numOfChars] = '\0';
				j++;
		
				int newSize = size-numOfChars-1;
				temp[j] = new char[newSize+1];

				for (int k=numOfChars, m=0; k<size; ++k, ++m) 
					temp[j][m] = (*tokens)[i][k];

				temp[j][newSize] = '\0';
				j++;
			}
			else // (when == "AFTER")
			{
				temp[j] = new char[2];
				temp[j][0] = '|';
				temp[j][1] = '\0';
				j++;
			
				temp[j] = new char[numOfChars+1];

				for (int k=1, m=0; k<=numOfChars; ++k, ++m) 
					temp[j][m] = (*tokens)[i][k];
				temp[j][numOfChars] = '\0';
				j++;
			}
		}
		delete[] (*tokens)[i];
		(*tokens)[i] = NULL;
	}
	temp[numOfTokensToCopy] = NULL;

	delete[] (*tokens);
	(*tokens) = NULL;
	(*tokens) = temp;
}

void startingPositionToRemove(char **tokens, int &index, int &charsToRemove, int redirectionPlace)
{
	/***
	this function finds information about the token: index to know from which token we can remove AND how many chars to remove.
	we will use this function when we will need to remove the redirection tokens (also the fd before and the file name after).
	Parameters:
	'index': will update to be the start position to remove
	'charsToRemove': will updated to how many chars i need to remove
	***/
	bool numberBeforeRedirection = false;

	if (redirectionPlace == 0)
	{
		if (getNumberOfFD(tokens[index-1][0]) != -1) // if there is a number (different token) before redirection: 1 >
		{	
			numberBeforeRedirection = true;	
			index -= 1;
		}

		if (strlen(tokens[index]) == 1) // there is only redirection in this token
		{
			if (numberBeforeRedirection)
				charsToRemove = 3;
		}			
		else // there is also a file name in this token
		{
			if (!numberBeforeRedirection)
				charsToRemove = 1;
		}
	}
	else // if (redirectionPlace == 1)
	{
		if (strlen(tokens[index]) > 2) // there is also a file name in this token: 1>a
			charsToRemove = 1;
	}
	//cout <<"charsToRemove "<<charsToRemove<<endl;
	//cout <<"index "<<index<<endl;

}
void removeTokens(char ***tokens, int index, int charsToRemove)
{
	int numOfTokensToCopy = 0 - charsToRemove;
	for (int i=0; (*tokens)[i] != NULL; ++i)
		numOfTokensToCopy++;
	
	char **temp = new char*[numOfTokensToCopy+1]; // create temp char array

	for (int i=0, j=0; (*tokens)[i] != NULL; ++i) // copy the old array to the temp array (without the chars to remove)
	{ 
		int size = strlen((*tokens)[i]);

		temp[j] = new char[size+1]; 
		if (i != index)
		{

			for (int k=0; k<size; ++k) 
				temp[j][k] = (*tokens)[i][k];

			temp[j][size] = '\0';
			j++;

			delete[] (*tokens)[i];
			(*tokens)[i] = NULL;
		}
		else
		{ // i is equal to the index value - remove //
			while(charsToRemove > 0)
			{
				delete[] (*tokens)[i];
				(*tokens)[i] = NULL;
				charsToRemove--;
				i++;
			}
			i--;
		}
	}
	temp[numOfTokensToCopy] = NULL;

	delete[] (*tokens);
	(*tokens) = NULL;
	(*tokens) = temp;
}

int searchForPipe(char **tokens)
{
	bool apostrophe = false;

	for (unsigned int i=0; tokens[i] != NULL; ++i)
	{
		if (tokens[i][0] == '"') // check in the begining of the token
			apostrophe = !apostrophe;

		for (unsigned int j=0; j<strlen(tokens[i]); ++j)
		{
			if (tokens[i][j] == '|' && !apostrophe) // there is a pipe (not inside " ")
				return i;
		}
		if (tokens[i][strlen(tokens[i])-1] == '"') // check in the end of the token
			apostrophe = !apostrophe;
	}
	return -1;
}

int searchForRedirection(char **tokens, int &redirectionPlace)
{
	for (unsigned int i=0; tokens[i] != NULL; ++i)
	{
		if (tokens[i][0] == '>' || tokens[i][0] == '<')
		{
			redirectionPlace = 0; // that means redirection is first char. example: <
			return i;
		}
		else if (tokens[i][1] == '>' || tokens[i][1] == '<')
		{
			redirectionPlace = 1; // that means redirection is second char. example: 0<
			return i;
		}
	}
	return -1;
}

int getNumberOfFD(char number)
{
	if (isdigit(number))
		return (int)number - '0';
	return -1;
}

void executeCd(vector<string>& tokens,int& errorStatus)
{
	errorStatus=0;

	if(tokens.size() > 1)
		errorStatus=chdir(tokens[1].data());
	else // only 'cd' - go to HOME
		errorStatus=chdir(getenv("HOME"));

	if(errorStatus == -1)
	{
		printCurrentDirectory(" cd: No such file or directory");
		errorStatus = 1;
	}
	
}
void printCurrentDirectory(string error) //error is optional
{
	char buffer[MaxSizePath]; // temporary buffer to help cast
	string path; //current directory
	path = getcwd(buffer,MaxSizePath); //get path of bash
	string homePath = string(getenv("HOME")); // get path of home

	if(error!="")
		cout<<shellName<<error<<endl;
	else
	{
		if (path.find(homePath) != string::npos) // string::npos = not found
			path.replace(0, homePath.size(), "~");
		cout<<shellName<<path<<">";
	}



}
//the token in token[index] will be without ~
void replaceTildaToHome(vector<string>& tokens)
{
	// this function replace the '~' to actual HOME directory
	// example:	the command: 'ls -l ~/ex2/test' will execute as 'ls -l $HOME/ex2/test'
	size_t st;
	for(unsigned int index=0;index<tokens.size();index++)
	{
		st=0;
		while(st<tokens[index].length())
		{
			st=tokens[index].find("~",st);
			if(st!=string::npos)
				tokens[index].replace(st,1,getenv("HOME"));
		}
	}
}

void replaceVariableToValue(vector<string>& tokens,int& errorStatus)
{
	// this function handles all the $ expressions //

	regex variable("\\$[_a-zA-Z][_a-zA-Z0-9]*|\\$\\?");
	smatch m; // used for regex
	string varName;
	size_t startPos;
	char* value;
	int varLength;
	for (unsigned int index=0;index<tokens.size();index++)
	{
		startPos=0;
		while(regex_search(tokens[index],m,variable))
		{
			varName=m[0]; //get variable name from m
			startPos=tokens[index].find(varName); //find first index of var in tokens[i]
			varLength=varName.length(); //find length
			varName.replace(0,1,""); //remove $ from varName
			// after deleting $:
			if(varName == "?")
				tokens[index].replace(startPos,varLength,to_string(errorStatus));		
			else // if its a variable
			{
				value=getenv(varName.data()); // get the value of the variable
				if(value != NULL) // if there was a variable
					tokens[index].replace(startPos,varLength,value);//expand into the value of environment variable
				else // expand into empty string if there is no such environment variable
					tokens[index].replace(startPos,varLength,"");
			}	
		}
	}
}

char ** createArrayFromVector(vector<string>& tokens)
{
	int length, size = tokens.size();
	char** array = new char*[size+1];  

	for(int i=0;i<size;i++)	
	{
		length = tokens[i].length();
		array[i] = new char[length+1];
		tokens[i].copy(array[i],length);
		array[i][length]='\0';
	}
	array[size]=NULL; // so the command 'execvp' could know when to stop
	return array; 
}

void deleteArray(char *** array)
{
	if((*array) != NULL)
	{
		int i;
		for(i=0; (*array)[i] != NULL; ++i)
		{
			delete (*array)[i];
			(*array)[i] = NULL;	
		}
		delete (*array)[i]; // delete the last index
		(*array)[i] = NULL;

		delete[] (*array);
		(*array) = NULL;
	}
}	

