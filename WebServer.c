#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>

#define BACKLOG 10
#define MAX_THREADS 128
#define REQ_SLEEP_DUR 3
#define THRD_SLEEP_DUR 1

/*
Selecting a port that is currently not in use in njit afs servers:

netstat -an --tcp | awk '/LISTEN/ {sub(".*:", "", $4); print $4}' | sort -nu

Testing if port number specified is not available error message works:

`netstat -an --tcp | awk '/LISTEN/ {sub(".*:", "", $4); print $4}' | sort -nu |tail -1
*/

const char* HTTP_200_STRING = "OK";
const char* HTTP_404_STRING = "Not Found";
const char* HTTP_501_STRING = "Not Implemented";
//html or htm extension
const char* chtml = "Content-Type: text/html\r\n\r\n";
//css extension
const char* ccss = "Content-Type: text/css\r\n\r\n";
//jpg extension
const char* cjpg = "Content-Type: image/jpeg\r\n\r\n";
//png extension
const char* cpng = "Content-Type: image/png\r\n\r\n";
//gif extension
const char* cgif = "Content-Type: image/gif\r\n\r\n";
//Any other extensions
const char* cplain = "Content-Type: text/plain\r\n\r\n";
const char* HTTP_404_CONTENT = "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1>The requested resource could not be found but may be available again in the future.<div style=\"color: #eeeeee; font-size: 8pt;\">Actually, it probably won't ever be available unless this is showing up because of a bug in your program. :(</div></html>";const char* HTTP_501_CONTENT = "<html><head><title>501 Not Implemented</title></head><body><h1>501 Not Implemented</h1>The server either does not recognise the request method, or it lacks the ability to fulfill the request.</body></html>";

struct sockaddr_in myAddr;
struct sockaddr_in theirAddr;
int sockfd;
int connfd;
int DEBUG = 0;

void * connectSock(void* threadId){
  int keepAlive = 0;
  do{
    char recvBuf[BUFSIZ];
    if((recv(connfd, recvBuf, BUFSIZ-1, 0)) == -1){
      perror("ERROR: recv");
      return;
    }
    //It is safe for you to use a buffer of 2KB
    char* httpHead = strtok(recvBuf, "\r\n\r\n");
    if(DEBUG){
      fprintf(stderr, httpHead);
      fprintf(stderr, "\n");
    }
    char* headTok = strtok(httpHead, " \r\n");
    char url[127];
    strcpy(url, "web");
    int httpStatCode;
    if(strcmp(headTok, "GET")) httpStatCode = 501;
    headTok = strtok(NULL, " \r\n");
    if(headTok[0] != '/') httpStatCode = 501;
    strcat(url, headTok);
    if(headTok[strlen(headTok)-1] == '/') strcat(url, "index.html");
    if(httpStatCode != 501)
      while((headTok = strtok(NULL, " \r\n")) != NULL){
        if(!strcmp(headTok, "Connection:")){
          headTok = strtok(NULL, " \r\n");
          if(!strcasecmp(headTok, "Keep-Alive")) keepAlive = 1;
          break;
        }
      }
    char response[BUFSIZ];
    char respSize[16];
    strcpy(response, "HTTP/1.0 ");
    int fd;
    int fileSize;
    if(httpStatCode == 501){
      keepAlive = 0;
      strcat(response, "501 ");
      strcat(response, HTTP_501_STRING);
      strcat(response, "\r\n");
      strcat(response, "Connection: close\r\n");
      strcat(response, "Content-Length: ");
      sprintf(respSize, "%d", strlen(HTTP_501_CONTENT));
      strcat(response, respSize);
      strcat(response, "\r\n");
      strcat(response, chtml);
      strcat(response, HTTP_501_CONTENT);
    }
    else{
      if(DEBUG){
        fprintf(stderr, url);
        fprintf(stderr, "\n");
      }
      if((fd = open(url, O_RDONLY)) == -1){
        httpStatCode = 404;
        keepAlive = 0;
        strcat(response, "404 ");
        strcat(response, HTTP_404_STRING);
        strcat(response, "\r\n");
        strcat(response, "Connection: close\r\n");
        strcat(response, "Content-Length: ");
        sprintf(respSize, "%d", strlen(HTTP_404_CONTENT));
        strcat(response, respSize);
        strcat(response, "\r\n");
        strcat(response, chtml);
        strcat(response, HTTP_404_CONTENT);
      }
      else{
        char* fileType = strtok(url, ".");
        fileType = strtok(NULL, ".");
        httpStatCode = 200;
        struct stat fileStat;
        fstat(fd, &fileStat);
        fileSize = fileStat.st_size;
        sprintf(respSize, "%d", fileSize);
        strcat(response, "200 ");
        strcat(response, HTTP_200_STRING);
        strcat(response, "\r\n");
        if(keepAlive) strcat(response, "Connection: Keep-Alive\r\n");
        else strcat(response, "Connection: close\r\n");
        strcat(response, "Content-Length: ");
        strcat(response, respSize);
        strcat(response, "\r\n");
        if(!strcmp(fileType, "html")) strcat(response, chtml);
        else if(!strcmp(fileType, "css")) strcat(response, ccss);
        else if(!strcmp(fileType, "jpg")) strcat(response, cjpg);
        else if(!strcmp(fileType, "png")) strcat(response, cpng);
        else if(!strcmp(fileType, "gif")) strcat(response, cgif);
        else strcat(response, cplain);
      }
    }
    send(connfd, response, strlen(response), 0);
    int charsRead;
    char fc[2048];
    int nr;
    if(httpStatCode == 200){
      while(charsRead < fileSize){
        if((nr = read(fd, &fc, sizeof(fc))) == -1){
          perror("ERROR: read");
          close(connfd);
          return;
        }
        if(DEBUG){
          fprintf(stderr, url);
          fprintf(stderr, "/nr: %d\n", nr);
        }
        send(connfd, fc, nr, 0);
        charsRead += nr;
        if(DEBUG) fprintf(stderr, "charsRead/fileSize: %d/%d\n", charsRead, fileSize);
      }
      close(fd);
    }
    sleep(REQ_SLEEP_DUR);
  }while(keepAlive);
  close(connfd);
  return;
}

