#include "defs.h"

void sigchld_handler(int s);
void *getSocketAddress(struct sockaddr *sa);

int setUpServer();

int waitForConnections();

void parseRequest(char* request, int newSocket);

void headRequest(int newSocket, char **fileArgs);
void getRequest(int newSocket, char **fileArgs);
void cgi_likeRequest(int newSocket, char **fileArgs);

void error400_BadRequest(int newSocket);
void error403_PermissionDenied(int newSocket);
void error404_NotFound(int newSocket);
void error500_InternalError(int newSocket, char* errorText);
void error501_NotImplemented(int newSocket, char* errorText);


int main (int argc, char const *argv[])
{
	/* itterator and a flag that will evaluate false if the givena argument isnt a number */
	int i, isNumber;
	
	/* isNumber defaults to true */
	isNumber = 1;
	
	/* if there are not exactly two arguments, then complain and stop */
	if (argc != 2)
	{
		fprintf(stderr, "%s\n", "[SERVER] Can't start, failed to specify port number.");
		fprintf(stderr, "%s\n", "[SERVER] Usage: httpd <port_number_greater_than_1024>");
		exit(1);
	}
	
	i = 0;
	/* while there are more characters of the second argument */
	while (argv[1][i])
	{
		/* if the charater is not a numeric digit */
		if (!isdigit(argv[1][i]))
		{
			/* then switch the isNumber flag to false */
			isNumber = 0;
		}
		i++;
	}
	
	/* check the flag and complain if it is false */
	if (!isNumber)
	{
		fprintf(stderr, "%s\n", "[SERVER] Can't start, failed to interpret port number.");
		fprintf(stderr, "%s\n", "[SERVER] Usage: httpd <port_number_greater_than_1024>");
		exit(1);
	}
	
	/* otherwise setup the server to run listening on that port */
	setUpServer(argv[1]);
	return 0;
}

/* this custom signal handler reaps zombie children */
void sigchld_handler(int s)
{
	/* waitpid() might overwrite errno, so we save and restore it: */
	int saved_errno = errno;

	/* waits for all children to kill themselves */
	while(waitpid(-1, NULL, WNOHANG) > 0);

	/* restore saved erno */
	errno = saved_errno;
}

/* get sockaddr, IPv4 or IPv6: */
void *getSocketAddress(struct sockaddr *socketAddress)
{
	/* returns the ipv4 address */
	if (socketAddress->sa_family == AF_INET)
	{
		return &(((struct sockaddr_in*)socketAddress)->sin_addr);
	}
	/* returns ipv6 address */
	return &(((struct sockaddr_in6*)socketAddress)->sin6_addr);
}

/* setsup the listening socket and signal handler */
int setUpServer(char const *portNum)
{
	printf("[SERVER] Starting on port: %s\n", portNum);
	fflush(stdout);
	
	/* listeningSocket is the socket fd used for listening, yes stores 1 to be pointed to later */
	int listeningSocket, yes, returnValue;
	/* various address info structs */
	struct addrinfo setupInfo, *servinfo, *clientInfo;
	/* sigaction struct */
	struct sigaction sa;
	

	/* populate the setup info */
	memset(&setupInfo, 0, sizeof setupInfo);
	setupInfo.ai_family = AF_UNSPEC;
	setupInfo.ai_socktype = SOCK_STREAM;
	setupInfo.ai_flags = AI_PASSIVE;
	yes = 1;

	/* uses getaddrinfo to pupulate serverinfo with addresses from a given host */
	if ((returnValue = getaddrinfo(NULL, portNum, &setupInfo, &servinfo)) != 0) 
	{
		/* check and print error */
		fprintf(stderr, "[SERVER] Error: getaddrinfo() failed: %s.\n", gai_strerror(returnValue));
		fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
		fflush(stderr);
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(clientInfo = servinfo; clientInfo != NULL; clientInfo = clientInfo->ai_next) 
	{
		/* setup listening socket */
		if ((listeningSocket = socket(clientInfo->ai_family, clientInfo->ai_socktype, clientInfo->ai_protocol)) == -1) 
		{
			fprintf(stderr, "%s\n", "[SERVER] Error: socket() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Continuing...");
			fflush(stderr);
			continue;
		}
		/* set socket options */
		if (setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) 
		{
			fprintf(stderr, "%s\n", "[SERVER] Error: listeningSocket() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
			fflush(stderr);
			exit(1);
		}
		/* bind socket */
		if (bind(listeningSocket, clientInfo->ai_addr, clientInfo->ai_addrlen) == -1) 
		{
			close(listeningSocket);
			fprintf(stderr, "%s\n", "[SERVER] Error: bind() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Continuing...");
			fflush(stderr);
			continue;
		}
		break;
	}

	/* free up the addrinfo structs */
	freeaddrinfo(servinfo);
	/* error check bind */
	if (clientInfo == NULL)  
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: bind() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
		fflush(stderr);
		exit(1);
	}
	/* listen using the BACKLOG macro */
	if (listen(listeningSocket, BACKLOG) == -1) 
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: listen() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
		fflush(stderr);
		exit(1);
	}

	/* setup the sigaction handler for sigchld signals */
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	/* error check sigaction */
	if (sigaction(SIGCHLD, &sa, NULL) == -1) 
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: sigaction() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
		fflush(stderr);
		exit(1);
	}
	
	/* wait for connection to the listening socket */
	waitForConnections(listeningSocket);
	return 0;
}

