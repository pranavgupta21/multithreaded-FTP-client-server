/* check if sock_data needs to be closed as well
 * handling of invalid command response has not been done yet on client side
 */
#include "client_func.h"

/* Socket Variables */
extern int sock, sock_data, length;
extern struct sockaddr_in server, server_data;
extern struct hostent *hp, *hp_data;
extern char *hostname;
extern int optval;

/* Packet Variables */
extern char filename[BUF_SIZE], packet_str[PACK_SIZE], buffer[BUF_SIZE], command_str[BUF_SIZE + 10], server_curdir[DIR_LENGTH];
extern struct Packet packet;
extern struct Request *request;
extern short buffer_byte;

/* Command Variables */
extern struct Command command;

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void makeConnection(){
	/* creating socket */
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0)
		error("socket");

	server.sin_family = PF_INET;
	hp = gethostbyname(hostname);
	if(!hp)
		error("Unknown host");
	bcopy((char *)hp->h_addr, (char *)&server.sin_addr, hp->h_length);
	server.sin_port = htons(SERVER_PORT);
		
	if (connect(sock, (struct sockaddr *) &server, sizeof(server)) < 0)
		error("connect() failed");
}

void makeDataConnection(){
	/* creating socket */
	sock_data = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock_data < 0)
		error("socket");

	length = sizeof(server_data);
	bzero(&server_data,length);
	server_data.sin_family = PF_INET;
	server_data.sin_addr.s_addr = INADDR_ANY;
	server_data.sin_port = htons(DATA_PORT);
	
	/* bind socket to the port */
	if(bind(sock_data, (struct sockaddr *)&server_data, length) < 0) 
		error("binding");

	/* set the socket to listen */
	if (listen(sock_data, PACK_SIZE) < 0)
		error("listen() failed");
	
	fprintf(stdout, "makeDataConnection() : waiting for active data connection\n");
	//fflush(stdout);
	/* Accept data connection */
	if ((request->serverSocket = accept(sock_data,(struct sockaddr *)&request->from, &request->fromlen)) < 0)
		error("accept() failed");
}

/* parse_command_str : takes the raw command string, parses it and stores the command and the arguments in a Command (struct) */
void parse_command_str(char *command_str, struct Command& command){
	short byte_no = 0, buf_byte = 0;
	char command_name[10];
	
	// command_name //
	while(command_str[byte_no] != ' ' && command_str[byte_no] != '\0'){
		command.command_name[buf_byte++] = command_str[byte_no++];
	}
	command.command_name[buf_byte] = '\0';
	
	if(command_str[byte_no++] == '\0')
		return;
	
	// arguments //
	buf_byte = 0;
	while((command.arguments[buf_byte++] = command_str[byte_no++]) != '\0');
}

void parse_file_list(char *packet_file_list, char **file_list){
	short byte_no = 0, buf_byte = 0, file_no = 0;
	char filename[BUF_SIZE], ch = ' ';	
	
	do{
		ch = packet_file_list[byte_no];
		if(ch != '/' && ch != '\0')
			 filename[buf_byte] = ch;
		else{
			filename[buf_byte] = '\0';
			buf_byte = -1;
			file_list[file_no] = (char*)malloc(strlen(filename) + 1);
			strcpy(file_list[file_no], filename);
			file_no++;
		}
		byte_no++;
		buf_byte++;
	}while(ch != '\0');
}

int parse_file_list(char *file_list_str, char *packet_file_list){
	short byte_no = 0, buf_byte = 0, file_no = 0;
	char filename[BUF_SIZE], ch = ' ';	
	filename[0] = '\0';
	strcpy(packet_file_list, "");
	
	do{
		ch = file_list_str[byte_no];
		if(ch != ' ' && ch != '\0')
			 filename[buf_byte] = ch;
		else if(!(ch == ' ' && strlen(filename) && filename[buf_byte - 1] == '\\')){
			filename[buf_byte] = '\0';
			buf_byte = -1;
			strcat(packet_file_list, filename);
			strcat(packet_file_list, "/");
			filename[0] = '\0';
			file_no++;
		}
		else
			filename[buf_byte] = ch;

		byte_no++;
		buf_byte++;
	}while(ch != '\0');
	packet_file_list[byte_no-1] = '\0';
	return file_no;
}

