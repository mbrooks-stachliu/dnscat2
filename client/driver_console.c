#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "memory.h"
#include "message.h"
#include "select_group.h"
#include "session.h"
#include "types.h"

#include "driver_console.h"

/* There can only be one driver_console, so store these as global variables. */
static SELECT_RESPONSE_t console_stdin_recv(void *group, int socket, uint8_t *data, size_t length, char *addr, uint16_t port, void *d)
{
  driver_console_t *driver_console = (driver_console_t*) d;

  message_post_data_out(driver_console->session_id, data, length);

  return SELECT_OK;
}

static SELECT_RESPONSE_t console_stdin_closed(void *group, int socket, void *d)
{
  /* When the stdin pipe is closed, the stdin driver signals the end. */
  message_post_shutdown();

  return SELECT_CLOSE_REMOVE;
}

/* This is called after the drivers are created, to kick things off. */
static void handle_start(driver_console_t *driver)
{
  /*message_post_create_session();*/
  if(driver->tunnel_host)
    message_post_create_session_with_tunnel(driver->tunnel_host, driver->tunnel_port);
  else
    message_post_create_session();
}

static void handle_session_created(driver_console_t *driver, uint16_t session_id)
{
  driver->session_id = session_id;
}

static void handle_data_in(driver_console_t *driver, uint8_t *data, size_t length)
{
  size_t i;

  for(i = 0; i < length; i++)
    fputc(data[i], stdout);
}

static void handle_message(message_t *message, void *d)
{
  driver_console_t *driver = (driver_console_t*) d;

  switch(message->type)
  {
    case MESSAGE_START:
      handle_start(driver);
      break;

    case MESSAGE_SESSION_CREATED:
      handle_session_created(driver, message->message.session_created.session_id);
      break;

    case MESSAGE_DATA_IN:
      handle_data_in(driver, message->message.data_in.data, message->message.data_in.length);
      break;

    default:
      LOG_FATAL("driver_console received an invalid message!");
      abort();
  }
}

driver_console_t *driver_console_create(select_group_t *group)
{
  driver_console_t *driver = (driver_console_t*) safe_malloc(sizeof(driver_console_t));

  driver->tunnel_host = NULL;
  driver->tunnel_port = -1;

#ifdef WIN32
  /* On Windows, the stdin_handle is quite complicated, and involves a sub-thread. */
  HANDLE stdin_handle = get_stdin_handle();
  select_group_add_pipe(ui_stdin->group, -1, stdin_handle, driver);
  select_set_recv(ui_stdin->group, -1, console_stdin_recv);
  select_set_closed(ui_stdin->group, -1, console_closed);
#else
  /* On Linux, the stdin_handle is easy. */
  int stdin_handle = STDIN_FILENO;
  select_group_add_socket(group, stdin_handle, SOCKET_TYPE_STREAM, driver);
  select_set_recv(group, stdin_handle, console_stdin_recv);
  select_set_closed(group, stdin_handle, console_stdin_closed);
#endif

  /* Subscribe to the messages we care about. */
  message_subscribe(MESSAGE_START,           handle_message, driver);
  message_subscribe(MESSAGE_SESSION_CREATED, handle_message, driver);
  message_subscribe(MESSAGE_DATA_IN,         handle_message, driver);

  return driver;
}

void driver_console_set_tunnel(driver_console_t *driver, char *host, uint16_t port)
{
  driver->tunnel_host = host;
  driver->tunnel_port = port;
}

void driver_console_destroy(driver_console_t *driver)
{
  safe_free(driver);
}
