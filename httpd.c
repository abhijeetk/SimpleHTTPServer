
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
#include "request-response.h"

#define SERVER_STRING "Server: SimpleHTTPServer/0.1.0\r\n"

/* -------- Clinet -------- */
void receive_request(int);

/* -------- Server -------- */
void send_response(int, FILE *);

void send_header_error(int, const char*); // Internal Error 500
void send_header_failure(int); // Bad request 400
void send_header_success(int); // OK 200
void send_header_not_found(int); // Not found 404

void execute_cgi(int, const char *, const char *, const char *);
void serve_file(int, const char *);

void server_log(const char *);
void unimplemented(int);
void setEnviormentForCGI(char* query_string);

int startup(u_short *);

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
  printf("\n.....THREAD is exiting.....\n");
  pthread_exit(pthread_self);
}

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void receive_request(int client)
{
  char buf[1024];
  char method[255];
  char url[255];
  char path[512];
  struct stat st;
  int cgi = 0;      /* becomes true if server decides this is a CGI
                  * program */
  char *query_string = NULL;
  char qString[1024];

  char requestBuffer[1024];
  memset(&requestBuffer, 0, sizeof(requestBuffer));
  int content_length = -1;

  int numbytes = 100;

  if ((numbytes=recv(client, requestBuffer, sizeof(requestBuffer), 0)) == -1) {
    server_log("Request is not received\n");
    return;
  }

  if (numbytes == 0) {
    server_log("No messages are available or peer has performed an orderly shutdown.\n");
    return;
  }

  requestBuffer[numbytes] = '\0';
  printf("\n.......REQUEST......\n%s\n.....Size %d.....numbytes %d\n", requestBuffer, strlen(requestBuffer), numbytes);
  strcpy(buf, requestBuffer);
  memset(&method, 0, sizeof(method));
  char* token = NULL;
  token = strtok(requestBuffer, " ");
  strcpy(&method, token ? token : "");

  memset(&url, 0, sizeof(url));
  token =  strtok(NULL, " ");
  strcpy(&url, token ? token : "");

  char version[64];
  memset(&version, 0, sizeof(version));
  token = strtok(NULL, "\r\n");
  strcpy(&version, token ? token : "");

  printf("method -> %s url -> %s version -> %s\n", method, url, version);

  if (!(strcasecmp(&method, "HEAD") || strcasecmp(&method, "GET") || strcasecmp(&method, "POST")))
  {
    unimplemented(client);
    return;
  }

  if (strcasecmp(&method, "HEAD") == 0)
  {
    printf("...HEAD method...\n");
    send_header_success(client);
    return;
  }
  else if (strcasecmp(&method, "GET") == 0 && strchr(url, '?'))
  {
    cgi = 1;
    char complete_url[1024];
    strcpy(complete_url, url);
    printf("complete_url -> %s\n", complete_url);
    memset(&url, 0, sizeof(url));
    token = strtok(complete_url, "?");
    strcpy(&url, token ? token : "");
    printf("url -> %s\n", url);
    memset(&qString, 0, sizeof(qString));
    token = strtok(NULL, "\r\n");
    strcpy(&qString, token ? token : "");
    printf("method -> %s url -> %s qString -> %s\n", method, url, qString);
  }
  else if (strcasecmp(method, "POST") == 0)
  {
    char prev_token[1024];
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

  sprintf(path, "htdocs%s", url);
  printf("====> Path -> %s url -> %s\n", path, url);
  if (path[strlen(path) - 1] == '/')
    strcat(path, "index.html");

  printf("====> Path -> %s\n", path);
  if (stat(path, &st) == -1)
  {
    printf("File Not found %d %s\n", __LINE__, __FUNCTION__);
    send_header_not_found(client);
  }
  else
  {
    if ((st.st_mode & S_IFMT) == S_IFDIR)
      strcat(path, "/index.html");

    /* If file is cgi and has 555 permission */
    if (strstr(path,".cgi") || strstr(path,".py"))
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
      serve_file(client, path);
    else
      execute_cgi(client, path, method, &qString);
  }
  close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void send_header_failure(int client)
{
  char buf[1024];

  strcpy(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  strcat(buf, "Content-type: text/html\r\n");
  strcat(buf, "\r\n");
  strcat(buf, "<h1>Bad request<h1>\r\n");
  strcat(buf, "Your browser sent a bad request, such as a POST without a Content-Length.\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void send_response(int client, FILE *resource)
{
  char readbuf[1024];
  while(fgets(readbuf, sizeof(readbuf), resource) != NULL) {
    send(client, readbuf, strlen(readbuf), 0);
    memset(&readbuf, 0, sizeof(readbuf));
  }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void send_header_error(int client, const char* err_msg)
{
  char buf[1024];

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
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void server_log(const char *sc)
{
 perror(sc);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
  char buf[1024];
  int cgi_output[2];
  int cgi_input[2];
  pid_t pid;
  int status;
  int i;
  char c;
  int numchars = 1;

  printf("Inside execute_cgi %s\n", query_string);

#if 0
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);

  strcpy(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
#endif

  FILE *pipein_fp;
  char readbuf[1024];
  memset(&readbuf, 0, sizeof(readbuf));

  if ((pipein_fp = popen(path, "r")) == NULL) {
    send_header_error(client, "Permission error.");
    server_log("popen");
    return;
  } else {
    setEnviormentForCGI(query_string);
    send_header_success(client);

    /* Processing loop */
    printf("\n.........Response-1.........");
    send_response(client, pipein_fp);
    #if 0
    while(fgets(readbuf, sizeof(readbuf), pipein_fp) != NULL) {
    send(client, readbuf, strlen(readbuf), 0);
    memset(&readbuf, 0, sizeof(readbuf));
    }
    #endif
    printf("\n.........END Response.........\n"); 

    pclose(pipein_fp);
  }
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void send_header_success(int client)
{
 char buf[1024];
 //(void)filename;  /* could use filename to determine file type */

 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 strcat(buf, SERVER_STRING);
 strcat(buf, "Content-Type: text/html\r\n");
 strcat(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 printf(" HTTP 200 Header sent to browser/client\n");
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void send_header_not_found(int client)
{
 char buf[1024];

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
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
  FILE *resource = NULL;

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
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  server_log("socket");

 printf("1. Server socket is created. httpd running on port %d\n", *port);

 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);

 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  server_log("bind");

 printf("2. Server socket is bind with port number nad IP address\n");
 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   server_log("getsockname");
  *port = ntohs(name.sin_port);
 }

 if (listen(httpd, 5) < 0)
  server_log("listen");

 printf("3. Server socket is listning on port %d for connections\n", *port);
 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
  char buf[1024];
  memset(&buf, 0, sizeof(buf));
  strcat(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  strcat(buf, SERVER_STRING);
  strcat(buf, "Content-Type: text/html\r\n");
  strcat(buf, "\r\n");
  strcat(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
  strcat(buf, "</TITLE></HEAD>\r\n");
  strcat(buf, "<BODY><P>HTTP request method not supported.</P>\r\n");
  strcat(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

void setEnviormentForCGI(char* query_string)
{
  char query_env[255];
  char length_env[255];
  sprintf(query_env, "QUERY_STRING=%s", query_string);
  putenv(query_env);
  sprintf(length_env, "CONTENT_LENGTH=%d", strlen(query_string));
  putenv(length_env);
}


/**********************************************************************/

int main(void)
{
 int server_sock = -1;
 u_short port = 0;
 int client_sock = -1;
 struct sockaddr_in client_name;
 int client_name_len = sizeof(client_name);
 //pthread_t newthread;

 server_sock = startup(&port);
 printf("httpd running on port %d\n", port);

 while (1)
 {
  client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);

  if (client_sock == -1)
   server_log("accept");

  printf("4. Server accepted connection request form machine %s\n", inet_ntoa(client_name.sin_addr));
  pthread_t tid;
  if (pthread_create(&tid, NULL, handle_request_response, client_sock) == 0)
      pthread_detach(tid);
  else
    send_header_error(client_sock, 0);
  
  //handle_request_response(client_sock);
  //receive_request(client_sock);

 //if (pthread_create(&newthread , NULL, receive_request, client_sock) != 0)
 //perror("pthread_create");
 }

 close(server_sock);

 return(0);
}