void send_pack(enum commands command_code, char *pack_buffer, int sock_var){
	// prepare packet to be sent //
	strcpy(packet_str, "");
	ptos(0, command_code, pack_buffer, packet_str);
	
	// Send response //
	if (send(sock_var, packet_str, PACK_SIZE, 0) != PACK_SIZE)
		error("send() sent a different number of bytes than expected");
}

void send_data_port(){
	char data_port_str[5];
	sprintf(data_port_str, "%d", DATA_PORT);
	send_pack(PORT, data_port_str, sock);
}

void pwd(){
	// send command
	allzero(buffer, 1);
	send_pack(PWD, buffer, sock);

	// receive the server's response //
	if(recv(sock, packet_str, PACK_SIZE, 0) < 0){
		error("recv");
	}
	parse_packet(packet_str, packet);
	
	// take appropriate action
	if(packet.command_code == PWD){
		fprintf(stdout, "%s\n", packet.buffer);
		strcpy(server_curdir, packet.buffer);
	}
	else if(packet.command_code == INVALID){
		fprintf(stderr, "Not able to retrieve current working directory\n");
	}
}

void get(){
	// Send command //
	send_pack(GET, command.arguments, sock);

	// receive positive/negative acknowledgement (whether the file exists or not) //
	if(recv(sock, packet_str, PACK_SIZE, 0) < 0){
		error("recv");
	}
	
	parse_packet(packet_str, packet);
	if(!(int)packet.command_code){
		fprintf(stderr, "The specified does not exist\n");
		return;
	}
	
	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "get() : data connection accepted\n");
	
	// recieve file //
	receive_file();

	fprintf(stdout, "get() : complete file received\n");
	fflush(stdout);	
	// close the data connection //
	close(request->serverSocket);
}

void receive_file(){
	//open the file
	FILE *file_ptr;
	file_ptr = fopen(command.arguments, "w");
	while(recv(request->serverSocket, packet_str, PACK_SIZE, 0)){
		parse_packet(packet_str, packet);
		fputs(packet.buffer, file_ptr);			
	}
	fclose(file_ptr);
}

void mget(){
	int no_files = 0;
	char **file_list, packet_file_list[BUF_SIZE];
	fprintf(stdout, "mget() : argument list : %s\n", command.arguments);
	
	// parse the list of files //	
	no_files = parse_file_list(command.arguments, packet_file_list);
	fprintf(stdout, "mget() : packet_file_list : %s\n", packet_file_list);
	
	file_list = (char**)malloc(no_files*sizeof(char*));
	parse_file_list(packet_file_list, file_list);
	
	// Send command //
	send_pack(MGET, packet_file_list, sock);
	fprintf(stdout, "mget() : packet_str : %s\n", packet_str);

	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "get() : data connection accepted\n");

	// recieve files //
	receive_multiple_files(no_files, file_list);
	fprintf(stdout, "get() : files completely received\n");
	fflush(stdout);
	
	// close the data connection //
	close(request->serverSocket);	
}

void receive_multiple_files(int no_files, char **file_list){
	int file_no = 0, buffer_byte = 0, file_byte = 0;
	char ch = ' ';
	FILE *file_ptr;
	
	while(recv(request->serverSocket, packet_str, PACK_SIZE, 0)){
		parse_packet(packet_str, packet);
		strcpy(buffer, packet.buffer);
		while(buffer_byte < BUF_SIZE && buffer_byte < strlen(buffer)){
			ch = buffer[buffer_byte++];
			if(!file_byte++){
				if(ch == '0')
					fprintf(stdout, "receive_multiple_files() : file number %d : %s : File Not Found\n", file_no, file_list[file_no]);
				else{
					file_ptr = fopen(file_list[file_no], "w");
				}
			}
			
			else if(ch == EOF){
				fclose(file_ptr);
					fprintf(stdout, "receive_multiple_files() : file number %d : %s : Downloaded\n", file_no, file_list[file_no]);
				file_byte = 0;
				file_no++;
			}
			else
				fputc((int)ch, file_ptr);
		}
	}
	fflush(stdout);
}