/* loops indefinetly waiting for connections and spawning children to fufill requests */
int waitForConnections(int listeningSocket)
{
	/* new socket to fufill the request */
	int newSocket;
	/* length of client address */
	socklen_t clientAddressLength;
	/* connector's address information */
	struct sockaddr_storage clientAddress;
	/* holds a numeric representation of the address */
	char numericAddress[INET6_ADDRSTRLEN];
	
	char request[1024];
	int recieveValue;
	
	pid_t forkID;
	pid_t childID;
	
	/* info */
	printf("[SERVER] Waiting for connections...\n");
	fflush(stdout);
	
	/* continuously handle requests */
	while(1) 
	{ 
		/* get address length */
		clientAddressLength = sizeof(clientAddress);
		/* newSocket gets the fd of listening socket iff the accept() is successfull */
		newSocket = accept(listeningSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
		if (newSocket == -1)
		{
			/* error check accept() */
			fprintf(stderr, "%s\n", "[SERVER] Error: accept() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Continuing...");
			fflush(stderr);
			continue;
		}
	
		/* convert the client address to a printable numeric format */
		inet_ntop(clientAddress.ss_family, getSocketAddress((struct sockaddr*)&clientAddress), numericAddress, sizeof(numericAddress));
		/* print the address for each client connection */
		printf("[SERVER] Connection opened from %s ...\n", numericAddress);
	
		/* fork to handle request */
		forkID = fork();
		/* check for fork error */
		if (forkID == -1)
		{
			/* fork() failed print and send error to client */
			fprintf(stderr, "%s\n", "[SERVER] Error: fork() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
			fflush(stderr);
			error500_InternalError(newSocket, "Error: fork() failed.\n");
			exit(1);
		}
		/* child process */
		else if (!forkID)
		{
			/* child doesn't need the listener */
			close(listeningSocket);
			
			/* recieve then handle error or deal with request */
			recieveValue = recv(newSocket, request, 1000, 0);
			/* error check recv() */
			if (recieveValue == 0)
			{
				/* recv() error */
				fprintf(stderr, "%s\n", "[SERVER] Error: Connection terminated by client.");
				fprintf(stderr, "%s\n", "[SERVER] Continuing...");
				fflush(stderr);
				error500_InternalError(newSocket, "Error: Connection terminated by client.\n");
				continue;
			}
			else
			{
				/* recv() success, handle request */
				childID = getpid();
				printf("[SERVER] Proccess %i recieved request.\n", (int)childID);;
				printf("[SERVER] Proccess %i handling request.\n", (int)childID);
				parseRequest(request, newSocket);
			}
			
			/* after parseRequest() is completed, the connection can be closed */
			close(newSocket);
			/* print server info */
			printf("[SERVER] Proccess %i closed connection.\n\n", (int)childID);
			fflush(stdout);
			/* end child proccess normally */
			exit(0);
		}
		/* parent process */
		else
		{
			/* parent doesnt need to do anything, so it closes its connection to the socket */
			close(newSocket);
		}
	}
	/* if the while loop is brokoen the server will shut down normally */
	printf("%s\n", "[SERVER] Shutting down...");
	fflush(stdout);
	return 0;
}

/* parse request interprets the http request and calls an apropriate function to handle it */
void parseRequest(char* request, int newSocket)
{
	/* typeText holds GET or HEAD */
	/* file holds the content of the request */
	/* footer holds the "HTTP/1.0" */
	/* initialPath holds the first few characters of file to check for cgi-like requests */
	char typeText[8], file[1000], footer[16], initialPath[16];
	/* token and tokPTR are used with the strtok_r() function to split up file[] */
	char *token, *tokPTR, *subStringPTR;
	/* fileArgs holds pointers to file delimeted by question marks to handle cgi-like commands */
	/* fileArg[0] should always have the path of the rquested file */
	char *fileArgs[500];
	/* iterator */
	int i;
	
	/* if the request starts with a space */
	if (request[0] == ' ')
	{
		error400_BadRequest(newSocket);
		return;
	}
	
	/* tokenize the request delimiting with spaces */
	token = strtok_r(request, " ", &tokPTR);
	/* copy the first token contents into typeText */
	strcpy(typeText, token);
	
	/* tokenize the request delimiting with spaces */
	token = strtok_r(NULL, " ", &tokPTR);
	/* if there is no second token, the request is incomplete */
	if (!token)
	{
		error400_BadRequest(newSocket);
		return;
	}
	/* otherwise copy second token into file[] */
	else
	{
		strcpy(file, token);
	}
	
	/* checks if file contains the substring ".." */
	subStringPTR = strstr(file, "..");
	/* if it does contain ".." */
	if (subStringPTR)
	{
		/* reject the request, send error 501 */
		error501_NotImplemented(newSocket, "Using \"..\" in the request is not supported.\n");
		return;
	}
		
	/* tokenize the request delimiting with spaces */
	token = strtok_r(NULL, " ", &tokPTR);
	/* if there is no third token, the request is incomplete */
	if (!token)
	{
		error400_BadRequest(newSocket);
		return;
	}
	/* otherwise copy the third token into footer */
	else
	{
		/* the footer must be "HTTP/1.0", so a null is written to bit 9 to terminate the string */
		strcpy(footer, token);
		footer[8] = '\0';
		
		/* checks the footer against "HTTP/1.0", any other footer is rejected */
		if (strcmp(footer, "HTTP/1.0"))
		{
			error400_BadRequest(newSocket);
			return;
		}
	}
	
	/* attempt to tokenize the request again delimiting with spaces */
	token = strtok_r(NULL, " ", &tokPTR);
	/* only three tokens are expected if there is a fourth, the request is invalid */
	if (token)
	{
		error400_BadRequest(newSocket);
		return;
	}
	
	/* clear token and tokPTR; not sure if this is at all necessary, but it couldn't hurt  */
	token = tokPTR = NULL;
	
	i = 0;
	/* tokenize file[] delimiting with question marks */
	token = strtok_r(file, "?", &tokPTR);
	/* while there are more tokens */
	while (token)
	{
		/* set a pointer in fileArgs to point to this token */
		fileArgs[i] = token;
		/* get the next token delimiting with question marks */
		token = strtok_r(NULL, "&", &tokPTR);
		/* increment */
		i++;
	}
	/* set the next pointer to explicitly point to null, will make exec() easier later */
	fileArgs[i] = NULL;
	
	/* checks the type of the request */
	/* if the type is HEAD */
	if (!strcmp(typeText, "HEAD"))
	{
		/* fufill the head request */
		headRequest(newSocket, fileArgs);
	}
	/* otherwise if the type is GET */
	else if (!strcmp(typeText, "GET"))
	{
		/* copy the first 10 characters of fileArgs[0] (the path) into initialPath */
		memcpy(initialPath, fileArgs[0], 10);
		/* force the initialPath string to be terminated at bit 11 */
		initialPath[10] = '\0';
		
		/* compare the path to "/cgi-like/" to determine if this is a cgi-like request */
		/* if the path matches "/cgi-like/" */
		if (!strcmp(initialPath, "/cgi-like/"))
		{
			/* fufill the cgi-like request */
			cgi_likeRequest(newSocket, fileArgs);
		}
		/* otherwise this must be a normal GET request */
		else
		{
			/* fufill the get request */
			getRequest(newSocket, fileArgs);
		}
	}
	/* otherwise if type doesn't match HEAD or GET, then it is a bad request */
	else
	{
		error400_BadRequest(newSocket);
		return;
	}
}

/* specifically handles HEAD requests */
void headRequest(int newSocket, char **fileArgs)
{
	/* fileSize holds the raw size of the file in bits */
	int fileSize;
	/* this will hold the header string to be sent as a response */
	char header[512], pathWithoutLeadingSlash[1024];
	/* this struct holds info about each file, returned by the stat() function */
	struct stat fileStat;
	
	/* if the file path starts with '/' */
	if (fileArgs[0][0] == '/')
	{
		/* trim the '/' from the begining */
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][1]);
	}
	/* otherwise just copy the path whole */
	else
	{
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][0]);
	}
		
	/* use stat() to get all pertanant file info and check for success */
	if (stat(pathWithoutLeadingSlash, &fileStat))
	{
		/* if stat fails the file is not found */
		error404_NotFound(newSocket);
		return;
	}
	/* checks the file permisions for other read access */
	if (!(fileStat.st_mode & S_IROTH))
	{
		/* if other read access is disabled then permision is denied */
		error403_PermissionDenied(newSocket);
		return;
	}
	
	/* NOT FUNCTIONING PROPERLY */
	/* checks for any of the special file types */
	if (S_ISBLK(fileStat.st_mode) || S_ISCHR(fileStat.st_mode) || S_ISDIR(fileStat.st_mode) || S_ISFIFO(fileStat.st_mode) || S_ISLNK(fileStat.st_mode))
	{
		/* special file types are not supported */
		error501_NotImplemented(newSocket, "This functionality only supported for normal files.\n");
		return;
	}
	
	/* get file size from the stat structure */
	fileSize = (fileStat.st_size);
	
	/* use sprintf() to format the header for sending */
	sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", fileSize);
	/* send header and error check */
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		/* if the send failed, give 500 error to client */
		error500_InternalError(newSocket, "Error: send() failed.\n");
		/* print server info */
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
}

