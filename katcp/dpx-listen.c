#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

#define LISTEN_MAGIC 0xea2002ea

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_listener_katcl(struct katcp_listener *kl)
{
  if(kl == NULL){
    fprintf(stderr, "major logic problem: expected a listener, got NULL\n");
    abort();
  }

  if(kl->l_magic != LISTEN_MAGIC){
    fprintf(stderr, "major logic problem: expected a listener structure with magic 0x%x, got 0x%x\n", LISTEN_MAGIC, kl->l_magic);
    abort();
  }
}
#else
#define sane_listener_katcl(kl)
#endif

struct katcp_listener *allocate_listener_katcp(struct katcp_dispatch *d)
{
  struct katcp_listener *kl;

  kl = malloc(sizeof(struct katcp_listener));
  if(kl == NULL){
    return NULL;
  }

  kl->l_port = 0;
  kl->l_address = NULL;
  kl->l_group = NULL;

  kl->l_magic = LISTEN_MAGIC;

  return kl;
}

void release_listener_katcp(struct katcp_dispatch *d, struct katcp_listener *kl)
{
  sane_listener_katcl(kl);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "releasing listener on port %u for group %s", kl->l_port, kl->l_group ? (kl->l_group->g_name ? kl->l_group->g_name : "<unnamed>") : "<none>");

  if(kl->l_address){
    free(kl->l_address);
    kl->l_address = NULL;
  }

  if(kl->l_group){
    /* WARNING: destroy may just dereference things */
    destroy_group_katcp(d, kl->l_group);
    kl->l_group = NULL;
  }

  kl->l_port = 0;

  free(kl);
}

/* new connection handling routines *****************************/

int accept_flat_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
#define LABEL_BUFFER 32
  int fd, nfd;
  unsigned int len;
  struct sockaddr_in sa;
  struct katcp_flat *f;
  struct katcp_listener *kl;
  char label[LABEL_BUFFER];
  long opts;
  int result;

  result = 0;

  if(mode & KATCP_ARB_READ){

    fd = fileno_arb_katcp(d, a);

    len = sizeof(struct sockaddr_in);
    nfd = accept(fd, (struct sockaddr *) &sa, &len);

    if(nfd < 0){
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "accept on %s failed: %s", name_arb_katcp(d, a), strerror(errno));
      return 0;
    }

    kl = data_arb_katcp(d, a);

    sane_listener_katcl(kl);

    if(kl->l_group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group needed for accept");
      close(fd);
      return 0;
    }

    opts = fcntl(nfd, F_GETFL, NULL);
    if(opts >= 0){
      opts = fcntl(nfd, F_SETFL, opts | O_NONBLOCK);
    }

    fcntl(nfd, F_SETFD, FD_CLOEXEC);

    snprintf(label, LABEL_BUFFER, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
    label[LABEL_BUFFER - 1] = '\0';

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "accepted new connection from %s via %s", label, name_arb_katcp(d, a));

    f = create_flat_katcp(d, nfd, KATCP_FLAT_TOCLIENT, label, kl->l_group);
    if(f == NULL){
      close(nfd);
    }

  }

  if(mode & KATCP_ARB_STOP){
    kl = data_arb_katcp(d, a);

    sane_listener_katcl(kl);

    release_listener_katcp(d, kl);
  }
  
  if(mode & KATCP_ARB_WRITE){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "listener callback called in mode 0x%x", mode);
    result = (-1);
  }

  return result;
#undef LABEL_BUFFER
}

int destroy_listen_flat_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_arb *a;

  a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
  if(a == NULL){
    return -1;
  }

  return unlink_arb_katcp(d, a);
}

struct katcp_arb *create_listen_flat_katcp(struct katcp_dispatch *d, char *name, unsigned int port, char *address, struct katcp_group *g)
{
  int fd, p;
  struct katcp_arb *a;
  struct katcp_group *gx;
  struct katcp_listener *kl;
  struct sockaddr_in sa;
  unsigned int len;
  long opts;
  struct katcp_shared *s;
  char *copy;

  s = d->d_shared;

  gx = (g != NULL) ? g : s->s_fallback;

  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group given, no duplex instances can be created");
    return NULL;
  }

  if(address){
    copy = strdup(address);
    if(copy == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation failure");
      return NULL;
    }
  } else {
    copy = NULL;
  }

  /* TODO: net_listen needs a flag to allocate something random */
  fd = net_listen(copy, port, NETC_AUTO_PORT);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %u at %s: %s", port, copy ? copy : "0.0.0.0", strerror(errno));
    if(copy){
      free(copy);
    }
    return NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "started listener on %u at %s", port, copy ? copy : "0.0.0.0");

  opts = fcntl(fd, F_GETFL, NULL);
  if(opts >= 0){
    opts = fcntl(fd, F_SETFL, opts | O_NONBLOCK);
  }

  if(port == 0){
    len = sizeof(struct sockaddr_in);
    if(getsockname(fd, (struct sockaddr *)&sa, &len) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve system selected port: %s", strerror(errno));
      if(copy){
        free(copy);
      }
      close(fd);
      return NULL;
    }
    p = ntohs(sa.sin_port);
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "system selected port %d", p);
  } else {
    p = port;
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);

  kl = allocate_listener_katcp(d);
  if(kl == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate listener");
    if(copy){
      free(copy);
    }
    close(fd);
    return NULL;
  }

  kl->l_group = gx;
  kl->l_port = p;
  if(copy){
    kl->l_address = copy;
  }

  hold_group_katcp(kl->l_group);

  a = create_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER, fd, KATCP_ARB_READ | KATCP_ARB_STOP, &accept_flat_katcp, kl);
  if(a == NULL){
    release_listener_katcp(d, kl);
    close(fd);
    return NULL;
  }

  return a;
}


#endif /* experimental */
