/* dnscat.c
 * Created March/2013
 * By Ron Bowes
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>

#include "buffer.h"
#include "dns.h"
#include "log.h"
#include "memory.h"
#include "message.h"
#include "select_group.h"
#include "session.h"
#include "udp.h"

#include "driver_console.h"
#include "driver_dns.h"
#include "driver_exec.h"
#include "driver_listener.h"
#include "driver_socks4.h"

/* Default options */
#define VERSION "0.00"

/* Default options */
#define DEFAULT_DNS_HOST NULL
#define DEFAULT_DNS_PORT 53

/* Define these outside the function so they can be freed by the atexec() */
select_group_t   *group          = NULL;

/* Input drivers. */
driver_console_t  *driver_console  = NULL;
driver_exec_t     *driver_exec     = NULL;
driver_listener_t *driver_listener = NULL;
driver_socks4_t   *driver_socks4   = NULL;

/* Output drivers. */
driver_dns_t     *driver_dns     = NULL;

static SELECT_RESPONSE_t timeout(void *group, void *param)
{
  message_post_heartbeat();

  return SELECT_OK;
}

static void cleanup()
{
  LOG_WARNING("Terminating");

  message_post_shutdown();
  message_cleanup();

  if(group)
    select_group_destroy(group);

  if(driver_console)
    driver_console_destroy(driver_console);
  if(driver_dns)
    driver_dns_destroy(driver_dns);
  if(driver_exec)
    driver_exec_destroy(driver_exec);
  if(driver_listener)
    driver_listener_destroy(driver_listener);
  if(driver_socks4)
    driver_socks4_destroy(driver_socks4);

  print_memory();
}

void usage(char *name, char *message)
{
  fprintf(stderr,
"Usage: %s [args] [domain]\n"
"\n"

"General options:\n"
" --help -h               This page\n"
" --name -n <name>        Give this connection a name, which will show up in\n"
"                         the server list\n"
" --tunnel <host:port>    Requests the server to forward all messages to the\n"
"                         given server and port on the user's behalf.\n"
"\n"
"Input options:\n"
" --console --stdin       Send/receive output to the console [default]\n"
" --exec -e <process>     Execute the given process and link it to the stream\n"
" --listen -l <port>      Listen on the given port and link each connection to\n"
"                         a new stream\n"
" --socks4 <port>         Expose a SOCKS4 proxy on the give port\n"
"\n"

"DNS-specific options:\n"
" --dns <domain>          Enable DNS mode with the given domain\n"
" --host <host>           The DNS server [default: %s]\n"
" --port <port>           The DNS port [default: 53]\n"
"\n"

"Debug options:\n"
" -d                      Display more debug info (can be used multiple times)\n"
" -q                      Display less debug info (can be used multiple times)\n"
"\n"
"%s\n"
"\n"
, name, dns_get_system(), message
);
  exit(0);
}

