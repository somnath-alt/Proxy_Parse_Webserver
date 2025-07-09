#include"proxy_parse.h"
#include<stdio.h>
#include<string.h>
#include<time.h>
#include<pthread.h>
#include<semaphore.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/wait.h>
#include<errno.h>
#include<stdlib.h>
#include<netinet/in.h>




#define MAX_CLIENTS 10
#define MAX_BYTES 4096

typedef struct cashe_element cashe_element;
struct cashe_element{
    char* data;
    int len;
    char* url;
    time_t lru_track_cashe;
    cashe_element* next;
    
} cashe_element;

//Declairing functions

cashe_element* find(char* url);
int add_cashe_element(char*data,int size,char*url);
void remove_cache_element();

int port_number=8080;
pthread_t tid[MAX_CLIENTS];  //stored thread Id,s
sem_t semaphore;
pthread_mutex_t lock;

cashe_element* head;  //cashe list head defined
int cashe_size;

int connectRemoteServer(char *host_addr,int port_number){
  int remoteSocketId= socket(AF_INET,SOCK_STREAM,0);
  if(remote socketId<0){
    perror("Error in creating your Socket...\n");
    return -1;
    
  }
  struct hostent * host = gethostbyname(host_addr);
  if(host == NULL){
    perror("Error in getting host by name...\n");
    return -1;
  }
}

int handle_request(int client_socketId, ParsedRequest *request,char* tempReq){
  char *buf=(char*)malloc(MAX_BYTES*sizeof(char));
  strcpy(buf,"GET ");
  strcat(buf,request->path);
  strcat(buf ," ");
  strcat(buf, request->version);
  strcat(buf , "\r\n");
  size_t len = strlen(buf);

  if(ParsedHeader_set(request,"Connection","close")<0){
    printf("Set header key is not working...\n");
}

  if(ParsedHeader_get(request,"Host")==NULL){
    if(ParsedHeader_set(request,"Host",request->host)<0){
      printf("Host header is not set...\n");
    }
  }
  if(ParsedRequest_unparse_headers(request,buf+len,(size_t)MAX_BYTES-len)<0){
    printf("Unparsed Failed..\n");
  }

  int server_port = 80;
  if(request->port != NULL){
    server_port = atoi(request->port);
  }
  int remoteSocketId = connectRemoteServer(request->host,server_port);
}

void *thread_fn(void * socketNew){
  sem_wait(&semaphore);
  int p;
  sem_getvalue(&semaphore,p);
  printf("Semaphore Value is : %d\n",p);
  int *t= (int*) socketNew;
  int socket=*t;
  int bytes_send_client,len;

  char *buffer = (char*)calloc(MAX_BYTES,sizeof(char));

  bzero(buffer,MAX_BYTES);
  bytes_send_client=recv(socket,buffer,MAX_BYTES,0);

  while(bytes_send_client >0){
    len = strlen(buffer);
    if(strstr(buffer , "\r\n\r\n") == NULL){
      bytes_send_client =recv(socket ,buffer+len ,MAX_BYTES-len,0);
    }
    else{
      break;
    }
  }

  char *tempReq= (char *)malloc(strlen(buffer)*sizeof(char)+1);
  for(int i=0; i<strlen(buffer);i++){
    tempReq[i]=buffer[i];
  }
  tempReq[strlen(buffer)] = '\0'; // Null-terminate tempReq
  struct cashe_element * temp=find(tempReq);
  if(temp != NULL){
    int size = temp->len; // sizeof(char) is always 1
    int pos=0;
    char response[MAX_BYTES];

    // sending bytes of data over a Socket.
    while (pos < size) {
      memset(response, 0, MAX_BYTES); // Use memset for portability
      int chunk_size = (size - pos < MAX_BYTES) ? size - pos : MAX_BYTES;
      memcpy(response, temp->data + pos, chunk_size); // Copy actual data
      send(socket, response, chunk_size, 0);
      pos += chunk_size;
    }
    printf("Data is retrieved from the cache...\n");
    printf("%.*s\n\n", size, temp->data); // Print the cached data
    free(tempReq); // Free allocated memory

  }
  else if(bytes_send_client>0){
    len =strlen(buffer);
    ParsedRequest *request = ParsedRequest_create();

    if(ParsedRequest_parse(request, buffer, len)<0){
      printf("Parsing failed..\n");
    } else{
      bzero(buffer,MAX_BYTES);
      if(!strcmp(request->method,"GET")){
        if(request->host && request->path && checkHTTPSversion(request->version)==1){
          bytes_send_client =handle_request(socket,request,tempReq);
          if(bytes_send_client==-1){
            sendErrorMessage(socket,500);
          }
        }else{
          sendErrorMessage(socket,500);
        }
      }else{
        printf("Method is not GET...\n");
      }
    }
    ParsedRequest_destroy(request);
  }else if(bytes_send_client==0){
    printf("client is disconnected...\n");
  }
  shutdown(socket, SHUT_RDWR);
  close(socket);
  free(buffer);
  sem_post(&semaphore);
  sem_getvalue(&semaphore,p);
  printf("Semaphore Value is : %d\n",p);
  free(tempReq); //free allocated memory
  return NULL;
}

  