void put(){
	strcpy(filename, command.arguments);
	fprintf(stdout, "put() : %s\n", filename);
	
	// check if the file exists //
	FILE *file_ptr = fopen(filename, "r");
	if(!file_ptr){
		fprintf(stderr, "The specified does not exist\n");
		return;
	}
	
	// Send command //
	send_pack(PUT, command.arguments, sock);
	fprintf(stdout, "%s\n", packet_str);

	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "put() : data connection accepted\n");
	
	// send file //
	sendFile(file_ptr);

	fprintf(stdout, "put() : complete file sent\n");
	fflush(stdout);
	
	//close the file
	fclose(file_ptr);
	
	// close the data connection //
	close(request->serverSocket);
}

void sendFile(FILE *file_ptr){
	int buf_byte = 0, ch = ' ';
	do{
		ch = fgetc(file_ptr);
		if(ch != EOF)
			buffer[buf_byte] = ch;
		else
			buffer[buf_byte] = '\0';
		
		buf_byte++;
		if(buf_byte == BUF_SIZE)
			buffer[buf_byte] = '\0';
		
		if(buf_byte == BUF_SIZE || ch==EOF){
			buf_byte = 0;
			// Send response //
			send_pack((enum commands)1, packet_str, request->serverSocket);
			fprintf(stdout, "sendFile() : file packet contents : %s\n", packet_str);
		}
	}while(ch != EOF);
}

void mput(){
	// Send command //
	allzero(buffer, 1);
	send_pack(MPUT, buffer, sock);
	fprintf(stdout, "mput() : packet_str : %s\n", packet_str);
			
	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "mput() : data connection accepted\n");

	// send files //
	sendMultipleFiles(command.arguments);
	fprintf(stdout, "mput() : files completely sent\n");
	fflush(stdout);
	
	// close the data connection //
	close(request->serverSocket);
	/*
	if(setsockopt(sock_data, SOL_SOCKET, SO_REUSEADDR, (void*)&optval, sizeof(int)) < 0){
		error("setsockopt") ;
	}
	*/
}

void sendMultipleFiles(char *file_list_str){
	short byte_no = 0, buf_byte = 0, file_no = 0, filename_byte = 0, buffer_byte = 0;
	char ch_file = ' ', filename[BUF_SIZE], ch = ' ', ch_filename = ' ';
	FILE *file_ptr;
	
	fprintf(stderr, "sendMultipleFiles() : file_list_str : %s\n", file_list_str);
	
	while(byte_no < strlen(file_list_str)){
		ch_filename = file_list_str[byte_no++];
		//fprintf(stdout, "byte_no : %d\tch_filename : %c\n", byte_no, ch_filename);
		if((ch_filename == ' ' && buf_byte && filename[buf_byte - 1] != '\\') || (byte_no == strlen(file_list_str))){
			if(byte_no == strlen(file_list_str)){
				filename[buf_byte++] = ch_filename;
			}
			filename[buf_byte++] = '\0';
			buf_byte = 0;
			
			fprintf(stdout, "sendMultipleFiles() : filename : %s\n", filename);
			for(filename_byte = 0; filename[filename_byte] != '\0'; filename_byte++){
				ch = filename[filename_byte];
				if(buffer_byte != BUF_SIZE - 1)
					buffer[buffer_byte++] = ch;
				else{
					buffer[buffer_byte] = '\0';
					buffer_byte = 0;
					fprintf(stdout, "send_mputw_files() : buffer : %s\n", buffer);
					send_pack((enum commands)1, buffer, request->serverSocket);
				}			
			}
			if(buffer_byte == BUF_SIZE - 1){
				buffer[buffer_byte] = '\0';
				buffer_byte = 0;
				fprintf(stdout, "send_mputw_files() : buffer : %s\n", buffer);
				send_pack((enum commands)1, buffer, request->serverSocket);
			}
			buffer[buffer_byte++] = '\t';

			file_ptr = fopen(filename, "r");
			if(!file_ptr){
				fprintf(stdout, "sendMultipleFiles() : %s : not found\n", filename);
			}
			else{
				fprintf(stdout, "sendMultipleFiles() : %s : started to buffer\n", filename);
				ch_file = ' ';
				do{
					if(buffer_byte != BUF_SIZE - 1){
						ch_file = fgetc(file_ptr);
						buffer[buffer_byte++] = ch_file;
					}
						
					if(buffer_byte == BUF_SIZE - 1){
						buffer[buffer_byte] = '\0';
						buffer_byte = 0;
						fprintf(stdout, "send_mputw_files() : buffer : %s\n", buffer);
						send_pack((enum commands)1, buffer, request->serverSocket);
					}
				}while(ch_file != EOF);
				fclose(file_ptr);
			}
		}
		else
			filename[buf_byte++] = ch_filename;

	}
	if(buffer_byte){
		buffer[buffer_byte++] = '\0';
		fprintf(stdout, "send_mputw_files() : buffer : %s\n", buffer);
		send_pack((enum commands)1, buffer, request->serverSocket);
	}
	fflush(stdout);
}

