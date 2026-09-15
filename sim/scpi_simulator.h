#ifndef SCPI_SIMULATOR_H
#define SCPI_SIMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define SCPI_MAX_RESPONSES 256
#define SCPI_MAX_TERMINATOR_LEN 32
#define SCPI_COMMAND_LOG_SIZE 16384
#define SCPI_RECV_BUFFER_SIZE 8192

typedef struct {
  char *command;
  char *response;

  bool persistent;
  bool consumed;

  size_t hit_count;
} ScheduledResponse;

typedef struct {
  uint16_t port;

  int server_fd;
  int client_fd;

  bool running;

  bool server_ready;
  bool client_connected;

  pthread_t thread;

  pthread_mutex_t mutex;

  pthread_cond_t ready_cond;
  pthread_cond_t connected_cond;
  char command_terminator[SCPI_MAX_TERMINATOR_LEN];

  ScheduledResponse responses[SCPI_MAX_RESPONSES];

  size_t response_count;

  char command_log[SCPI_COMMAND_LOG_SIZE];
} ScpiSimulator;

/*
 * Starts worker thread and blocks until server is listening.
 */
void scpi_simulator_start(ScpiSimulator *sim, uint16_t port,
                          const char *command_terminator);

/*
 * Stops simulator and frees resources.
 */
void scpi_simulator_stop(ScpiSimulator *sim);

/*
 * One-shot response.
 */
void scpi_simulator_expect(ScpiSimulator *sim, const char *command,
                           const char *response);

/*
 * Persistent response.
 */
void scpi_simulator_expect_persistent(ScpiSimulator *sim, const char *command,
                                      const char *response);

/*
 * Wait until VISA client connects.
 */
void scpi_simulator_wait_for_connection(ScpiSimulator *sim);

/*
 * Returns true when all non-persistent expectations
 * have been matched at least once.
 */
bool scpi_simulator_all_expectations_met(ScpiSimulator *sim);

/*
 * Number of times a command matched.
 */
size_t scpi_simulator_hits(ScpiSimulator *sim, const char *command);

bool scpi_simulator_client_connected(ScpiSimulator *sim);

/*
 * Command log.
 */
const char *scpi_simulator_command_log(ScpiSimulator *sim);

#endif
