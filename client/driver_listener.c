#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"
#include "memory.h"
#include "message.h"
#include "select_group.h"
#include "session.h"
#include "tcp.h"
#include "types.h"

#include "driver_listener.h"

typedef struct _listener_client_t
{
  int                s;
  char              *address;
  uint16_t           port;
  uint16_t           session_id;
  driver_listener_t *driver;

  struct _listener_client_t *next;
} client_entry_t;

static client_entry_t *first_client = NULL;

static SELECT_RESPONSE_t client_recv(void *group, int socket, uint8_t *data, size_t length, char *addr, uint16_t port, void *c)
{
  client_entry_t *client = (client_entry_t*) c;

  message_post_data_out(client->session_id, data, length);

  return SELECT_OK;
}

static SELECT_RESPONSE_t client_closed(void *group, int socket, void *c)
{
  client_entry_t *client = (client_entry_t*) c;

  message_post_close_session(client->session_id);

  /* TODO: Unlink it from the entry list. */

  return SELECT_CLOSE_REMOVE;
}

static SELECT_RESPONSE_t listener_closed(void *group, int socket, void *c)
{
  LOG_FATAL("Listener socket went away!");
  exit(1);

  return SELECT_CLOSE_REMOVE;
}

static SELECT_RESPONSE_t listener_accept(void *group, int s, void *d)
{
  driver_listener_t *driver = (driver_listener_t*) d;
  client_entry_t *client = safe_malloc(sizeof(client_entry_t));

  client->s          = tcp_accept(s, &client->address, &client->port);
  if(driver->tunnel_host)
    client->session_id = message_post_create_session_with_tunnel(driver->tunnel_host, driver->tunnel_port);
  else
    client->session_id = message_post_create_session();
  client->driver     = driver;
  client->next       = first_client;
  first_client       = client;

  LOG_WARNING("Received a connection from %s:%d (created session %d)", client->address, client->port, client->session_id);

  select_group_add_socket(group, client->s, SOCKET_TYPE_STREAM, client);
  select_set_recv(group, client->s, client_recv);
  select_set_closed(group, client->s, client_closed);

  return SELECT_OK;
}

/* This is called after the drivers are created, to kick things off. */
static void handle_start(driver_listener_t *driver)
{
  driver->s = tcp_listen(driver->host, driver->port);
  if(!driver->s)
  {
    LOG_FATAL("Failed to listen on %s:%d", driver->host, driver->port);
    exit(1);
  }

  /* On Linux, the stdin_handle is easy. */
  select_group_add_socket(driver->group, driver->s, SOCKET_TYPE_LISTEN, driver);
  select_set_listen(driver->group, driver->s, listener_accept);
  select_set_closed(driver->group, driver->s, listener_closed);
}

static void handle_session_closed(driver_listener_t *driver, uint16_t session_id)
{
  client_entry_t *client;

  for(client = first_client; client; client = client->next)
  {
    if(client->session_id == session_id)
    {
      tcp_close(client->s);
      return;
    }
  }

  LOG_WARNING("Couldn't find listener to close: session %d", session_id);
}

static void handle_data_in(driver_listener_t *driver, uint16_t session_id, uint8_t *data, size_t length)
{
  client_entry_t *client;

  for(client = first_client; client; client = client->next)
  {
    if(client->session_id == session_id)
    {
      tcp_send(client->s, data, length);
      return;
    }
  }

  LOG_WARNING("Couldn't find listener to send data to: %d bytes to session %d", length, session_id);
}

static void handle_shutdown()
{
  /* TODO: Clean up. */
}

static void handle_message(message_t *message, void *d)
{
  driver_listener_t *driver = (driver_listener_t*) d;

  switch(message->type)
  {
    case MESSAGE_START:
      handle_start(driver);
      break;

    case MESSAGE_SESSION_CLOSED:
      handle_session_closed(driver, message->message.session_closed.session_id);
      break;

    case MESSAGE_DATA_IN:
      handle_data_in(driver, message->message.data_in.session_id, message->message.data_in.data, message->message.data_in.length);
      break;

    case MESSAGE_SHUTDOWN:
      handle_shutdown();
      break;

    default:
      LOG_FATAL("driver_listener received an invalid message!");
      abort();
  }
}

driver_listener_t *driver_listener_create(select_group_t *group, char *host, int port)
{
  driver_listener_t *driver = (driver_listener_t*) safe_malloc(sizeof(driver_listener_t));

  driver->group = group;
  driver->host  = host;
  driver->port  = port;
  driver->tunnel_host = NULL;
  driver->tunnel_port = -1;

  /* Subscribe to the messages we care about. */
  message_subscribe(MESSAGE_START,           handle_message, driver);
  message_subscribe(MESSAGE_SESSION_CLOSED,  handle_message, driver);
  message_subscribe(MESSAGE_DATA_IN,         handle_message, driver);
  message_subscribe(MESSAGE_SHUTDOWN,        handle_message, driver);

  return driver;
}

void driver_listener_set_tunnel(driver_listener_t *driver, char *host, uint16_t port)
{
  driver->tunnel_host = host;
  driver->tunnel_port = port;
}

void driver_listener_destroy(driver_listener_t *driver)
{
  safe_free(driver);
}