int main(int argc,char* argv[]){
    int client_socketId,client_len,proxy_socketId;
     struct sockaddr_in server_addr, client_addr;//address IDs of server and client
 server_addr,client_addr;
    sem_init(&semaphore,0,MAX_CLIENTS);
    pthread_mutex_init(&lock,NULL);
    if(argc==2){
        port_number=atoi(argv[1]);
    }
    else{
        printf("Too few arguements...\n");
        exit(1);
    }
  

    printf("Starting proxy server at port: %d\n",port_number);
    proxy_socketId= socket(AF_INET,SOCK_STREAM,0);    
      if(proxy_socketId<0){
        perror("Failed to create a socket\n");
        exit(1);
      }

      int reuse=1;
      if(setsockopt(proxy_socketId,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse))<0){
        perror("setSockOpt failed...");
      }

      bzero((char*)&server_addr,sizeof(server_addr));
      server_addr.sin_family=AF_INET;
      server_addr.sin_port=htons(port_number);
      server_addr.sin_addr.s_addr=INDDR_ANY;
      if(bind(proxy_socketId,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("Port is not available...");
        exit(1);
      }
      printf("Bindin on port %d\n",port_number);
      int listen_status=listen(proxy_socketId,MAX_CLIENTS);
      if(listen_status<0){
        perror("Error in listening..");
        exit(1);
      }
int i=0;
int connected_socketId[MAX_CLIENTS];

while(1){
  bzero((char *)&client_addr,sizeof(client_addr));
  client_len=sizeof(client_addr);
  client_socketId=accept(proxy_socketId,(struct sockaddr *)&client_addr,(socklen_t *)&client_len);
  if(client_socketId<0){
    printf("Not able to connect....");
    exit(1);
  }
  else{
    connected_socketId[i]=client_socketId;
  }
  /*This code processes the client’s address information (IP address and port) obtained from the 
  accept call in a socket-based server, 
  converts the IP address to a human-readable string, 
  and logs the connection details.*/

  struct sockaddr_in* client_pt=(struct sockaddr_in *)&client_addr;
  struct in_addr ip_addr = client_pt->sin_addr;
  char str[INDDR_ADDRSTRLEN];
  inet_ntop(AF_INET,&ip_addr,str,INDDR_ADDRSTRLEN);
  printf("Client is connected with port number %d and ip address is %s\n",ntohs(client_addr.sin_port),str);

  pthread_create(&tid[i], NULL, thread_fn, (void *)&connected_socketId[i]);
  i++;
}
close(proxy_socketId);
return 0;

}