/* specifically handles GET requests without cgi-like support */
void getRequest(int newSocket, char **fileArgs)
{
	/* fileSize holds the raw size of the file in bits */
	int fileSize;
	/* file stream pointer */
	FILE* lineIn;
	/* this will hold the header string to be sent as a response */
	char header[512], pathWithoutLeadingSlash[1024];
	/* contents pointer will point to the current line being sent */
	char *contentBuffer;
	/* this struct holds info about each file, returned by the stat() function */
	struct stat fileStat;
	
	/* check the path for a leading '/' */
	if (fileArgs[0][0] == '/')
	{
		/* if it has one, trim the slash and copy into pathWithoutLeadingSlash */
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][1]);
	}
	/* otherwise copy the path unedited */
	else
	{
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][0]);
	}
		
	/* use stat() to get all pertanant file info and check for success */
	if (stat(pathWithoutLeadingSlash, &fileStat))
	{
		/* if stat fails the file is not found */
		error404_NotFound(newSocket);
		return;
	}
	/* checks the file permisions for other read access */
	if (!(fileStat.st_mode & S_IROTH))
	{
		/* if other read access is disabled then permision is denied */
		error403_PermissionDenied(newSocket);
		return;
	}
	
	/* NOT FUNCTIONING PROPERLY */
	/* checks for any of the special file types */
	if (S_ISBLK(fileStat.st_mode) || S_ISCHR(fileStat.st_mode) || S_ISDIR(fileStat.st_mode) || S_ISFIFO(fileStat.st_mode) || S_ISLNK(fileStat.st_mode))
	{
		/* special file types are not supported */
		error501_NotImplemented(newSocket, "This functionality only supported for normal files.\n");
		return;
	}
	
	/* get file size from the stat structure */
	fileSize = (fileStat.st_size);
	
	/* open the file for reading, already checked the permisions so this isn't error checked */
	lineIn = fopen(pathWithoutLeadingSlash, "r");
	
	/* use sprintf() to format the header for sending */
	sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", fileSize);
	/* send header and error check */
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		/* if the send failed, give 500 error to client */
		error500_InternalError(newSocket, "Error: send() failed.\n");
		/* print server info */
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	
	/* get a line from the file using readLine() */
	contentBuffer = readLine(lineIn);
	/* each call to readline() will set contentBuffer[0] to NULL unless there is more to read */
	/* while there are more lines */
	while (contentBuffer[0])
	{
		/* send the line to the client and error check */
		if (send(newSocket, contentBuffer, strlen(contentBuffer), 0) == -1)
		{
			/* if the send failed, give 500 error to client */
			error500_InternalError(newSocket, "Error: send() failed.\n");
			/* print server info */
			fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Continuing...");
			fflush(stderr);
		}
		/* get the next line */
		contentBuffer = readLine(lineIn);
	}
	/* free the memory holding the buffer */
	free(contentBuffer);
}

