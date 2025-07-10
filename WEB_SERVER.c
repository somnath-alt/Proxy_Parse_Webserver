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
#include<sys/stat.h>




#define MAX_CLIENTS 10
#define MAX_BYTES 4096
#define MAX_ELEMENTS_SIZE 10*(1<<10) // 10 kb
#define MAX_SIZE 200*(1<<10) // 200 kb


typedef struct  cashe_element{
    char* data;
    int len;
    char* url;
    time_t lru_track_cashe;
    struct cashe_element* next;
    
}cashe_element;

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

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %z", &data);

	switch(status_code)  
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

int connectRemoteServer(char *host_addr,int port_number){
  int remoteSocket= socket(AF_INET,SOCK_STREAM,0);
  if(remoteSocket<0){
    perror("Error in creating your Socket...\n");
    return -1;
    
  }
  struct hostent * host = gethostbyname(host_addr);
  if(host == NULL){
    perror("Error in getting host by name...\n");
    return -1;
  }
  struct sockaddr_in server_addr;
  bzero((char *)&server_addr,sizeof(server_addr));
  server_addr.sin_family= AF_INET;
  server_addr.sin_port= htons(port_number);

  bcopy((char *)&host->h_addr ,(char *)&server_addr.sin_addr.s_addr,host->h_length);
  if(connect(remoteSocket,(struct sockaddr *)&server_addr,(size_t)sizeof(server_addr)<0)){
    fprintf(stderr,"Error in connecting to remote server...\n");
    return -1;
  }
  return remoteSocket;
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
  if(remoteSocketId<0){
    printf("Error in connecting to remote server.../n");
    return -1;
  }
  int bytes_send=send(remoteSocketId,buf,strlen(buf),0);
  bzero(buf,MAX_BYTES);

  bytes_send=recv(remoteSocketId,buf,MAX_BYTES-1,0);
  char *temp_buffer= (char*)malloc(sizeof(char)*MAX_BYTES);
  int temp_buffer_size = MAX_BYTES;
  int temp_buffer_index - 0;

  while(bytes_send > 0){
    bytes_send = send(client_socketId,buf,MAX_BYTES-1,0);
    for(int i=0;i<bytes_send/sizeof(char); i++){
      temp_buffer[temp_buffer_index]=buf[i];
      temp_buffer_index++;
}
    temp_buffer_size += MAX_BYTES;
    temp_buffer = (char*)realloc(temp_buffer,temp_buffer_size);
    if(bytes_send < 0){
      perror("Error in sending Data to the client...\n");
      break;
    }
    bzero(buf,MAX_BYTES);
    bytes_send = recv(remoteSocketId,buf,MAX_BYTES-1,0);
    }

    temp_buffer[temp_buffer_index] = '\0'; // Null-terminate the buffer
    free(buf);
    add_cashe_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;
}

int checkHTTPSversion(char *msg){
  int version = -1;

  if(strcmp(msg,"HTTP/1.1",8)==0){
    int version= 1;
  }
  else if(strcmp(msg,"HTTP/1.0",8)==0){
    version = 1;
  }
  else{
    version = -1;
  }
  return version;
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

cashe_element *find(char *url){
  cashe_element *site = NULL;
  int temp_lock_val = pthread_mutex_lock(&lock);
  printf("Remove cache lock aquired %d\n", temp_lock_val);
  if(head == NULL){
    site = head;
    while(site != NULL){
      if(!strcmp(site->url,url)){
        printf("LRU time tracked before : %ld",site->lru_time_track);
        printf("\n url founf\n");
        site->lru_time_track = time(NULL);
        printf("LRU time track after %ld", site->lru_time_track);
        break;
      }
      site = site->next;
    }
    }
    else{
      printf("URL not found in the cache...\n");
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove cashe lock released %d\n",);
    return site;
}

void remove_cache_element(){
  cashe_element *p;
  cashe_element *q;
  cashe_element *temp;
  int temp_lock_vtempl = pthread_mutex_lock(&lock);
  printf("Lock is Aquiared...\n");
  if(head!=NULL){
    for(q=head , p=head, temp=head; q->next!=NULL; q=q->next){
      if(((q->next)->lru_time_track)<(temp->lru_time_track)){
        temp = q->next;
        p=q;
      }
    }
    if(temp == head){
      head=head->next;
    }else{
      p->next = temp->next;
    }
    cashe_size = cashe_size-(temp->len)-sizeof(cashe_element)-strlen(temp->url)-1;
    free(temp->data);
    free(temp->url);
    free(temp);
  }

  temp_lock_val = pthread_mutex_unlock(&lock);
  printf("Remove Cashe Lock...\n");


}

int add_cashe_element(char *data, int size,char *url){
  int temp_lock_val = pthread_mutex_lock(&lock);
  printf("Add cashe lock aquiared %d\n", temp_lock_val);
  int element_size = size+1+strlen(url)+sizeof(cashe_element);
  if(element_size> MAX_ELEMENTS_SIZE){
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Add cashe lock is Unlocked..\n");
    return 0;
  }
  else{
        while(cashe_size + element_size > MAX_SIZE){
           remove_cache_element();
        }
        cashe_element *element = (cashe_element*)malloc(sizeof(cashe_element));
        element->data = (char*)malloc(size*sizeof(char)+1);
        strcpy(element->data,data);
        element->url = (char*)malloc(strlen(url)*sizeof(char)+1);
        strcpy(element->url,url);
        element_lru_time_track = time(NULL);
        element->next = head;
        element->len = size;
        head = element;
        cashe_size += element_size;
        temp_lock_val =pthread_mutex_unlock(&lock);
        printf("Add cashe lock is unlocked %d\n", temp_lock_val);
        return 1;


  }

  return 0;


}







  
    