void mgetw(){
	// Send command //
	send_pack(MGETW, command.arguments, sock);
	fprintf(stdout, "%s\n", packet_str);
	
	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "mgetw() : data connection accepted\n");
	
	// recieve file //
	receive_mgetw_files();
	fprintf(stdout, "mgetw() : files completely received\n");
	fflush(stdout);	
	
	// close the data connection //
	close(request->serverSocket);
	//close(sock_data);
}

void receive_mgetw_files(){
	int file_no = 0, buffer_byte = 0, file_byte = 0, filename_byte = 0;
	char ch = ' ', filename[BUF_SIZE];
	FILE *file_ptr;
	
	strncpy(packet_str, "", PACK_SIZE);
	while(recv(request->serverSocket, packet_str, PACK_SIZE, 0)){
		parse_packet(packet_str, packet);
		strncpy(buffer, packet.buffer, BUF_SIZE);
		//fprintf(stdout, "receive_mgetw_files() : buffer : %s\n", buffer);
		buffer_byte = 0;
		while(buffer_byte < BUF_SIZE && buffer_byte < strlen(buffer)){
			ch = buffer[buffer_byte++];
			if(!file_byte){
				if(ch == '\t'){
					filename[filename_byte] = '\0';
					filename_byte = 0;
					fprintf(stderr, "receive_mgetw_files() : filename : %s\n", filename);
					file_ptr = fopen(filename, "w");
					file_byte++;
				}
				else{
					filename[filename_byte++] = ch;
				}
			}
			
			else{
				if(ch == EOF){
					fclose(file_ptr);
					fprintf(stdout, "receive_mgetw_files() : file number %d : %s : Downloaded\n", file_no, filename);
					file_byte = 0;
					file_no++;
				}
				else
					fputc((int)ch, file_ptr);
			}
		}
		strncpy(packet_str, "", PACK_SIZE);
	}
	fflush(stdout);	
}

void mputw(){
	strncpy(filename, command.arguments, BUF_SIZE);

	// Send command //
	strncpy(buffer, "", BUF_SIZE);
	send_pack(MPUTW, buffer, sock);

	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "mputw() : data connection accepted\n");
	
	// recieve file //
	send_mputw_files(filename);
	fprintf(stdout, "mputw() : files completely sent\n");
	fflush(stdout);	
	
	// close the data connection //
	close(request->serverSocket);
	//close(sock_data);
}