int main(int argc, char *argv[])
{
  /* Define the options specific to the DNS protocol. */
  struct option long_options[] =
  {
    /* General options */
    {"help",    no_argument,       0, 0}, /* Help */
    {"h",       no_argument,       0, 0},
    {"name",    required_argument, 0, 0}, /* Name */
    {"n",       required_argument, 0, 0},
    {"tunnel",  required_argument, 0, 0}, /* Tunnel */

    /* Console options. */
    {"stdin",   no_argument,       0, 0}, /* Enable console (default) */
    {"console", no_argument,       0, 0}, /* (alias) */

    /* Execute-specific options. */
    {"exec",    required_argument, 0, 0}, /* Enable execute */
    {"e",       required_argument, 0, 0},

    /* Listener options */
    {"listen",  required_argument, 0, 0}, /* Enable listener */
    {"l",       required_argument, 0, 0},

    /* SOCKS4 options */
    {"socks4",  required_argument, 0, 0}, /* Enable socks4 */

    /* DNS-specific options */
    {"dns",        required_argument, 0, 0}, /* Enable DNS (default) */
    {"dnshost",    required_argument, 0, 0}, /* DNS server */
    {"host",       required_argument, 0, 0}, /* (alias) */
    {"dnsport",    required_argument, 0, 0}, /* DNS port */
    {"port",       required_argument, 0, 0}, /* (alias) */

    /* Debug options */
    {"d",       no_argument,       0, 0}, /* More debug */
    {"q",       no_argument,       0, 0}, /* Less debug */
    {0,         0,                 0, 0}  /* End */
  };

  /* Define DNS options so we can set them later. */
  struct {
    char     *host;
    uint16_t  port;
  } dns_options = { DEFAULT_DNS_HOST, DEFAULT_DNS_PORT };

  struct {
    char    *host;
    uint16_t port;
  } tunnel = { NULL, -1 };

  char              c;
  int               option_index;
  const char       *option_name;

  NBBOOL            input_set = FALSE;
  NBBOOL            output_set = FALSE;

  log_level_t       min_log_level = LOG_LEVEL_WARNING;

  /* Initialize the modules that need initialization. */
  log_init();
  sessions_init();

  group = select_group_create();
#if 0
  driver_console = driver_console_create(group);
  /*driver_exec    = driver_exec_create(group, "cmd.exe");*/
  driver_dns     = driver_dns_create(group);
#endif

  srand(time(NULL));

  /* Set the default log level */
  log_set_min_console_level(min_log_level);

  /* Parse the command line options. */
  opterr = 0;
  while((c = getopt_long_only(argc, argv, "", long_options, &option_index)) != EOF)
  {
    switch(c)
    {
      case 0:
        option_name = long_options[option_index].name;

        /* General options */
        if(!strcmp(option_name, "help") || !strcmp(option_name, "h"))
        {
          usage(argv[0], "--help requested");
        }
        else if(!strcmp(option_name, "name") || !strcmp(option_name, "n"))
        {
          /* TODO: Handle 'name' again */
          /*options->name = optarg;*/
        }
        else if(!strcmp(option_name, "tunnel"))
        {
          char *colon = strchr(optarg, ':');

          if(!colon)
            usage(argv[0], "--tunnel must be in the format <host:port>");

          *colon = '\0';
          tunnel.host = optarg;
          tunnel.port = atoi(colon + 1);
        }

        /* Console-specific options. */
        else if(!strcmp(option_name, "stdin"))
        {
          if(input_set)
            usage(argv[0], "More than one of --exec, --stdin, --listen, and --socks4 can't be set!");

          input_set = TRUE;
          driver_console = driver_console_create(group);
        }

        /* Execute options. */
        else if(!strcmp(option_name, "exec") || !strcmp(option_name, "e"))
        {
          if(input_set)
            usage(argv[0], "More than one of --exec, --stdin, --listen, and --socks4 can't be set!");

          input_set = TRUE;
          driver_exec = driver_exec_create(group, optarg);
        }

        /* Listener options. */
        else if(!strcmp(option_name, "listen") || !strcmp(option_name, "l"))
        {
          if(input_set)
            usage(argv[0], "More than one of --exec, --stdin, --listen, and --socks4 can't be set!");

          input_set = TRUE;
          driver_listener = driver_listener_create(group, "0.0.0.0", atoi(optarg));
        }

        else if(!strcmp(option_name, "socks4"))
        {
          if(input_set)
            usage(argv[0], "More than one of --exec, --stdin, --listen, and --socks4 can't be set!");

          input_set = TRUE;
          driver_socks4 = driver_socks4_create(group, "0.0.0.0", atoi(optarg));
        }

        /* DNS-specific options */
        else if(!strcmp(option_name, "dns"))
        {
          if(output_set)
            usage(argv[0], "More than one of --dns and --tcp can't be set!");

          output_set = TRUE;
          driver_dns = driver_dns_create(group, optarg);
        }
        else if(!strcmp(option_name, "dnshost") || !strcmp(option_name, "host"))
        {
          dns_options.host = optarg;
        }
        else if(!strcmp(option_name, "dnsport") || !strcmp(option_name, "port"))
        {
          dns_options.port = atoi(optarg);
        }

        /* Debug options */
        else if(!strcmp(option_name, "d"))
        {
          min_log_level--;
          log_set_min_console_level(min_log_level);
        }
        else if(!strcmp(option_name, "q"))
        {
          min_log_level++;
          log_set_min_console_level(min_log_level);
        }
        else
        {
          usage(argv[0], "Unknown option");
        }
        break;

      case '?':
      default:
        /* Do nothing; we expect some unknown arguments. */
        break;
    }
  }

  /* If no input was created, default to console. */
  if(!input_set)
    driver_console = driver_console_create(group);

  /* If no output was set, use the domain, and use the last option as the
   * domain. */
  if(!output_set)
  {
    /* Make sure they gave a domain. */
    if(optind >= argc)
    {
      usage(argv[0], "Please provide a domain (either with --dns or at the end of the commandline)");
      exit(1);
    }
    driver_dns = driver_dns_create(group, argv[optind]);
  }

  if(driver_console)
  {
    LOG_WARNING("INPUT: Console");
    if(tunnel.host)
      driver_console_set_tunnel(driver_console, tunnel.host, tunnel.port);
  }
  else if(driver_listener)
  {
    LOG_WARNING("INPUT: Listening on port %d", driver_listener->port);
    if(tunnel.host)
      driver_listener_set_tunnel(driver_listener, tunnel.host, tunnel.port);
  }
  else if(driver_socks4)
  {
    LOG_WARNING("INPUT: SOCKS4 listening on port %d", driver_socks4->port);
  }
  else if(driver_exec)
  {
    LOG_WARNING("INPUT: Executing %s", driver_exec->process);
  }
  else
  {
    LOG_FATAL("INPUT: Ended up with an unknown input driver!");
    exit(1);
  }

  if(driver_dns)
  {
    if(dns_options.host == DEFAULT_DNS_HOST)
      driver_dns->dns_host = dns_get_system();
    else
      driver_dns->dns_host = safe_strdup(dns_options.host);

    driver_dns->dns_port = dns_options.port;
    LOG_WARNING("OUTPUT: DNS tunnel to %s", driver_dns->domain);
  }
  else
  {
    LOG_FATAL("OUTPUT: Ended up with an unknown output driver!");
    exit(1);
  }

  /* Be sure we clean up at exit. */
  atexit(cleanup);

  /* Kick things off */
  message_post_start();

  /* Add the timeout function */
  select_set_timeout(group, timeout, NULL);
  while(TRUE)
    select_group_do_select(group, 1000);

  return 0;
}
