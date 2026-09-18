#include "scpi_simulator.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
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
  size_t current = strlen(sim->shared->command_log);

  if (current >= sizeof(sim->shared->command_log) - 1) {
    return;
  }

  size_t available = sizeof(sim->shared->command_log) - current - 1;

  strncat(sim->shared->command_log, text, available);
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
  for (size_t i = 0; i < sim->shared->response_count; ++i) {
    ScheduledResponse *r = &sim->shared->responses[i];

    if (sim->shared->consumed[i]) {
      continue;
    }

    if (strcmp(r->command, command) == 0) {
      return r;
    }
  }

  return NULL;
}
static ssize_t transport_write(ScpiSimulator *sim, const void *buf,
                               size_t size) {
  ssize_t rc;

  switch (sim->transport_type) {

  case SCPI_TRANSPORT_TCP:
    rc = send(sim->worker.client_fd, buf, size, 0);
    break;

  case SCPI_TRANSPORT_SERIAL:
    rc = write(sim->worker.serial_fd, buf, size);
    break;

  default:
    return -1;
  }

  if (rc < 0) {
    fprintf(stderr, "transport_write failed: errno=%d (%s)\n", errno,
            strerror(errno));
  }

  return rc;
}
static ssize_t transport_read(ScpiSimulator *sim, void *buf, size_t size) {
  ssize_t rc;

  switch (sim->transport_type) {

  case SCPI_TRANSPORT_TCP:
    rc = recv(sim->worker.client_fd, buf, size, 0);
    break;

  case SCPI_TRANSPORT_SERIAL:
    rc = read(sim->worker.serial_fd, buf, size);
    break;

  default:
    return -1;
  }

  if (rc < 0) {
    fprintf(stderr, "transport_read failed: errno=%d (%s)\n", errno,
            strerror(errno));
  }

  return rc;
}
#include <time.h>

static uint64_t get_time_us(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
#endif
}
typedef struct {
  ScpiSimulator *sim;
  ScheduledResponse response;
  size_t idx;
} ResponseTask;
static void *response_thread(void *arg) {
  ResponseTask *task = arg;

  if (task->response.time_delay_us > 0) {
    usleep((useconds_t)task->response.time_delay_us);
  }

  fprintf(stderr, "[%llu us] actually sending response '%s'\n",
          (unsigned long long)get_time_us(), task->response.response);

  pthread_mutex_lock(&task->sim->write_mutex);

  transport_write(task->sim, task->response.response,
                  strlen(task->response.response));

  pthread_mutex_unlock(&task->sim->write_mutex);

  free(task);

  return NULL;
}

