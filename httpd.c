
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 1024
#define PATH_MAX 512
#define METHOD_NAME 256
#define SMALL_BUFFER_SIZE 256

#define SERVER_STRING "Server: SimpleHTTPServer/0.1.0\r\n"

/* -------- Clinet -------- */
void receive_request(int);

/* -------- Server -------- */
void send_response(int, FILE*);

void send_header_error(int, const char*); // Internal Error 500
void send_header_failure(int); // Bad request 400
void send_header_success(int); // OK 200
void send_header_not_found(int); // Not found 404

void execute_cgi(int, const char*, const char*, const char*);
void serve_file(int, const char*);

void server_log(const char*);
void unimplemented(int);

int startup(u_short*);

/* -------- Thread handling -------- */
static void set_signal_mask()
{
  static sigset_t   signal_mask;
  pthread_t  sig_thr_id;      
  sigemptyset(&signal_mask);
  sigaddset (&signal_mask, SIGINT);
  sigaddset (&signal_mask, SIGTERM);
  sigaddset (&signal_mask, SIGUSR1);
  sigaddset (&signal_mask, SIGUSR2);
  pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
}

static void* handle_request_response(int client_sock)
{
  set_signal_mask();
  receive_request(client_sock);
  close(client_sock);
  pthread_exit(pthread_self);
}

