#ifndef __DRIVER_LISTENER_H__
#define __DRIVER_LISTENER_H__

#include "message.h"
#include "select_group.h"
#include "session.h"

typedef struct
{
  int             s;
  select_group_t *group;
  char           *host;
  uint16_t        port;

  char           *tunnel_host;
  uint16_t        tunnel_port;
} driver_listener_t;

driver_listener_t *driver_listener_create(select_group_t *group, char *host, int port);
void               driver_listener_set_tunnel(driver_listener_t *driver, char *host, uint16_t port);
void               driver_listener_destroy();

#endif
