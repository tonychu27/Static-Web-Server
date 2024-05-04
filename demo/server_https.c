#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <locale.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define errquit(m)	{ perror(m); exit(-1); }
#define BUFFERSIZE 14000
#define PROCESS 9000

struct ClientInfo{
    int socket;
    SSL_CTX* ssl_context;
    SSL* ssl_connection;
};


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

int createServerSocket(int port, SSL_CTX* ssl_ctx) {
    int server_fd;
    struct sockaddr_in sin;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) errquit("Socket");

    int v = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));

    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "Error binding to port %d\n", port);
        errquit("Bind");
    }

    if (port == 443) {
        // HTTPS config
        if (listen(server_fd, SOMAXCONN) < 0) errquit("Listen");

        return server_fd;
    } 
    else {
        // HTTP config
        if (listen(server_fd, SOMAXCONN) < 0) errquit("Listen");

        return server_fd;
    }
}

int main() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // Create SSL context for HTTPS
    SSL_CTX *https_ssl_context = SSL_CTX_new(SSLv23_server_method());
    if(!https_ssl_context) errquit("SSL_CTX_new");

    // Load server certificate and private key
    if (SSL_CTX_use_certificate_file(https_ssl_context, "/cert/server.crt", SSL_FILETYPE_PEM) <= 0 || SSL_CTX_use_PrivateKey_file(https_ssl_context, "/cert/server.key", SSL_FILETYPE_PEM) <= 0) errquit("SSL_CTX_use_certificate_file / SSL_CTX_use_PrivateKey_file");

    int httpFd = createServerSocket(80, NULL);
    int httpsFd = createServerSocket(443, https_ssl_context);

    for(int i = 0; i < PROCESS; i++) {
        pid_t pid = fork();

        if(pid == -1) { 
            errquit("fork");
        }
        else if(pid == 0) {
            do {
                int c;
                struct sockaddr_in csin; 
                socklen_t csinlen = sizeof(csin);

                if((c = accept(httpsFd, (struct sockaddr*) &csin, &csinlen)) < 0) {
					errquit("accept");
					break;
				}
                SSL *ssl_connection = SSL_new(https_ssl_context);
                SSL_set_fd(ssl_connection, c);

                if (SSL_accept(ssl_connection) <= 0) {
                    ERR_print_errors_fp(stderr);
                    close(c);
                    SSL_free(ssl_connection);
                    continue;
                }
                
                struct ClientInfo client_info = {.socket = c, .ssl_connection = ssl_connection, .ssl_context = https_ssl_context};
                char request_buffer[BUFFERSIZE];
                size_t bytes_received = SSL_read(client_info.ssl_connection, request_buffer, sizeof(request_buffer));
                if(bytes_received < 0) errquit("SSL Read");

				handleRequest(c);

                if (SSL_shutdown(ssl_connection) == 0) SSL_shutdown(ssl_connection);
                SSL_free(ssl_connection);

                close(c);

            } while(1);

            exit(EXIT_SUCCESS);
        }
    }

    for(int i = 0; i < PROCESS; i++) wait(NULL);

    return 0;
}