/* specifically handles GET requests with cgi-like support */
void cgi_likeRequest(int newSocket, char **fileArgs)
{
	/* child and parent process IDs */
	pid_t childID, parentID;
	/* contentBuffer will point to the current line being sent */
	char *contentBuffer;
	/* tempFileNameWithPID holds the name to be used as a buffer file for sending */
	/* pathWithoutLeadingSlash holds the path with the leading '/' trimmed */
	/* fullPathFileName contains the full path (its just apends '.' to the path) */
	/* header holds the header string to be sent with the response */
	char tempFileNameWithPID[32], pathWithoutLeadingSlash[1024], fullPathFileName[32], header[512];
	/* redirectFD holds the file descriptor of the temporary buffer file */
	/* fileSize holds the raw size of the buffer file in bits */
	int redirectFD, fileSize;
	/* streamIN is the FILE* used to read from the buffer file line by line */
	FILE* streamIN;
	/* this struct holds info about each file, returned by the stat() function */
	struct stat fileStat;

	/* get the parent ID to use for naming the temporary file */
	parentID = getpid();
	
	/* construct the temporary file's name */
	sprintf(tempFileNameWithPID, "cgi_like_%i", (int)parentID);
	
	/* create the temporary file */
	redirectFD = open(tempFileNameWithPID, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	
	/* redirect stdout to the temporary file */
	dup2(redirectFD, 1);
	
	/* check the path for a leading '/' */
	if (fileArgs[0][0] == '/')
	{
		/* if it has one, trim the slash and copy into pathWithoutLeadingSlash */
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][1]);
	}
	/* otherwise copy the path unedited */
	else
	{
		strcpy(pathWithoutLeadingSlash, &fileArgs[0][0]);
	}
		
	/* fork processes */
	childID = fork();
	/* error check fork() */
	if (childID == -1)
	{
		/* if fork() fails, send 500 error to client */
		fprintf(stderr, "%s\n", "[SERVER] Error: fork() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Shutting down...");
		fflush(stderr);
		error500_InternalError(newSocket, "Error: fork() failed.\n");
		exit(1);
	}
	/* child process */
	else if (!childID)
	{
		/* the child calls execv() to run the cgi-like command */
		execv(pathWithoutLeadingSlash, fileArgs);
		/* if execv() fails with erno 2, then the command is not in the cgi-like directory */
		if (errno == 2)
		{
			/* print error to the server */
			fprintf(stderr, "%s\n", "[SERVER] Exec() failed, command not found.");
			/* atttempt to fufill the request as a non-cgi-like request */
			getRequest(newSocket, fileArgs);
			/* terminate upon completion */
			exit(0);
		}
		else if (errno == 13)
		{
			/* print error to the server */
			fprintf(stderr, "%s\n", "[SERVER] Exec() failed, permission denied.");
			/* atttempt to fufill the request as a non-cgi-like request */
			getRequest(newSocket, fileArgs);
			/* terminate upon completion */
			exit(0);
		}
		/* otherwise terminate - catastrophic failure */
		else
		{
			_exit(1);
		}
	}
	/* parent process */
	else
	{
		/* wait for the child to end */
		waitpid(childID, NULL, 0);
		/* close the temporary file's file descriptor */
		close(redirectFD);
		
		/* use stat() to get all pertanant file info and check for success */
		if (stat(tempFileNameWithPID, &fileStat))
		{
			/* if stat fails the file is not found */
			error404_NotFound(newSocket);
			return;
		}
	
		/* get file size from the stat structure */
		fileSize = (fileStat.st_size);
		/* if the temp file has size zero, then exec() must have failed */
		if (fileSize == 0)
		{
			/* append a "./" to the temporary file's name so that it can be easily deleted */
			sprintf(fullPathFileName, "./%s", tempFileNameWithPID);
			/* delete the temporary file */
			remove(fullPathFileName);
			/* stop the parent process before sending a response */
			return;
		}
		
		/* construct the header to be sent as a response */
		sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", fileSize);
		/* send header and error check */
		if (send(newSocket, header, strlen(header), 0) == -1)
		{
			/* if the send failed, give 500 error to client */
			error500_InternalError(newSocket, "Error: send() failed.\n");
			/* print server info */
			fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
			fprintf(stderr, "%s\n", "[SERVER] Continuing...");
			fflush(stderr);
		}
		
		/* open a file stream of the temporary file */
		streamIN = fopen(tempFileNameWithPID, "r");
		
		/* get a line from the file using readLine() */
		contentBuffer = readLine(streamIN);
		/* each call to readline() will set contentBuffer[0] to NULL unless there is more to read */
		/* while there are more lines */
		while (contentBuffer[0])
		{
			/* send the line to the client and error check */
			if (send(newSocket, contentBuffer, strlen(contentBuffer), 0) == -1)
			{
				/* if the send failed, give 500 error to client */
				error500_InternalError(newSocket, "Error: send() failed.\n");
				/* print server info */
				fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
				fprintf(stderr, "%s\n", "[SERVER] Continuing...");
				fflush(stderr);
			}
			/* get the next line */
			contentBuffer = readLine(streamIN);
		}
		/* free the memory holding the buffer */
		free(contentBuffer);
	
		/* append a "./" to the temporary file's name so that it can be easily deleted */
		sprintf(fullPathFileName, "./%s", tempFileNameWithPID);
		/* delete the temporary file */
		remove(fullPathFileName);
		
		/* end this fufilment request */
		return;
	}
}