static void handle_command(ScpiSimulator *sim, const char *raw_command) {
  uint64_t start = get_time_us();
  fprintf(stderr, "[%llu us] received '%s'\n", (unsigned long long)start,
          raw_command);

  char command[SCPI_RECV_BUFFER_SIZE];
  snprintf(command, sizeof(command), "%s", raw_command);
  remove_terminator(command, sim->command_terminator);
  append_log(sim, raw_command);

  ScheduledResponse *r = find_matching_response(sim, command);

  if (r != NULL) {
    size_t idx = (size_t)(r - sim->shared->responses);

    sim->shared->hit_counts[idx]++;

    sim->shared->total_commands_handled++;
    pthread_t tid;

    ResponseTask *task = malloc(sizeof(*task));

    task->sim = sim;
    task->response = *r;
    task->idx = idx;
    if (pthread_create(&tid, NULL, response_thread, task) == 0) {
      pthread_detach(tid);

    } else {
      free(task);
    }

    if (!r->persistent) {
      sim->shared->consumed[idx] = true;
    }
  } else {
    fprintf(stderr, "SCPI simulator: no handler for command '%s'\n", command);
  }
}
static bool ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (suffix_len > str_len) {
    return false;
  }

  return strcmp(str + str_len - suffix_len, suffix) == 0;
}
static bool tcp_setup(void *arg) {
  ScpiSimulator *sim = (ScpiSimulator *)arg;

  struct sockaddr_in addr;
  int reuse = 1;

  memset(&addr, 0, sizeof(addr));

  sim->worker.server_fd = socket(AF_INET, SOCK_STREAM, 0);

  assert(sim->worker.server_fd >= 0);

  setsockopt(sim->worker.server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
             sizeof(reuse));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(sim->port);

  assert(bind(sim->worker.server_fd, (struct sockaddr *)&addr, sizeof(addr)) ==
         0);

  assert(listen(sim->worker.server_fd, 1) == 0);

  sim->shared->server_ready = true;

  fprintf(stderr, "[%llu us] waiting for client connection\n",
          (unsigned long long)get_time_us());

  sim->worker.client_fd = accept(sim->worker.server_fd, NULL, NULL);

  if (sim->worker.client_fd < 0) {
    perror("accept");
    return false;
  }

  fprintf(stderr, "[%llu us] accepted connection\n",
          (unsigned long long)get_time_us());

  sim->shared->client_connected = true;

  return true;
}
static bool serial_setup(ScpiSimulator *sim) {
  sim->worker.serial_fd = open(sim->serial_device, O_RDWR | O_NOCTTY);

  if (sim->worker.serial_fd < 0) {
    perror("open serial");
    return false;
  }

  sim->shared->server_ready = true;
  sim->shared->client_connected = true;

  return true;
}
static void scpi_worker(ScpiSimulator *sim) {

  if (sim->transport_type == SCPI_TRANSPORT_TCP) {
    if (!tcp_setup(sim)) {
      return;
    }
  } else {
    if (!serial_setup(sim)) {
      return;
    }
  }

  char recv_buffer[SCPI_RECV_BUFFER_SIZE];
  char command_buffer[SCPI_RECV_BUFFER_SIZE];

  size_t command_len = 0;

  while (sim->shared->running) {

    ssize_t bytes = transport_read(sim, recv_buffer, sizeof(recv_buffer));

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

  return;
}

void scpi_simulator_start_tcp(ScpiSimulator *sim, uint16_t port,
                              const char *command_terminator) {
  memset(sim, 0, sizeof(*sim));
  sim->transport_type = SCPI_TRANSPORT_TCP;
  pthread_mutex_init(&sim->write_mutex, NULL);
  sim->port = port;
  snprintf(sim->command_terminator, sizeof(sim->command_terminator), "%s",
           command_terminator);
  sim->shared = mmap(NULL, sizeof(ScpiSharedState), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  assert(sim->shared != MAP_FAILED);
  memset(sim->shared, 0, sizeof(ScpiSharedState));
  sim->shared->running = true;

  pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    scpi_worker(sim);
    _exit(0);
  }

  sim->child_pid = pid;
  while (!sim->shared->server_ready) {
    usleep(1000);
  }
}

void scpi_simulator_start_serial(ScpiSimulator *sim, const char *serial_device,
                                 const char *command_terminator) {
  memset(sim, 0, sizeof(*sim));
  sim->transport_type = SCPI_TRANSPORT_SERIAL;
  pthread_mutex_init(&sim->write_mutex, NULL);
  snprintf(sim->command_terminator, sizeof(sim->command_terminator), "%s",
           command_terminator);
  snprintf(sim->serial_device, sizeof(sim->serial_device), "%s", serial_device);
  sim->shared = mmap(NULL, sizeof(ScpiSharedState), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  assert(sim->shared != MAP_FAILED);
  memset(sim->shared, 0, sizeof(ScpiSharedState));
  sim->shared->running = true;

  pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    scpi_worker(sim);
    _exit(0);
  }

  sim->child_pid = pid;
  while (!sim->shared->server_ready) {
    usleep(1000);
  }
}

void scpi_simulator_stop(ScpiSimulator *sim) {
  sim->shared->running = false;
  pthread_mutex_destroy(&sim->write_mutex);

  if (sim->worker.client_fd >= 0) {
    shutdown(sim->worker.client_fd, SHUT_RDWR);
    close(sim->worker.client_fd);
    sim->worker.client_fd = -1;
  }

  if (sim->worker.server_fd >= 0) {
    shutdown(sim->worker.server_fd, SHUT_RDWR);
    close(sim->worker.server_fd);
    sim->worker.server_fd = -1;
  }

  if (sim->worker.serial_fd >= 0) {
    close(sim->worker.serial_fd);
    sim->worker.serial_fd = -1;
  }
  sim->shared->running = false;

  for (int i = 0; i < 100; ++i) {
    pid_t rc = waitpid(sim->child_pid, NULL, WNOHANG);
    if (rc == sim->child_pid) {
      break;
    }
    usleep(10000);
  }

  kill(sim->child_pid, SIGTERM);
  waitpid(sim->child_pid, NULL, 0);
  munmap(sim->shared, sizeof(ScpiSharedState));
}

void scpi_simulator_expect(ScpiSimulator *sim, const char *command,
                           const char *response) {

  assert(sim->shared->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->shared->responses[sim->shared->response_count++];
  snprintf(r->command, sizeof(r->command), "%s", command);
  snprintf(r->response, sizeof(r->response), "%s", response);

  r->persistent = false;
}

void scpi_simulator_expect_delayed(ScpiSimulator *sim, const char *command,
                                   const char *response, uint64_t delay_us) {

  assert(sim->shared->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->shared->responses[sim->shared->response_count++];
  snprintf(r->command, sizeof(r->command), "%s", command);
  snprintf(r->response, sizeof(r->response), "%s", response);

  r->persistent = false;
  r->time_delay_us = delay_us;
}

void scpi_simulator_expect_persistent(ScpiSimulator *sim, const char *command,
                                      const char *response) {

  assert(sim->shared->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->shared->responses[sim->shared->response_count++];

  snprintf(r->command, sizeof(r->command), "%s", command);
  snprintf(r->response, sizeof(r->response), "%s", response);

  r->persistent = true;
}
void scpi_simulator_expect_persistent_delayed(ScpiSimulator *sim,
                                              const char *command,
                                              const char *response,
                                              uint64_t delay_us) {

  assert(sim->shared->response_count < SCPI_MAX_RESPONSES);

  ScheduledResponse *r = &sim->shared->responses[sim->shared->response_count++];

  snprintf(r->command, sizeof(r->command), "%s", command);
  snprintf(r->response, sizeof(r->response), "%s", response);

  r->persistent = true;
  r->time_delay_us = delay_us;
}

void scpi_simulator_wait_for_connection(ScpiSimulator *sim) {
  while (!sim->shared->client_connected) {
    usleep(1000);
  }
}

bool scpi_simulator_all_expectations_met(ScpiSimulator *sim) {
  bool result = true;

  for (size_t i = 0; i < sim->shared->response_count; ++i) {
    ScheduledResponse *r = &sim->shared->responses[i];

    if (!r->persistent && !sim->shared->consumed[i]) {
      result = false;
      break;
    }
  }

  return result;
}

size_t scpi_simulator_hits(ScpiSimulator *sim, const char *command) {
  size_t hits = 0;

  for (size_t i = 0; i < sim->shared->response_count; ++i) {
    if (strcmp(sim->shared->responses[i].command, command) == 0) {
      hits += sim->shared->hit_counts[i];
    }
  }

  return hits;
}

const char *scpi_simulator_command_log(ScpiSimulator *sim) {
  return sim->shared->command_log;
}

bool scpi_simulator_client_connected(ScpiSimulator *sim) {
  return sim->shared->client_connected;
}

bool scpi_simulator_wait_for_hits(ScpiSimulator *sim, const char *command,
                                  size_t expected_hits, uint32_t timeout_ms) {
  const uint32_t sleep_ms = 1;

  for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed += sleep_ms) {
    if (scpi_simulator_hits(sim, command) >= expected_hits) {
      return true;
    }

    usleep(sleep_ms * 1000);
  }
  return false;
}
const char *scpi_simulator_serial_device(ScpiSimulator *sim) {
  return sim->serial_device;
}
