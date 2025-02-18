#include "fcntl.h"
#include "pthread.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

int running = 1;
int sock_fd = -1;
int client_fd = -1;
int file_fd = -1;

int num_threads = 0;
pthread_mutex_t file_mutex;
pthread_mutex_t threads_mutex;
pthread_cond_t threads_cond;

typedef struct thread_node {
    pthread_t thread_id;
    int client_fd;
    struct thread_node *next;
} thread_node_t;

thread_node_t *threads = NULL;

void add_thread(pthread_t thread_id, int client_fd) {
    thread_node_t *node = (thread_node_t *)malloc(sizeof(thread_node_t));
    node->thread_id = thread_id;
    node->client_fd = client_fd;
    node->next = threads;
    threads = node;

    pthread_mutex_lock(&threads_mutex);
    num_threads++;
    pthread_mutex_unlock(&threads_mutex);
}

void remove_thread(thread_node_t *node) {
    if (threads == node) {
        threads = node->next;
    } else {
        thread_node_t *current = threads;
        while (current->next != node) {
            current = current->next;
        }
        current->next = node->next;
    }

    free(node);
}

void signal_handler(int signum __attribute__((unused))) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting.");
        running = 0;

        if (sock_fd >= 0) close(sock_fd);
        if (client_fd >= 0) close(client_fd);
        if (file_fd >= 0) close(file_fd);
        unlink(FILE_PATH);

        while (threads != NULL) {
          pthread_join(threads->thread_id, NULL);
          remove_thread(threads);
        }

        pthread_mutex_destroy(&file_mutex);
        pthread_mutex_destroy(&threads_mutex);
        pthread_cond_destroy(&threads_cond);

        closelog();

        exit(0);
    }
}

// append timestamp to file every 10 seconds
void *append_timestamp() {
  while (running) {
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "timestamp:%Y-%m-%d %H:%M:%S\n", tm);

    pthread_mutex_lock(&file_mutex);
    if (write(file_fd, timestamp, strlen(timestamp)) < 0) {
        perror("write");
    } else {
        syslog(LOG_INFO, "Appended timestamp: %s", timestamp);
    }
    pthread_mutex_unlock(&file_mutex);

    usleep(10*1000*1000);
  }

  return NULL;
}

void *handle_connection(void *arg) {
  char buffer[BUFFER_SIZE];
  thread_node_t *node = (thread_node_t *)arg;
  int client_fd = node->client_fd;

  while (1) {
    int bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
    if (bytes_received < 0) {
      perror("recv");
      pthread_exit((void *)1);
    } else if (bytes_received == 0) {
      break;
    }

    pthread_mutex_lock(&file_mutex);
    if (write(file_fd, buffer, bytes_received) < 0) {
      perror("write");
      pthread_mutex_unlock(&file_mutex);
      pthread_exit((void *)1);
    }
    pthread_mutex_unlock(&file_mutex);

    if (strchr(buffer, '\n') != NULL) {
      lseek(file_fd, 0, SEEK_SET);
      while ((bytes_received = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        send(client_fd, buffer, bytes_received, 0);
      }
    }
  }

  close(client_fd);
  client_fd = -1;
  free(arg);
  syslog(LOG_INFO, "Closed connection at socket %d", client_fd);

  return NULL;
}

int main(int argc, char *argv[]) {
  int daemon_mode = 0;
  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-d") == 0) {
          daemon_mode = 1;
      }
  }

  openlog("aesdsocket", LOG_PID, LOG_DAEMON);

  if (signal(SIGINT, signal_handler) == SIG_ERR) {
      syslog(LOG_ERR, "Failed to set SIGINT handler");
      return 1;
  }
  if (signal(SIGTERM, signal_handler) == SIG_ERR) {
      syslog(LOG_ERR, "Failed to set SIGTERM handler");
      return 1;
  }

  sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET; 
  serv_addr.sin_port  = htons(PORT);
  serv_addr.sin_addr.s_addr = INADDR_ANY; 

  if (bind(sock_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
    perror("bind");
    close(sock_fd);
    return 1;
  }

  if (daemon_mode) {
      pid_t pid = fork();
      if (pid < 0) {
          syslog(LOG_ERR, "Fork failed");
          return 1;
      }
      if (pid > 0) {
        return 0;
      }
      if (setsid() < 0) {
        perror("setsid");
        return 1;
      }
      close(STDIN_FILENO);
      close(STDOUT_FILENO);
      close(STDERR_FILENO);
      open("/dev/null", O_RDONLY);
      open("/dev/null", O_WRONLY);
      open("/dev/null", O_RDWR);
  }

  if (listen(sock_fd, 1) != 0) {
    perror("listen");
    close(sock_fd);
    return 1;
  }

  file_fd = open(FILE_PATH, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (file_fd < 0) {
    perror("open");
    close(sock_fd);
    return 1;
  }

  // start timestamp logging thread
  pthread_t thread_id;
  thread_node_t *node = (thread_node_t *)malloc(sizeof(thread_node_t));
  node->client_fd = client_fd;
  if (pthread_create(&thread_id, NULL, append_timestamp, (void *)node) < 0) {
    perror("pthread_create");
    close(client_fd);
  } else {
    add_thread(thread_id, client_fd);
  }

  while (running) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    client_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &addr_len);
    if (client_fd < 0) {
      perror("accept");
      continue;
    }

    char *client_ip = inet_ntoa(client_addr.sin_addr);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    // handle connection in new thread
    pthread_t thread_id;
    thread_node_t *node = (thread_node_t *)malloc(sizeof(thread_node_t));
    node->client_fd = client_fd;
    if (pthread_create(&thread_id, NULL, handle_connection, (void *)node) < 0) {
      perror("pthread_create");
      close(client_fd);
    } else {
      add_thread(thread_id, client_fd);
    }
  }

  // join all threads
  while (threads != NULL) {
    pthread_join(threads->thread_id, NULL);
    remove_thread(threads);
  }

  close(sock_fd);
  close(file_fd);
  unlink(FILE_PATH);
  closelog();

  pthread_mutex_destroy(&file_mutex);
  pthread_mutex_destroy(&threads_mutex);
  pthread_cond_destroy(&threads_cond);

  return 0;
}