int main(int argc, char* argv[]){
  int opt;
  while((opt = getopt(argc, argv, ":")) != -1){
    switch(opt){
      case '?':
        printf("ERROR: invalid argument\n");
        break;
    }
  }
  int portNumber;
  if(argc != 2){
    printf("Usage: webServer portNumber\n");
    return -1;
  }
  else portNumber = atoi(argv[optind]);

  myAddr.sin_family = AF_INET;
  myAddr.sin_port = htons(portNumber);
  myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  memset(&(myAddr.sin_zero), 0, 8);

  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    perror("ERROR: socket");
    return 1;
  }
  if((bind(sockfd, (struct sockaddr*)&myAddr, sizeof(struct sockaddr))) == -1){
    perror("ERROR: bind");
    return 1;
  }
  if((listen(sockfd, BACKLOG)) == -1){
    perror("ERROR: listen");
    return 1;
  }

  pthread_t thread[MAX_THREADS];
  int threadRet;
  int threadNum;
  for(;;){
    int sin_size = sizeof(struct sockaddr_in);
    if((connfd = accept(sockfd, (struct sockaddr*)&theirAddr, &sin_size)) == -1){
      perror("ERROR: accept");
      continue;
    }
    if((threadRet = pthread_create(thread+(threadNum++), NULL, connectSock, &threadNum)) != 0){
      perror("ERROR: pthread_create");
      return 1;
    }
    if(DEBUG) fprintf(stderr, "pthreadNum: %d\n", threadNum);
    sleep(THRD_SLEEP_DUR);
    //Might be able to fix bad file descriptor error by sleeping after recv call
    //in the connectSock function
  }

  int i;
  for(i = 0; i < threadNum; i++){
    if((threadRet = pthread_join(thread[i], NULL)) != 0){
      perror("ERROR: pthread_join");
      return 1;
    }
  }
  return 0;
}