void send_mputw_files(char *w_pattern){
	short byte_no = 0, buf_byte = 0, file_no = 0, filename_byte = 0, buffer_byte = 0, last_file = 0;
	char baseDir[DIR_LENGTH], ch_file = ' ', filename[BUF_SIZE], ch = ' ';
	DIR *dp;
	struct dirent *dirp;
	struct stat isdir;
	FILE *file_ptr;
	
	strcpy(baseDir, ".");
	if((dp = opendir(baseDir)) == NULL){
		fprintf(stdout, "Couldn't open %s\n", baseDir);
		return;
	}
	while((dirp = readdir(dp)) != NULL){
		if ( !strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, "..") || (!stat(dirp->d_name, &isdir) && (isdir.st_mode & S_IFDIR)) || fnmatch(w_pattern, dirp->d_name, FNM_FILE_NAME) != 0){
			fprintf(stdout, "send_mputw_files() : not match : %s\n", dirp->d_name);
			continue;
		}
		
		strncpy(filename, dirp->d_name, BUF_SIZE);
		fprintf(stdout, "send_mputw_files() : filename : %s\n", filename);
		for(filename_byte = 0; filename[filename_byte] != '\0'; filename_byte++){
			ch = filename[filename_byte];
			if(buffer_byte != BUF_SIZE - 1)
				buffer[buffer_byte++] = ch;
			else{
				buffer[buffer_byte] = '\0';
				buffer_byte = 0;
				fprintf(stdout, "send_mputw_files() : buffer : %s\n", buffer);
				send_pack((enum commands)1, buffer, request->serverSocket);
			}			
		}
		if(buffer_byte == BUF_SIZE - 1){
			buffer[buffer_byte] = '\0';
			buffer_byte = 0;
			send_pack((enum commands)1, buffer, request->serverSocket);
		}
		buffer[buffer_byte++] = '\t';

		file_ptr = fopen(filename, "r");
		ch_file = ' ';
		do{
			if(buffer_byte != BUF_SIZE - 1){
				ch_file = fgetc(file_ptr);
				buffer[buffer_byte++] = ch_file;
			}
						
			if(buffer_byte == BUF_SIZE - 1){
				buffer[buffer_byte] = '\0';
				buffer_byte = 0;
				send_pack((enum commands)1, buffer, request->serverSocket);
			}
		}while(ch_file != EOF);
		fclose(file_ptr);
	}
	if(buffer_byte){
		buffer[buffer_byte] = '\0';
		send_pack((enum commands)1, buffer, request->serverSocket);
	}
}

void rgetdir(){
	// prepare packet to be sent //
	send_pack(RGETDIR, command.arguments, sock);

	// receive positive/negative acknowledgement (whether the file exists or not) //
	strncpy(packet_str, "", PACK_SIZE);
	if(recv(sock, packet_str, PACK_SIZE, 0) < 0){
		error("recv");
	}
	
	parse_packet(packet_str, packet);
	if(packet.command_code == INVALID){
		fprintf(stderr, "The specified folder does not exist\n");
		return;
	}
	
	mkdir(command.arguments, S_IRWXU | S_IRWXG | S_IRWXO);
	
	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "rgetdir() : data connection accepted\n");
	
	// recieve files //
	receive_rgetdir_files();
	fprintf(stdout, "rgetdir() : files completely received\n");
	fflush(stdout);	
	
	// close the data connection //
	close(request->serverSocket);
	//close(sock_data);
}

