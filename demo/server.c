#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <ctype.h>
#include <sys/wait.h>

#define errquit(m)	{ perror(m); exit(-1); }
#define BUFFERSIZE 14000
#define PROCESS 10000

static int port_http = 10802;
static int port_https = 10841;
static const char *docroot = "/html";

void discard(char *str) {
    char *questionMarkPos = strstr(str, "/?");

    if (questionMarkPos != NULL) {
        *questionMarkPos = '\0';
    }
}

char* urlDecode(char* encoded) {
    char* decoded = malloc(strlen(encoded) + 1);
    int i, j = 0;

    for (i = 0; encoded[i] != '\0'; ++i) {
        if (encoded[i] == '%' && encoded[i + 1] != '\0' && encoded[i + 2] != '\0') {
            int hex1, hex2;
            sscanf(encoded + i + 1, "%2x", &hex1);
            sscanf(encoded + i + 3, "%2x", &hex2);
            decoded[j++] = hex1;
            i += 2;
        } else {
            decoded[j++] = encoded[i];
        }
    }

    decoded[j] = '\0';

    return decoded;
}


int checkTrail(char *path) {
	for (int i = strlen(path) - 1; i >= 0; i--)
		if(path[i]=='.')
			return 1;

	return 0;
}

void handleRequest(int clientSocket) {
	char buffer[BUFFERSIZE];
	size_t bytesRead;

	bytesRead = read(clientSocket, buffer, BUFFERSIZE);
	if(bytesRead == -1)	
		errquit("Error reading from socket");

	buffer[bytesRead] = '\0';

	char method[10], p[255];
	sscanf(buffer, "%s %s", method, p);

	char *path = urlDecode(p);

	if(strcmp(method, "GET")) {
		const char* res = "HTTP/1.0 501 Not Implemented\r\n\r\n";
		send(clientSocket, res, strlen(res), 0);
		return;
	}

	if(checkTrail(path))
		discard(path);
		
	strtok(path, "?");

	char fullPath[300];
	snprintf(fullPath, sizeof(fullPath), "%s%s", docroot, path);

	int pathLen = strlen(fullPath);

	if(!checkTrail(fullPath) && fullPath[pathLen - 1] != '/') {
		char newPath[BUFFERSIZE], temp[BUFFERSIZE];
		strcpy(newPath, path);
		strcpy(temp, fullPath);
		strcat(newPath, "/");
		strcat(temp, "/");

		int exist = open(temp, O_RDONLY);
		if(exist != -1) {
			char res[BUFFERSIZE];
			snprintf(res, sizeof(res), "HTTP/1.0 301 Move Permanently\r\nLocation: %s\r\n\r\n", newPath);
			send(clientSocket, res, strlen(res), 0);
			return; 
		}
	}

	struct stat fileStat;
	if(stat(fullPath, &fileStat) == 0 && S_ISDIR(fileStat.st_mode)) {
		snprintf(fullPath, sizeof(fullPath), "%s%s/index.html", docroot, path);

		if(access(fullPath, F_OK) == -1) {
			const char* res = "HTTP/1.0 403 Forbidden\r\nContent-Type: text/html;\r\n\r\n";
			send(clientSocket, res, strlen(res), 0);

			int f = open("/html/Forbidden.html", O_RDONLY);
			char mes[BUFFERSIZE];
			int r = 0;
			while((r = read(f, mes, sizeof(mes))) > 0)
				write(clientSocket, mes, r);

			return;
		}
	}

	int fileID = open(fullPath, O_RDONLY);
	if(fileID == -1) {
		const char* res = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html;\r\n\r\n";
		send(clientSocket, res, strlen(res), 0);

		int f = open("/html/NotFound.html", O_RDONLY);
		char mes[BUFFERSIZE];
		int r = 0;
		while((r = read(f, mes, sizeof(mes))) > 0)
			write(clientSocket, mes, r);

		return;
	}

	const char* mimeType;
	const char* fileExtension = strrchr(fullPath, '.');
	if(fileExtension && (!strcmp(fileExtension, ".html") || !strcmp(fileExtension, ".txt"))) 
		mimeType = "text/html";
	else if(fileExtension && (!strcmp(fileExtension, ".jpg") || !strcmp(fileExtension, ".png") || !strcmp(fileExtension, ".gif")))
		mimeType = "image/jpge";	
	else if(fileExtension && !strcmp(fileExtension, ".mp3"))
		mimeType = "audio/mpeg";
	else {
		const char* res = "HTTP/1.0 501 Not Implemented\r\n\r\n";
		send(clientSocket, res, strlen(res), 0);
		close(fileID);
		return;
	}

	const char* res = "HTTP/1.0 200 OK\r\nContent-Type: ; charset=utf-8\r\n\r\n";
	send(clientSocket, res, strlen(res), 0);
	
	while((bytesRead = read(fileID, buffer, sizeof(buffer))) > 0)
		write(clientSocket, buffer, bytesRead);

	close(fileID);

}

void handleClient(int clientSocket) {
	handleRequest(clientSocket);

	close(clientSocket);
}

int main(int argc, char *argv[]) {
	
	int s;
	struct sockaddr_in sin;

	if(argc > 1) { port_http  = strtol(argv[1], NULL, 0); }
	if(argc > 2) { if((docroot = strdup(argv[2])) == NULL) errquit("strdup"); }
	if(argc > 3) { port_https = strtol(argv[3], NULL, 0); }

	if((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) 
		errquit("socket");

	do {
		int v = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
	} while(0);

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(80);
	if(bind(s, (struct sockaddr*) &sin, sizeof(sin)) < 0) errquit("bind");
	if(listen(s, SOMAXCONN) < 0) errquit("listen");

	for (int i = 0; i < PROCESS; i++) {
	
		pid_t pid = fork();

		if(pid == -1) {
			errquit("Error process");
		}
		else if(pid == 0) {
			
			do {
				int c;
				struct sockaddr_in csin;
				socklen_t csinlen = sizeof(csin);

				if((c = accept(s, (struct sockaddr*) &csin, &csinlen)) < 0) {
					errquit("accept");
					break;
				}

				handleClient(c);

			} while(1);

			exit(EXIT_SUCCESS);
		}
	}

	for (int i = 0; i < PROCESS; i++) {
        wait(NULL);
    }

	return 0;
}
