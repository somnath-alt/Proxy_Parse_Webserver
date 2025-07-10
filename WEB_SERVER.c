#include "proxy_parse.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/stat.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 4096
#define MAX_ELEMENTS_SIZE 10 * (1 << 10) // 10 KB
#define MAX_SIZE 200 * (1 << 10)        // 200 KB

typedef struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    struct cache_element* next;
} cache_element;

// Function declarations
cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();
int handle_request(int client_socketId, struct ParsedRequest* request, char* tempReq);

int port_number = 8080;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;
cache_element* head;
int cache_size;

int sendErrorMessage(int socket, int status_code) {
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %z", &data);

    switch (status_code) {
        case 400:
            snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
            printf("400 Bad Request\n");
            send(socket, str, strlen(str), 0);
            break;
        case 403:
            snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
            printf("403 Forbidden\n");
            send(socket, str, strlen(str), 0);
            break;
        case 404:
            snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
            printf("404 Not Found\n");
            send(socket, str, strlen(str), 0);
            break;
        case 500:
            snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
            send(socket, str, strlen(str), 0);
            break;
        case 501:
            snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
            printf("501 Not Implemented\n");
            send(socket, str, strlen(str), 0);
            break;
        case 505:
            snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
            printf("505 HTTP Version Not Supported\n");
            send(socket, str, strlen(str), 0);
            break;
        default:
            return -1;
    }
    return 1;
}

int connectRemoteServer(char* host_addr, int port_number) {
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0) {
        perror("Error in creating socket...\n");
        return -1;
    }
    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL) {
        perror("Error in getting host by name...\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    bcopy((char*)host->h_addr, (char*)&server_addr.sin_addr.s_addr, host->h_length);
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error in connecting to remote server...\n");
        return -1;
    }
    return remoteSocket;
}

int handle_request(int client_socketId, struct ParsedRequest* request, char* tempReq) {
    char* buf = malloc(MAX_BYTES * sizeof(char));
    snprintf(buf, MAX_BYTES, "GET %s %s\r\n", request->path, request->version);
    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set header key is not working...\n");
    }
    if (ParsedHeader_get(request, "Host") == NULL) {
        if (ParsedHeader_set(request, "Host", request->host) < 0) {
            printf("Host header is not set...\n");
        }
    }
    if (ParsedRequest_unparse_headers(request, buf + len, MAX_BYTES - len) < 0) {
        printf("Unparse failed...\n");
    }

    int server_port = 80;
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if (remoteSocketId < 0) {
        printf("Error in connecting to remote server...\n");
        free(buf);
        return -1;
    }
    int bytes_send = send(remoteSocketId, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

    char* temp_buffer = malloc(MAX_BYTES * sizeof(char));
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    bytes_send = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    while (bytes_send > 0) {
        if (send(client_socketId, buf, bytes_send, 0) < 0) {
            perror("Error in sending data to client...\n");
            break;
        }
        if (temp_buffer_index + bytes_send > temp_buffer_size) {
            temp_buffer_size += MAX_BYTES;
            temp_buffer = realloc(temp_buffer, temp_buffer_size);
        }
        memcpy(temp_buffer + temp_buffer_index, buf, bytes_send);
        temp_buffer_index += bytes_send;
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';
    add_cache_element(temp_buffer, temp_buffer_index, tempReq);
    free(temp_buffer);
    free(buf);
    close(remoteSocketId);
    return 0;
}

int checkHTTPSversion(char* msg) {
    if (strncmp(msg, "HTTP/1.1", 8) == 0 || strncmp(msg, "HTTP/1.0", 8) == 0) {
        return 1;
    }
    return -1;
}

void* thread_fn(void* socketNew) {
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore Value is: %d\n", p);
    int* t = (int*)socketNew;
    int socket = *t;
    free(t);

    char* buffer = calloc(MAX_BYTES, sizeof(char));
    int bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while (bytes_send_client > 0) {
        int len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") != NULL) {
            break;
        }
        bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
    }

    char* tempReq = malloc(bytes_send_client + 1);
    memcpy(tempReq, buffer, bytes_send_client);
    tempReq[bytes_send_client] = '\0';

    if (bytes_send_client > 0) {
        cache_element* temp = find(tempReq);
        if (temp != NULL) {
            int size = temp->len;
            int pos = 0;
            char response[MAX_BYTES];
            while (pos < size) {
                memset(response, 0, MAX_BYTES);
                int chunk_size = (size - pos < MAX_BYTES) ? size - pos : MAX_BYTES;
                memcpy(response, temp->data + pos, chunk_size);
                send(socket, response, chunk_size, 0);
                pos += chunk_size;
            }
            printf("Data retrieved from cache...\n");
            printf("%.*s\n\n", size, temp->data);
        } else {
            struct ParsedRequest* request = ParsedRequest_create();
            if (ParsedRequest_parse(request, buffer, bytes_send_client) < 0) {
                printf("Parsing failed...\n");
                sendErrorMessage(socket, 400);
            } else {
                if (!strcmp(request->method, "GET") && request->host && request->path && checkHTTPSversion(request->version) == 1) {
                    if (handle_request(socket, request, tempReq) == -1) {
                        sendErrorMessage(socket, 500);
                    }
                } else {
                    sendErrorMessage(socket, 501);
                }
            }
            ParsedRequest_destroy(request);
        }
    } else if (bytes_send_client == 0) {
        printf("Client disconnected...\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    free(tempReq);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore Value is: %d\n", p);
    return NULL;
}

int main(int argc, char* argv[]) {
    int client_socketId, client_len, proxy_socketId;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    if (argc == 2) {
        port_number = atoi(argv[1]);
    } else {
        printf("Too few arguments...\n");
        exit(1);
    }

    printf("Starting proxy server at port: %d\n", port_number);
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0) {
        perror("Failed to create a socket\n");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt failed...\n");
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Port is not available...\n");
        exit(1);
    }
    printf("Binding on port %d\n", port_number);
    if (listen(proxy_socketId, MAX_CLIENTS) < 0) {
        perror("Error in listening...\n");
        exit(1);
    }

    int i = 0;
    while (1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if (client_socketId < 0) {
            printf("Not able to connect...\n");
            continue;
        }

        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client connected with port number %d and IP address %s\n", ntohs(client_addr.sin_port), str);

        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socketId;
        pthread_create(&tid[i], NULL, thread_fn, socket_ptr);
        pthread_detach(tid[i]);
        i++;
        if (i >= MAX_CLIENTS) {
            i = 0;
        }
    }

    close(proxy_socketId);
    return 0;
}