/* sends bad request error to client */
void error400_BadRequest(int newSocket)
{
	char header[512];
		
	sprintf(header, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", 0);
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Sent error: %s\n", "400 Bad Request");
	fflush(stderr);
}

/* sends permission denied error to client */
void error403_PermissionDenied(int newSocket)
{
	char header[512];
		
	sprintf(header, "HTTP/1.0 403 Permision Denied\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", 0);
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Sent error: %s\n", "403 Permision Denied");
	fflush(stderr);
}

/* sends not found error to client */
void error404_NotFound(int newSocket)
{
	char header[512];
		
	sprintf(header, "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", 0);
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Sent error: %s\n", "404 Not Found");
	fflush(stderr);
}

/* sends internal error to client */
void error500_InternalError(int newSocket, char* errorText)
{
	char header[512];

	sprintf(header, "HTTP/1.0 500 Internal Error\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", 0);
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Sent error: %s\n", "500 Internal Error");
	fflush(stderr);
	if (send(newSocket, errorText, strlen(errorText), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Error: %s\n", errorText);
	fflush(stderr);
}

/* sends not implemented error to client */
void error501_NotImplemented(int newSocket, char* errorText)
{
	char header[512];

	sprintf(header, "HTTP/1.0 501 Not Implemented\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", 0);
	if (send(newSocket, header, strlen(header), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Sent error: %s\n", "501 Not Implemented");
	fflush(stderr);
	if (send(newSocket, errorText, strlen(errorText), 0) == -1)
	{
		fprintf(stderr, "%s\n", "[SERVER] Error: send() failed.");
		fprintf(stderr, "%s\n", "[SERVER] Continuing...");
		fflush(stderr);
	}
	fprintf(stderr, "[SERVER] Error: %s\n", errorText);
	fflush(stderr);
}