void receive_rgetdir_files(){
	int file_no = 0, buffer_byte = 0, file_byte = 0, filename_byte = 0, file_dir = 1;
	char ch = ' ', filename[BUF_SIZE];
	FILE *file_ptr;
	
	strncpy(filename, "", BUF_SIZE);
	strncpy(packet_str, "", PACK_SIZE);
	while(recv(request->serverSocket, packet_str, PACK_SIZE, 0)){
		fprintf(stderr, "receive_rgetdir_files() : new packet\n");		
		//fprintf(stderr, "receive_rgetdir_files() : packet_str : %s\n", packet_str);
		//strncpy(packet.buffer, "", BUF_SIZE);
		parse_packet(packet_str, packet);
		//memcpy(buffer, packet.buffer, 2048);
		strncpy(buffer, packet.buffer, BUF_SIZE);
		//fprintf(stdout, "receive_rgetdir_files() : buffer : %s\n", packet.buffer);
		fprintf(stderr, "receive_rgetdir_files() : buffer : %s\n", buffer);
		buffer_byte = 0;
		while(buffer_byte < BUF_SIZE && buffer_byte < strlen(buffer)){
			ch = buffer[buffer_byte++];
			if(!file_byte){
				if(strcmp(filename, "") != 0 && !filename_byte){
					file_dir = ch - '0';
					fprintf(stdout, "receive_rgetdir_files() : file type : %d\n", file_dir);
					if(!file_dir){
						file_ptr = fopen(filename, "w");
						file_byte++;
					}
					else{
						mkdir(filename, S_IRWXU | S_IRWXG | S_IRWXO);
						strncpy(filename, "", BUF_SIZE);
					}
					
				}
				else if(ch == '\t'){
					filename[filename_byte] = '\0';
					filename_byte = 0;
					fprintf(stderr, "receive_rgetdir_files() : filename : %s\n", filename);
				}
				else{
					filename[filename_byte++] = ch;
				}
			}
			
			else{
				if(ch == EOF){
					fclose(file_ptr);
					fprintf(stdout, "receive_rgetdir_files() : file number %d : %s : Downloaded\n", file_no, filename);
					strncpy(filename, "", BUF_SIZE);
					file_byte = 0;
					file_no++;
				}
				else{
					fputc((int)ch, file_ptr);
				}
			}
		}
		fprintf(stderr, "receive_rgetdir_files() : buffer done\n");
		strncpy(packet_str, "", PACK_SIZE);
	}
	fflush(stdout);
	fprintf(stderr, "receive_rgetdir_files() : complete\n");
}

void rputdir(){
	DIR *dp = opendir(command.arguments);
	allzero(buffer, 1);
	if(!dp){
		fprintf(stdout, "The specified folder does not exist\n");
		return;
	}
	closedir(dp);
	
	// send command //
	send_pack(RPUTDIR, command.arguments, sock);

	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "rputdir() : data connection accepted\n");
	
	// recieve file //
	send_rputdir_files(command.arguments);
	fprintf(stdout, "rputdir() : files completely sent\n");
	fflush(stdout);	
	
	// close the data connection //
	close(request->serverSocket);
	//close(sock_data);
}

void send_rputdir_files(char *baseDir){
	short filename_byte = 0, file_dir = 1;
	char filename[BUF_SIZE], ch_file = ' ', ch = ' ';
	struct dirent *dirp;
	struct stat isdir;
	FILE *file_ptr;
	DIR *dp = opendir(baseDir);
	
	while((dirp = readdir(dp)) != NULL){
		if(strcmp(dirp->d_name, "..") != 0 && strcmp(dirp->d_name, ".") != 0){
			strncpy(filename, baseDir, BUF_SIZE);
			strcat(filename, "/");
			strcat(filename, dirp->d_name);
			file_dir = !stat(filename, &isdir) && (isdir.st_mode & S_IFDIR) ? 1 : 0;
						
			fprintf(stdout, "send_rputdir_files() : filename : %s\n", filename);
			for(filename_byte = 0; filename[filename_byte] != '\0'; filename_byte++){
				ch = filename[filename_byte];
				if(buffer_byte != BUF_SIZE - 1)
					buffer[buffer_byte++] = ch;
				else{
					buffer[buffer_byte] = '\0';
					buffer_byte = 0;
					fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
					send_pack((enum commands)1, buffer, request->serverSocket);
				}			
			}
			//fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
			if(buffer_byte == BUF_SIZE - 1){
				buffer[buffer_byte] = '\0';
				buffer_byte = 0;
				fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
				send_pack((enum commands)1, buffer, request->serverSocket);
			}
			buffer[buffer_byte++] = '\t';
			if(buffer_byte == BUF_SIZE - 1){
				buffer[buffer_byte] = '\0';
				buffer_byte = 0;
				fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
				send_pack((enum commands)1, buffer, request->serverSocket);
			}
			buffer[buffer_byte++] = (char)(file_dir + '0');
			
			if(!file_dir){
				file_ptr = fopen(filename, "r");
				ch_file = ' ';
				do{
					if(buffer_byte != BUF_SIZE - 1){
						ch_file = fgetc(file_ptr);
						buffer[buffer_byte++] = ch_file;
					}
						
					if(buffer_byte == BUF_SIZE - 1){
						buffer[buffer_byte] = '\0';
						buffer_byte = 0;
						fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
						send_pack((enum commands)1, buffer, request->serverSocket);
					}
				}while(ch_file != EOF);
				fclose(file_ptr);
			}
			else{
				send_rputdir_files(filename);
			}
		}
	}
	
	buffer[buffer_byte] = '\0';
	fprintf(stdout, "send_rputdir_files() : buffer : %s\n", buffer);
	send_pack((enum commands)1, buffer, request->serverSocket);
	
	closedir(dp);
}