cache_element* find(char* url) {
    cache_element* site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Find cache lock acquired %d\n", temp_lock_val);
    site = head;
    while (site != NULL) {
        if (!strcmp(site->url, url)) {
            printf("LRU time tracked before: %ld\n", site->lru_time_track);
            printf("URL found\n");
            site->lru_time_track = time(NULL);
            printf("LRU time track after: %ld\n", site->lru_time_track);
            break;
        }
        site = site->next;
    }
    if (site == NULL) {
        printf("URL not found in cache...\n");
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Find cache lock released %d\n", temp_lock_val);
    return site;
}

void remove_cache_element() {
    cache_element *p = NULL, *q = head, *temp = head;
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove cache lock acquired %d\n", temp_lock_val);
    if (head != NULL) {
        while (q->next != NULL) {
            if ((q->next)->lru_time_track < temp->lru_time_track) {
                temp = q->next;
                p = q;
            }
            q = q->next;
        }
        if (temp == head) {
            head = head->next;
        } else if (p != NULL) {
            p->next = temp->next;
        }
        cache_size -= (temp->len + sizeof(cache_element) + strlen(temp->url) + 1);
        free(temp->data);
        free(temp->url);
        free(temp);
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove cache lock released %d\n", temp_lock_val);
}

int add_cache_element(char* data, int size, char* url) {
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add cache lock acquired %d\n", temp_lock_val);
    int element_size = size + strlen(url) + sizeof(cache_element) + 1;
    if (element_size > MAX_ELEMENTS_SIZE) {
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock released %d\n", temp_lock_val);
        return 0;
    }
    while (cache_size + element_size > MAX_SIZE) {
        remove_cache_element();
    }
    cache_element* element = malloc(sizeof(cache_element));
    element->data = malloc(size + 1);
    memcpy(element->data, data, size);
    element->data[size] = '\0';
    element->url = malloc(strlen(url) + 1);
    strcpy(element->url, url);
    element->lru_time_track = time(NULL);
    element->len = size;
    element->next = head;
    head = element;
    cache_size += element_size;
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Add cache lock released %d\n", temp_lock_val);
    return 1;
}
