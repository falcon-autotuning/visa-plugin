#include "scpi_simulator.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

static void append_log(ScpiSimulator *sim, const char *text) {
  size_t current = strlen(sim->command_log);

  if (current >= sizeof(sim->command_log) - 1) {
    return;
  }

  size_t available = sizeof(sim->command_log) - current - 1;

  strncat(sim->command_log, text, available);
}

static void remove_terminator(char *command, const char *terminator) {
  size_t cmd_len = strlen(command);

  size_t term_len = strlen(terminator);

  if (cmd_len < term_len) {
    return;
  }

  if (strcmp(command + cmd_len - term_len, terminator) == 0) {
    command[cmd_len - term_len] = '\0';
  }
}

static ScheduledResponse *find_matching_response(ScpiSimulator *sim,
                                                 const char *command) {
  for (size_t i = 0; i < sim->response_count; ++i) {
    ScheduledResponse *r = &sim->responses[i];

    if (r->consumed) {
      continue;
    }

    if (strcmp(r->command, command) == 0) {
      return r;
    }
  }

  return NULL;
}

static void handle_command(ScpiSimulator *sim, const char *raw_command) {
  char command[SCPI_RECV_BUFFER_SIZE];

  snprintf(command, sizeof(command), "%s", raw_command);

  remove_terminator(command, sim->command_terminator);

  pthread_mutex_lock(&sim->mutex);

  append_log(sim, raw_command);

  ScheduledResponse *r = find_matching_response(sim, command);

  if (r != NULL) {
    r->hit_count++;

    send(sim->client_fd, r->response, strlen(r->response), 0);

    if (!r->persistent) {
      r->consumed = true;
    }
  } else {
    fprintf(stderr, "SCPI simulator: no handler for command '%s'\n", command);
  }

  pthread_mutex_unlock(&sim->mutex);
}
static bool ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (suffix_len > str_len) {
    return false;
  }

  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static void *scpi_worker(void *arg) {
  ScpiSimulator *sim = (ScpiSimulator *)arg;

  struct sockaddr_in addr;

  int reuse = 1;

  memset(&addr, 0, sizeof(addr));

  sim->server_fd = socket(AF_INET, SOCK_STREAM, 0);

  assert(sim->server_fd >= 0);

  setsockopt(sim->server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(sim->port);

  assert(bind(sim->server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  assert(listen(sim->server_fd, 1) == 0);

  pthread_mutex_lock(&sim->mutex);

  sim->server_ready = true;

  pthread_cond_broadcast(&sim->ready_cond);

  pthread_mutex_unlock(&sim->mutex);

  int flags = fcntl(sim->server_fd, F_GETFL, 0);

  fcntl(sim->server_fd, F_SETFL, flags | O_NONBLOCK);
  while (sim->running) {

    sim->client_fd = accept(sim->server_fd, NULL, NULL);

    if (sim->client_fd >= 0) {
      break;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      usleep(100000); /* 100 ms */
      continue;
    }

    perror("accept");
    return NULL;
  }

  if (!sim->running) {
    return NULL;
  }

  pthread_mutex_lock(&sim->mutex);

  sim->client_connected = true;

  pthread_cond_broadcast(&sim->connected_cond);

  pthread_mutex_unlock(&sim->mutex);

  char recv_buffer[SCPI_RECV_BUFFER_SIZE];

  char command_buffer[SCPI_RECV_BUFFER_SIZE];

  size_t command_len = 0;

  while (sim->running) {
    ssize_t bytes = recv(sim->client_fd, recv_buffer, sizeof(recv_buffer), 0);

    if (bytes <= 0) {
      break;
    }

    for (ssize_t i = 0; i < bytes; ++i) {
      char c = recv_buffer[i];

      if (command_len >= sizeof(command_buffer) - 1) {
        command_len = 0;
        continue;
      }

      command_buffer[command_len++] = c;
      command_buffer[command_len] = '\0';

      if (ends_with(command_buffer, sim->command_terminator)) {
        handle_command(sim, command_buffer);

        command_len = 0;
      }
    }
  }

  return NULL;
}

void scpi_simulator_start(ScpiSimulator *sim, uint16_t port,
                          const char *command_terminator) {
  memset(sim, 0, sizeof(*sim));

  sim->port = port;
  snprintf(sim->command_terminator, sizeof(sim->command_terminator), "%s",
           command_terminator);

  sim->server_fd = -1;
  sim->client_fd = -1;

  pthread_mutex_init(&sim->mutex, NULL);

  pthread_cond_init(&sim->ready_cond, NULL);

  pthread_cond_init(&sim->connected_cond, NULL);

  sim->running = true;

  int rc = pthread_create(&sim->thread, NULL, scpi_worker, sim);

  assert(rc == 0);

  pthread_mutex_lock(&sim->mutex);

  while (!sim->server_ready) {
    pthread_cond_wait(&sim->ready_cond, &sim->mutex);
  }

  pthread_mutex_unlock(&sim->mutex);
}

void scpi_simulator_stop(ScpiSimulator *sim) {
  sim->running = false;

  if (sim->client_fd >= 0) {
    shutdown(sim->client_fd, SHUT_RDWR);

    close(sim->client_fd);

    sim->client_fd = -1;
  }

  if (sim->server_fd >= 0) {
    shutdown(sim->server_fd, SHUT_RDWR);

    close(sim->server_fd);

    sim->server_fd = -1;
  }

  pthread_join(sim->thread, NULL);

  for (size_t i = 0; i < sim->response_count; ++i) {
    free(sim->responses[i].command);

    free(sim->responses[i].response);
  }

  pthread_mutex_destroy(&sim->mutex);

  pthread_cond_destroy(&sim->ready_cond);

  pthread_cond_destroy(&sim->connected_cond);
}

void scpi_simulator_expect(ScpiSimulator *sim, const char *command,
                           const char *response) {
  pthread_mutex_lock(&sim->mutex);

  assert(sim->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->responses[sim->response_count++];

  r->command = strdup(command);
  r->response = strdup(response);

  r->persistent = false;
  r->consumed = false;
  r->hit_count = 0;

  pthread_mutex_unlock(&sim->mutex);
}

void scpi_simulator_expect_persistent(ScpiSimulator *sim, const char *command,
                                      const char *response) {
  pthread_mutex_lock(&sim->mutex);

  assert(sim->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->responses[sim->response_count++];

  r->command = strdup(command);
  r->response = strdup(response);

  r->persistent = true;
  r->consumed = false;
  r->hit_count = 0;

  pthread_mutex_unlock(&sim->mutex);
}

void scpi_simulator_wait_for_connection(ScpiSimulator *sim) {
  pthread_mutex_lock(&sim->mutex);

  while (!sim->client_connected) {
    pthread_cond_wait(&sim->connected_cond, &sim->mutex);
  }

  pthread_mutex_unlock(&sim->mutex);
}

bool scpi_simulator_all_expectations_met(ScpiSimulator *sim) {
  bool result = true;

  pthread_mutex_lock(&sim->mutex);

  for (size_t i = 0; i < sim->response_count; ++i) {
    ScheduledResponse *r = &sim->responses[i];

    if (!r->persistent && !r->consumed) {
      result = false;
      break;
    }
  }

  pthread_mutex_unlock(&sim->mutex);

  return result;
}

size_t scpi_simulator_hits(ScpiSimulator *sim, const char *command) {
  size_t hits = 0;

  pthread_mutex_lock(&sim->mutex);

  for (size_t i = 0; i < sim->response_count; ++i) {
    if (strcmp(sim->responses[i].command, command) == 0) {
      hits += sim->responses[i].hit_count;
    }
  }

  pthread_mutex_unlock(&sim->mutex);

  return hits;
}

const char *scpi_simulator_command_log(ScpiSimulator *sim) {
  return sim->command_log;
}

bool scpi_simulator_client_connected(ScpiSimulator *sim) {
  bool result;

  pthread_mutex_lock(&sim->mutex);

  result = sim->client_connected;

  pthread_mutex_unlock(&sim->mutex);

  return result;
}