void exitc(){
	close(sock);
	exit(0);
}

void cd(){
	// Send command //
	send_pack(CD, command.arguments, sock);
	fprintf(stdout, "cd() : packet_str : %s : packet_str len : %d\n", packet_str, strlen(packet_str));
	
	// receive the server's response //
	strncpy(packet_str, "", PACK_SIZE);
	if(recv(sock, packet_str, PACK_SIZE, 0) < 0){
		error("recv");
	}
	parse_packet(packet_str, packet);

	// take appropriate action
	if(packet.command_code == INVALID){
		fprintf(stderr, "Not able to change to the specified directory\n");
	}
	else /*if(packet.command_code == CD)*/{
		fprintf(stdout, "%s\n", packet.buffer);
		strcpy(server_curdir, packet.buffer);
	}
	fflush(stdout);
}

void lcd(){
	if (chdir(command.arguments) != 0){
		fprintf(stderr, "Error Changing Directory\n");
	}
}

void dir(){
	// send command //
	allzero(buffer, 1);
	send_pack(DIRL, buffer, sock);
		
	/* send port number for data connection to server */
	send_data_port();
	
	// make socket and accept server's Data Connection (active)//
	makeDataConnection();
	fprintf(stderr, "get() : data connection accepted\n");
	
	// recieve file list //
	receive_file_list();
	fprintf(stdout, "get() : complete file received\n");

	// close the data connection //
	close(request->serverSocket);
}

void receive_file_list(){
	int buffer_byte = 0, filename_byte = 0;
	char ch = ' ';
	
	while(recv(request->serverSocket, packet_str, PACK_SIZE, 0)){
		parse_packet(packet_str, packet);
		strcpy(buffer, packet.buffer);
		while(buffer_byte < BUF_SIZE && buffer_byte < strlen(buffer)){
			ch = buffer[buffer_byte++];
			if(ch == '\t'){
				filename[filename_byte++] = '\0';
				fprintf(stdout, "%s\n", filename);
				filename_byte = 0;
			}
			else{
				filename[filename_byte++] = ch;
			}
		}
	}
	fflush(stdout);
}

void ldir(){
	DIR *dp;
	struct dirent *dirp;
	char baseDir[DIR_LENGTH];
	strcpy(baseDir, ".");
	struct stat isdir;
	
	if((dp = opendir(baseDir)) == NULL){
		fprintf(stdout, "Couldn't open %s\n", baseDir);
		return;
	}
	else{
		while((dirp = readdir(dp)) != NULL){
			if (strcmp(dirp->d_name, ".") && strcmp(dirp->d_name, "..")){
				if (!stat(dirp->d_name, &isdir) && (isdir.st_mode & S_IFDIR)){
						fprintf(stdout, "[D] %s\n", dirp->d_name);
				}
				else{
						fprintf(stdout, "[F] %s\n", dirp->d_name);
				}
			}
			fflush(stdout);
		}
		closedir(dp);
	}	
}

