//usage gcc -o http_server http_server.c -lpthread
// ./http_server 5 5 80
// localhost:1000/home/tzur
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>

#define HTTP_UNAVALIABLE	"503 Service Unavailable"
#define DEFAULT_PORT				80
#define MAX_CONTENT_LENGTH	100
#define HTTP_DEFAULT_TEMPLATE \
	"<html>" \
	"<head><title>%s</title></head>" \
	"<body><h1>%s</h1></body>" \
	"</html>"

#define HTTP_RESPONSE_TEMPLATE \
	"HTTP/1.0 %s\r\n" \
	"\r\n" \
	"%s"

#define HTTP_SERVICE_UNAVALIABLE	"503 Service Unavailable"
#define HTTP_NOT_FOUND 				"404 Not Found"
#define HTTP_NOT_IMPLEMENTED 		"501 Not Implemented"
#define HTTP_OK 					"200 OK"
//	"Content-Type: text/html\r\n" \
//	"Content-Length: %lu\r\n" \

pthread_mutex_t sharedElementMutex; // mutex for access to shared "data"
pthread_cond_t  canConsumeCondition; // condition variable to wake up "consumers" of data
int done = 0;
int num_threads_global = 0;
pthread_t threads_global[1000];
int socket_fd;
int queue_size = 0;
struct list_el {
   int fd;
   struct list_el *next;
};
struct list_el *head = NULL;
struct list_el *tail = NULL;
void writeToFile(int client_fd, char* buf, int size){
 
    int overall_chars = 0;
    int chars_wrote = 0;
     
    while (overall_chars < size){

        chars_wrote = write(client_fd, buf+overall_chars, size-overall_chars);
        overall_chars += chars_wrote;
        assert(chars_wrote != -1 && "error writing to file! closing");

    }  
    return;
}

void respond(int fd, const char* code, char* message) {

 	int len;
 	// is msg is NULL, we generate a default http msg
 	if (!message) {
 		len = strlen(HTTP_DEFAULT_TEMPLATE) + strlen(code) * 2 + 1;
 		message = (char*) malloc(len);
 		assert(message);
 		sprintf(message, HTTP_DEFAULT_TEMPLATE, code, code);
 		message[len - 1] = '\0';
 	}
 	
// 	// Response
 	len = strlen(HTTP_RESPONSE_TEMPLATE)+ strlen(code)+ MAX_CONTENT_LENGTH+ strlen(message);

 	char * response = (char*) malloc(len);
 	assert(response && "standard function malloc failed");

 	sprintf(response,HTTP_RESPONSE_TEMPLATE,code,message);

 	writeToFile(fd, response, strlen(response));
 	assert(close(fd) != -1);
	}



void handler(int sig){
 	int i, num_threads_global, rc;
    signal(sig, SIG_IGN);
    rc = pthread_mutex_lock(&sharedElementMutex); assert(!rc);
    done = 1;
    rc = pthread_mutex_unlock(&sharedElementMutex); assert(!rc);

    printf(" cleaning out the queue...\n");
  	for (i=0; i < num_threads_global; i++) {
       rc = pthread_mutex_lock(&sharedElementMutex); assert(!rc);
       pthread_cond_signal(&canConsumeCondition);
       rc = pthread_mutex_unlock(&sharedElementMutex); assert(!rc);
   	}    
    for(i = 0 ; i < num_threads_global; i++){
		rc = pthread_join(threads_global[i], NULL);
	}
	printf("done\n");
	close(socket_fd);
    exit(0);
}

void* respond_thread(int client_fd) {
	char buf[1000], request[2][1024];
	int total_wrriten, read_size = 0, total_size = 0; 
	struct stat st;
	//if we got here, we need to parse the client's request
	//read(client_fd ,buf, 1000);
	//we need to locate the string "\r\n\r\n", as written in the forum.
	int match = 0;
	while (!match){
        read_size = read(client_fd, buf+total_size, (2000)-total_size);

        total_size += read_size;
         
        if (strstr(buf, "\r\n\r\n") != NULL)
            match = 1;
    }

	//we need to check wether its a get or a post
	sscanf(buf, "%s %s ", request[0], request[1]);
	//printf("reponding! thread - %u \n", (unsigned int)pthread_self());
	if(!strcmp(request[0], "GET") || !strcmp(request[0], "POST")){
		//we need to check wether its a folder or a specific file
		stat(request[1], &st);
		if(st.st_mode & S_IFDIR){
			char response[2000];
			char dir_buf[1024];
			memset(&dir_buf, '\0', sizeof(dir_buf));
			//its a DIR, so lets check the contents
			DIR *dir;
			struct dirent *item;
			if ((dir = opendir (request[1])) != NULL) {
				/* print all the files and directories within directory */
				while ((item = readdir (dir)) != NULL) {
					strcat(dir_buf, item->d_name);
					strcat(dir_buf,"<br>");
				}
				sprintf(response,HTTP_DEFAULT_TEMPLATE,"Dir",dir_buf);
				respond(client_fd, HTTP_OK, response);
				closedir (dir);
			} else {
				
				return ;
			}
			//close(client_fd);
		}
		else if(st.st_mode & S_IFREG){
			//its a FILE lets send it!!!!
			//first we need to write that everything is ok - 
			char buf[1000];
			int overall = 0;
			int open_fd = open(request[1], O_RDONLY);
			if(open_fd != -1){
				sprintf(buf, HTTP_RESPONSE_TEMPLATE, HTTP_OK, "");
				writeToFile(client_fd, buf, strlen(buf));
				lseek(open_fd, 0 ,SEEK_SET);
				//we read in parts here. each time we read a part - we write a part.
				int bytes_read = read(open_fd, buf, 1000);
				//for the seek
				overall +=bytes_read;
				while(bytes_read > 0){
					writeToFile(client_fd, buf, bytes_read);
					lseek(open_fd, overall, SEEK_SET);
					bytes_read = read(open_fd, buf, 1000);
					overall+=bytes_read;
				}
				close(client_fd);
				//close(open_fd);
			}
			else{//file doesnt exist
				respond(client_fd, HTTP_NOT_FOUND, NULL);
				//close(client_fd);
			}
		}
		else{
			//it's a deamon, so we can't implement it!
			int open_fd = open(request[1], O_RDONLY);
			if(open_fd == -1){
				respond(client_fd, HTTP_NOT_FOUND, NULL);
				//close(client_fd);
			}
		}
	}
	else{
		respond(client_fd, HTTP_NOT_IMPLEMENTED,NULL);
		//close(client_fd);
	}


	}