/**********************************************************************/
/* This API process request from clinet. It parse HTTP Header, executes 
 * respective HTTP method and send response back to server.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void receive_request(int client)
{
  char buf[MAX_BUFFER_SIZE];
  char method[METHOD_NAME];
  char fileName[PATH_MAX];
  char resource_path[PATH_MAX];
  int i;
  struct stat st;
  int cgi = 0; /* becomes true if server decides this is a CGI program */

  char qString[MAX_BUFFER_SIZE];

  char requestBuffer[MAX_BUFFER_SIZE];
  memset(&requestBuffer, 0, sizeof(requestBuffer));
  int content_length = -1;

  int numbytes = 0;
  if ((numbytes = recv(client, requestBuffer, sizeof(requestBuffer), 0)) == -1) {
    server_log("Request is not received\n");
    return;
  }

  if (numbytes == 0) {
    server_log("No messages are available or peer has performed an orderly shutdown.\n");
    return;
  }

  requestBuffer[numbytes] = '\0';
  strcpy(buf, requestBuffer);

  memset(&method, 0, sizeof(method));
  char* token = NULL;
  token = strtok(requestBuffer, " ");
  strcpy(&method, token ? token : "");

  memset(&fileName, 0, sizeof(fileName));
  token =  strtok(NULL, " ");
  strcpy(&fileName, token ? token : "");

  char version[64];
  memset(&version, 0, sizeof(version));
  token = strtok(NULL, "\r\n");
  strcpy(&version, token ? token : "");

  if (!(strcasecmp(&method, "HEAD") || strcasecmp(&method, "GET") || strcasecmp(&method, "POST")))
  {
    unimplemented(client);
    return;
  }

  if (strcasecmp(&method, "HEAD") == 0)
  {
    cgi = 0;
    send_header_success(client);
    return;
  }
  else if (strcasecmp(&method, "GET") == 0 && strchr(fileName, '?'))
  {
    cgi = 1;
    char complete_fileName[MAX_BUFFER_SIZE];
    strcpy(complete_fileName, fileName);
    memset(&fileName, 0, sizeof(fileName));
    token = strtok(complete_fileName, "?");
    strcpy(&fileName, token ? token : "");
    memset(&qString, 0, sizeof(qString));
    token = strtok(NULL, "\r\n");
    strcpy(&qString, token ? token : "");
  }
  else if (strcasecmp(method, "POST") == 0)
  {
    char prev_token[MAX_BUFFER_SIZE];
    memset(&prev_token, 0, sizeof(prev_token));
    token = strtok(buf, "\r\n");
    while(token) {
      char* found = strstr(token, "Content-Length");
      if (found) {
        char curr = *(found + strlen("Content-Length: "));
        content_length = &curr ? atoi(&curr) : 0;
        if (content_length <= 0)
        {
          send_header_failure(client);
          return;
        }
      }

      cgi = 1;
      strcpy(&prev_token, token);
      token = strtok(NULL, "\r\n");

      if (token == NULL && strchr(prev_token, '=')) {
        memset(&qString, 0, sizeof(qString));
        strcpy(&qString, prev_token);
        break;
      }
    }
  }

  sprintf(resource_path, "htdocs%s", fileName);
  if (resource_path[strlen(resource_path) - 1] == '/')
    strcat(resource_path, "index.html");

  if (stat(resource_path, &st) == -1)
  {
    server_log("File Not found");
    send_header_not_found(client);
  }
  else
  {
    if ((st.st_mode & S_IFMT) == S_IFDIR)
      strcat(resource_path, "/index.html");

    /* If file is cgi and has 555 permission */
    if (strstr(resource_path,".cgi") || strstr(resource_path,".py"))
    {
      if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
      {
        cgi = 1;
      }
      else
      {
        cgi = 0;
        send_header_error(client, "Error prohibited CGI execution.");
        server_log("CGI file should have executable permission");
        return;
      } 
    }

    if (!cgi)
      serve_file(client, resource_path);
    else
      execute_cgi(client, resource_path, qString, method);
  }
  close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void send_header_failure(int client)
{
  char buf[MAX_BUFFER_SIZE];

  strcpy(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  strcat(buf, "Content-type: text/html\r\n");
  strcat(buf, "\r\n");
  strcat(buf, "<h1>Bad request<h1>\r\n");
  strcat(buf, "Your browser sent a bad request, such as a POST without a Content-Length.\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void send_response(int client, FILE *resource)
{
  char readbuf[MAX_BUFFER_SIZE];
  while(fgets(readbuf, sizeof(readbuf), resource) != NULL) {
    send(client, readbuf, strlen(readbuf), 0);
    memset(&readbuf, 0, sizeof(readbuf));
  }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor.
 *             Error message if any */
/**********************************************************************/
void send_header_error(int client, const char* err_msg)
{
  char buf[MAX_BUFFER_SIZE];

  strcpy(buf, "HTTP/1.0 500 Internal Server Error\r\n");
  strcat(buf, "Content-type: text/html\r\n");
  strcat(buf, "\r\n");
  strcat(buf, "<h1>500 Internal Server Error.</h1>\r\n");
  if (err_msg)
  {
    strcat(buf, err_msg);
    strcat(buf, "\r\n");  
  }
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() */
/**********************************************************************/
void server_log(const char *sc)
{
 perror(sc);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             resource_path to the CGI script
 *              query_string form client */
/**********************************************************************/
void execute_cgi(int client, const char* path, const char* query_string, const char* method)
{
  FILE *pipein_fp;
  char readbuf[MAX_BUFFER_SIZE];
  memset(&readbuf, 0, sizeof(readbuf));

  // Add enviorment variables
  char query_env[SMALL_BUFFER_SIZE];
  memset(query_env, 0, sizeof(query_env));
  sprintf(query_env, "QUERY_STRING=%s", query_string);
  putenv(query_env);

  char length_env[SMALL_BUFFER_SIZE];
  memset(length_env, 0, sizeof(length_env));
  sprintf(length_env, "CONTENT_LENGTH=%d", strlen(query_string));
  putenv(length_env);
#if 0
  char requestMethod_env[SMALL_BUFFER_SIZE];
  memset(requestMethod_env, 0, sizeof(requestMethod_env));
  sprintf(requestMethod_env, "REQUEST_METHOD=%s", method);
  putenv(requestMethod_env);
#endif
  if ((pipein_fp = popen(path, "r")) == NULL) {
    send_header_error(client, "Permission error.");
    server_log("popen");
    return;
  } else {
    send_header_success(client);
    send_response(client, pipein_fp);
    pclose(pipein_fp);
  }
  unsetenv("QUERY_STRING");
  unsetenv("CONTENT_LENGTH");
}

/**********************************************************************/
/* Return standard response for successful HTTP requests.
 * Parameters: the socket to print the headers on. */
/**********************************************************************/
void send_header_success(int client)
{
 char buf[MAX_BUFFER_SIZE];

 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 strcat(buf, SERVER_STRING);
 strcat(buf, "Content-Type: text/html\r\n");
 strcat(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Return standard response for requested resource could not be found but
*  may be available again in the future. Subsequent requests by the client are permissible.
*  Parameters: the socket to print the headers on. */
/**********************************************************************/
void send_header_not_found(int client)
{
 char buf[MAX_BUFFER_SIZE];

 strcpy(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 strcat(buf, SERVER_STRING);
 strcat(buf, "Content-Type: text/html\r\n");
 strcat(buf, "\r\n");
 strcat(buf, "<h1>404 NOT FOUND</h1>\r\n");
 strcat(buf, "Resource specified is unavailable or nonexistent.\r\n");
 strcat(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a requested resource file to the client ad response.
 * Report errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char* filename)
{
  FILE* resource = NULL;

  resource = fopen(filename, "r");
  if (resource == NULL) {
    send_header_not_found(client);
    server_log(client);
    return;
  }
  else
  {
    send_header_success(client);
    send_response(client, resource);
    fclose(resource);
  }
}

/**********************************************************************/
/* This function establish connection between server and clinet
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short* port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  server_log("socket");

 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);

 if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0)
  server_log("bind");

 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)
   server_log("getsockname");
  *port = ntohs(name.sin_port);
 }

 if (listen(httpd, 5) < 0)
  server_log("listen");

 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
  char buf[MAX_BUFFER_SIZE];
  memset(&buf, 0, sizeof(buf));
  strcat(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  strcat(buf, SERVER_STRING);
  strcat(buf, "Content-Type: text/html\r\n");
  strcat(buf, "\r\n");
  strcat(buf, "<html><head><title>Method Not Implemented\r\n");
  strcat(buf, "</title></head>\r\n");
  strcat(buf, "<body><p>HTTP request method not supported.</p>\r\n");
  strcat(buf, "</body></html>\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
int main(void)
{
 int server_sock = -1;
 u_short port = 0;
 int client_sock = -1;
 struct sockaddr_in client_name;
 int client_name_len = sizeof(client_name);

 server_sock = startup(&port);
 printf("httpd running on port %d\n", port);

 while (1)
 {
  client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);

  if (client_sock == -1)
   server_log("accept");

  pthread_t tid;
  if (pthread_create(&tid, NULL, handle_request_response, client_sock) == 0)
  {
      pthread_detach(tid);
  }
  else
  {
    send_header_error(client_sock, 0);
    server_log("Thread creation issue\n");
  }
 }

 close(server_sock);
 return(0);
}