int create_and_bind_socket(short port) {
	int socket_fd1 = socket(AF_INET, SOCK_STREAM, 0);
	assert(socket_fd1 != -1);

	//address to bind the socket to.
	struct sockaddr_in addr;
	memset(&addr, '0', sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(socket_fd1, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		printf("\n Error : Bind Failed. %s \n", strerror(errno));
		return 1;
	}

	return socket_fd1;
}

int enqueue(int connfd){
	struct list_el *curr = NULL;
	int rc;

	//lock the mutex when we use the list
	rc = pthread_mutex_lock(&sharedElementMutex);
	//first we check if the list is empty
	//we assign a value and the tail
	if(queue_size == 0){
		head = (struct list_el *)malloc(sizeof(struct list_el));
		tail = (struct list_el *)malloc(sizeof(struct list_el));
		assert(!rc && "error in locking!");
		head->fd = connfd;
		tail = head;
		queue_size++;
	}
	else{
		curr = (struct list_el *)malloc(sizeof(struct list_el));
		assert(!rc && "error in locking!");
		curr->fd = connfd;
		tail->next = curr;
		tail = curr;
		queue_size++;
	}
	//wake up!
	pthread_cond_signal(&canConsumeCondition);
	pthread_mutex_unlock(&sharedElementMutex);
	//unlcok after finished medeling with the list

}

void* dequeue(){
	struct list_el *curr = NULL;
	int rc;
	//pthread_cond_wait(&canConsumeCondition, &sharedElementMutex);
	rc = pthread_mutex_lock(&sharedElementMutex);
	if (rc) {
		printf("Error locking\n");
        exit(1);
    }
	while(done == 0 || queue_size != 0){

		while(queue_size == 0){

			pthread_cond_wait(&canConsumeCondition, &sharedElementMutex);
		}
		
		//printf("haaaa\n");
		//we dequeue, but have to lock the queue in the meanwhile.

		curr = head;
		head=head->next;
		queue_size--;
		if(queue_size != 0)
			pthread_cond_signal(&canConsumeCondition);
		rc = pthread_mutex_unlock(&sharedElementMutex);
		//printf("dequening! thread - %u \n", (unsigned int)pthread_self());
	//this is the function for the get/post logics
		respond_thread(curr->fd);
		close(curr->fd);
		free(curr);
		rc = pthread_mutex_lock(&sharedElementMutex);
	}
}

int main(int argc, char** argv){
	int num_threads, max_requests, port = DEFAULT_PORT, i, rc;
	int err;
	struct sigaction sa;

	sa.sa_handler = handler;
	sigaction(SIGINT, &sa, NULL);
	num_threads = strtol(argv[1], NULL, 10); 
	num_threads_global = num_threads;
	max_requests = strtol(argv[2], NULL, 10); 
	pthread_t threads[num_threads];
	*threads_global = *threads;
	if(argc == 4){
		port = strtol(argv[3], NULL, 10);
	}
	//we open and bind the port here. wether it's the default or else.
	socket_fd = create_and_bind_socket(port);

	//at the forum, they said that 10 is enough
	if(listen(socket_fd, 10)){
		printf("\n Error : Listen Failed. %s \n", strerror(errno));
       	return 1; 
	}
	rc = pthread_mutex_init(&sharedElementMutex, NULL); assert(rc==0 && "Lock failed!");
   	rc = pthread_cond_init(&canConsumeCondition, NULL); assert(rc==0&& "condition failed!");

	int* client = NULL;
	//getting the threads ready to dequeue when the lock is freed.
	for(i = 0; i < num_threads; i++){
		err = pthread_create( &(threads[i]) , NULL , &dequeue , NULL);
	}
	while(1){
		client = (int*) malloc(sizeof(int));
		assert(client && "standard function malloc Failed(in client)");
			
		*client = accept(socket_fd, NULL, NULL);

		if(*client == -1){
           printf("\n Error : Accept Failed. %s \n", strerror(errno));
           return 1; 
		}

		if(queue_size == max_requests){
			respond(*client, HTTP_SERVICE_UNAVALIABLE,NULL);
			close(*client);
		}		
		else{

			//if there is still a place in the queue, we enqueue the new item
			enqueue(*client);
		}
	}

}