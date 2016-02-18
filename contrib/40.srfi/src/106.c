#include "picrin.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

struct pic_socket_t {
  int fd;
};

PIC_INLINE void
socket_close(struct pic_socket_t *sock)
{
  if (sock != NULL && sock->fd != -1) {
    close(sock->fd);
    sock->fd = -1;
  }
}

PIC_INLINE void
ensure_socket_is_open(pic_state *pic, struct pic_socket_t *sock)
{
  if (sock != NULL && sock->fd == -1) {
    pic_errorf(pic, "the socket is already closed");
  }
}

static void
socket_dtor(pic_state *pic, void *data)
{
  struct pic_socket_t *sock;

  sock = data;
  socket_close(sock);
  pic_free(pic, data);
}

static const pic_data_type socket_type = { "socket", socket_dtor, NULL };

#define pic_socket_p(pic, o) (pic_data_type_p(pic, (o), &socket_type))
#define pic_socket_data_ptr(o) ((struct pic_socket_t *)pic_data_ptr(o)->data)

PIC_INLINE void
validate_socket_object(pic_state *pic, pic_value v)
{
  if (! pic_socket_p(pic, v)) {
    pic_errorf(pic, "~s is not a socket object", v);
  }
}

static pic_value
pic_socket_socket_p(pic_state *pic)
{
  pic_value obj;

  pic_get_args(pic, "o", &obj);
  return pic_bool_value(pic, pic_socket_p(pic, obj));
}

static pic_value
pic_socket_make_socket(pic_state *pic)
{
  const char *node, *service;
  int family, socktype, flags, protocol;
  int result;
  struct addrinfo hints, *ai, *it;
  struct pic_socket_t *sock;

  pic_get_args(pic, "zziiii", &node, &service, &family, &socktype, &flags, &protocol);

  if (strlen(node) == 0) node = NULL;
  if (strlen(service) == 0) service = NULL;

  sock = pic_malloc(pic, sizeof(struct pic_socket_t));
  sock->fd = -1;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = socktype;
  hints.ai_flags = flags;
  hints.ai_protocol = protocol;

  errno = 0;

  do {
    result = getaddrinfo(node, service, &hints, &ai);
  } while (result == EAI_AGAIN);
  if (result) {
    if (result == EAI_SYSTEM) {
      pic_errorf(pic, "%s", strerror(errno));
    }
    pic_errorf(pic, "%s", gai_strerror(result));
  }

  for (it = ai; it != NULL; it = it->ai_next) {
    int fd;

    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd == -1) {
      continue;
    }

    if (hints.ai_flags & AI_PASSIVE) {
      int yes = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == 0 &&
          bind(fd, it->ai_addr, it->ai_addrlen) == 0) {
        if (it->ai_socktype == SOCK_STREAM ||
            it->ai_socktype == SOCK_SEQPACKET) {
          /* TODO: Backlog should be configurable. */
          if (listen(fd, 8) == 0) {
              sock->fd = fd;
              break;
          }
        } else {
          sock->fd = fd;
          break;
        }
      }
    } else {
      if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
        sock->fd = fd;
        break;
      }
    }

    close(fd);
  }

  freeaddrinfo(ai);

  if (sock->fd == -1) {
    pic_errorf(pic, "%s", strerror(errno));
  }

  return pic_obj_value(pic_data_value(pic, sock, &socket_type));
}

static pic_value
pic_socket_socket_accept(pic_state *pic)
{
  pic_value obj;
  int fd = -1;
  struct pic_socket_t *sock, *new_sock;

  pic_get_args(pic, "o", &obj);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  errno = 0;
  while (1) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    fd = accept(sock->fd, (struct sockaddr *)&addr, &addrlen);

    if (fd < 0) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      } else {
        pic_errorf(pic, "%s", strerror(errno));
      }
    } else {
      break;
    }
  }

  new_sock = pic_malloc(pic, sizeof(struct pic_socket_t));
  new_sock->fd = fd;
  return pic_obj_value(pic_data_value(pic, new_sock, &socket_type));
}

static pic_value
pic_socket_socket_send(pic_state *pic)
{
  pic_value obj;
  struct pic_blob *bv;
  const unsigned char *cursor;
  int flags = 0;
  size_t remain, written;
  struct pic_socket_t *sock;

  pic_get_args(pic, "ob|i", &obj, &bv, &flags);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  cursor = bv->data;
  remain = bv->len;
  written = 0;
  errno = 0;
  while (remain > 0) {
    ssize_t len = send(sock->fd, cursor, remain, flags);
    if (len < 0) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        pic_errorf(pic, "%s", strerror(errno));
      }
    }
    cursor += len;
    remain -= len;
    written += len;
  }

  return pic_int_value(pic, written);
}

static pic_value
pic_socket_socket_recv(pic_state *pic)
{
  pic_value obj;
  struct pic_blob *bv;
  void *buf;
  int size;
  int flags = 0;
  ssize_t len;
  struct pic_socket_t *sock;

  pic_get_args(pic, "oi|i", &obj, &size, &flags);
  validate_socket_object(pic, obj);
  if (size < 0) {
    pic_errorf(pic, "size must not be negative");
  }

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  buf = malloc(size);
  if (buf == NULL && size > 0) {
    /* XXX: Is it really OK? */
    pic_panic(pic, "memory exhausted");
  }

  errno = 0;
  do {
    len = recv(sock->fd, buf, size, flags);
  } while (len < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));

  if (len < 0) {
    free(buf);
    pic_errorf(pic, "%s", strerror(errno));
  }

  bv = pic_blob_value(pic, buf, len);
  free(buf);

  return pic_obj_value(bv);
}

static pic_value
pic_socket_socket_shutdown(pic_state *pic)
{
  pic_value obj;
  int how;
  struct pic_socket_t *sock;

  pic_get_args(pic, "oi", &obj, &how);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  if (sock->fd != -1) {
    shutdown(sock->fd, how);
    sock->fd = -1;
  }

  return pic_undef_value(pic);
}

static pic_value
pic_socket_socket_close(pic_state *pic)
{
  pic_value obj;

  pic_get_args(pic, "o", &obj);
  validate_socket_object(pic, obj);

  socket_close(pic_socket_data_ptr(obj));

  return pic_undef_value(pic);
}

static int
xf_socket_read(pic_state PIC_UNUSED(*pic), void *cookie, char *ptr, int size)
{
  struct pic_socket_t *sock;

  sock = (struct pic_socket_t *)cookie;

  return recv(sock->fd, ptr, size, 0);
}

static int
xf_socket_write(pic_state PIC_UNUSED(*pic), void *cookie, const char *ptr, int size)
{
  struct pic_socket_t *sock;

  sock = (struct pic_socket_t *)cookie;

  return send(sock->fd, ptr, size, 0);
}

static long
xf_socket_seek(pic_state PIC_UNUSED(*pic), void PIC_UNUSED(*cookie), long PIC_UNUSED(pos), int PIC_UNUSED(whence))
{
  errno = EBADF;
  return -1;
}

static int
xf_socket_close(pic_state PIC_UNUSED(*pic), void PIC_UNUSED(*cookie))
{
  return 0;
}

static struct pic_port *
make_socket_port(pic_state *pic, struct pic_socket_t *sock, short dir)
{
  struct pic_port *port;

  port = (struct pic_port *)pic_obj_alloc(pic, sizeof(struct pic_port), PIC_TYPE_PORT);
  port->file = xfunopen(pic, sock, xf_socket_read, xf_socket_write, xf_socket_seek, xf_socket_close);
  port->flags = dir | PIC_PORT_BINARY | PIC_PORT_OPEN;
  return port;
}

static pic_value
pic_socket_socket_input_port(pic_state *pic)
{
  pic_value obj;
  struct pic_socket_t *sock;

  pic_get_args(pic, "o", &obj);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  return pic_obj_value(make_socket_port(pic, sock, PIC_PORT_IN));
}

static pic_value
pic_socket_socket_output_port(pic_state *pic)
{
  pic_value obj;
  struct pic_socket_t *sock;

  pic_get_args(pic, "o", &obj);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  return pic_obj_value(make_socket_port(pic, sock, PIC_PORT_OUT));
}

static pic_value
pic_socket_call_with_socket(pic_state *pic)
{
  pic_value obj, result;
  struct pic_proc *proc;
  struct pic_socket_t *sock;

  pic_get_args(pic, "ol", &obj, &proc);
  validate_socket_object(pic, obj);

  sock = pic_socket_data_ptr(obj);
  ensure_socket_is_open(pic, sock);

  result = pic_call(pic, proc, 1, obj);

  socket_close(sock);

  return result;
}

void
pic_init_srfi_106(pic_state *pic)
{
  pic_deflibrary(pic, "srfi.106");

#define pic_defun_(pic, name, f) pic_define(pic, "srfi.106", name, pic_obj_value(pic_make_proc(pic, f, 0, NULL)))
#define pic_define_(pic, name, v) pic_define(pic, "srfi.106", name, v)

  pic_defun_(pic, "socket?", pic_socket_socket_p);
  pic_defun_(pic, "make-socket", pic_socket_make_socket);
  pic_defun_(pic, "socket-accept", pic_socket_socket_accept);
  pic_defun_(pic, "socket-send", pic_socket_socket_send);
  pic_defun_(pic, "socket-recv", pic_socket_socket_recv);
  pic_defun_(pic, "socket-shutdown", pic_socket_socket_shutdown);
  pic_defun_(pic, "socket-close", pic_socket_socket_close);
  pic_defun_(pic, "socket-input-port", pic_socket_socket_input_port);
  pic_defun_(pic, "socket-output-port", pic_socket_socket_output_port);
  pic_defun_(pic, "call-with-socket", pic_socket_call_with_socket);

#ifdef AF_INET
  pic_define_(pic, "*af-inet*", pic_int_value(pic, AF_INET));
#else
  pic_define_(pic, "*af-inet*", pic_false_value(pic));
#endif
#ifdef AF_INET6
  pic_define_(pic, "*af-inet6*", pic_int_value(pic, AF_INET6));
#else
  pic_define_(pic, "*af-inet6*", pic_false_value(pic));
#endif
#ifdef AF_UNSPEC
  pic_define_(pic, "*af-unspec*", pic_int_value(pic, AF_UNSPEC));
#else
  pic_define_(pic, "*af-unspec*", pic_false_value(pic));
#endif

#ifdef SOCK_STREAM
  pic_define_(pic, "*sock-stream*", pic_int_value(pic, SOCK_STREAM));
#else
  pic_define_(pic, "*sock-stream*", pic_false_value(pic));
#endif
#ifdef SOCK_DGRAM
  pic_define_(pic, "*sock-dgram*", pic_int_value(pic, SOCK_DGRAM));
#else
  pic_define_(pic, "*sock-dgram*", pic_false_value(pic));
#endif

#ifdef AI_CANONNAME
  pic_define_(pic, "*ai-canonname*", pic_int_value(pic, AI_CANONNAME));
#else
  pic_define_(pic, "*ai-canonname*", pic_false_value(pic));
#endif
#ifdef AI_NUMERICHOST
  pic_define_(pic, "*ai-numerichost*", pic_int_value(pic, AI_NUMERICHOST));
#else
  pic_define_(pic, "*ai-numerichost*", pic_false_value(pic));
#endif
  /* AI_V4MAPPED and AI_ALL are not supported by *BSDs, even though they are defined in netdb.h. */
#if defined(AI_V4MAPPED) && !defined(BSD)
  pic_define_(pic, "*ai-v4mapped*", pic_int_value(pic, AI_V4MAPPED));
#else
  pic_define_(pic, "*ai-v4mapped*", pic_false_value(pic));
#endif
#if defined(AI_ALL) && !defined(BSD)
  pic_define_(pic, "*ai-all*", pic_int_value(pic, AI_ALL));
#else
  pic_define_(pic, "*ai-all*", pic_false_value(pic));
#endif
#ifdef AI_ADDRCONFIG
  pic_define_(pic, "*ai-addrconfig*", pic_int_value(pic, AI_ADDRCONFIG));
#else
  pic_define_(pic, "*ai-addrconfig*", pic_false_value(pic));
#endif
#ifdef AI_PASSIVE
  pic_define_(pic, "*ai-passive*", pic_int_value(pic, AI_PASSIVE));
#else
  pic_define_(pic, "*ai-passive*", pic_false_value(pic));
#endif

#ifdef IPPROTO_IP
  pic_define_(pic, "*ipproto-ip*", pic_int_value(pic, IPPROTO_IP));
#else
  pic_define_(pic, "*ipproto-ip*", pic_false_value(pic));
#endif
#ifdef IPPROTO_TCP
  pic_define_(pic, "*ipproto-tcp*", pic_int_value(pic, IPPROTO_TCP));
#else
  pic_define_(pic, "*ipproto-tcp*", pic_false_value(pic));
#endif
#ifdef IPPROTO_UDP
  pic_define_(pic, "*ipproto-udp*", pic_int_value(pic, IPPROTO_UDP));
#else
  pic_define_(pic, "*ipproto-udp*", pic_false_value(pic));
#endif

#ifdef MSG_PEEK
  pic_define_(pic, "*msg-peek*", pic_int_value(pic, MSG_PEEK));
#else
  pic_define_(pic, "*msg-peek*", pic_false_value(pic));
#endif
#ifdef MSG_OOB
  pic_define_(pic, "*msg-oob*", pic_int_value(pic, MSG_OOB));
#else
  pic_define_(pic, "*msg-oob*", pic_false_value(pic));
#endif
#ifdef MSG_WAITALL
  pic_define_(pic, "*msg-waitall*", pic_int_value(pic, MSG_WAITALL));
#else
  pic_define_(pic, "*msg-waitall*", pic_false_value(pic));
#endif

#ifdef SHUT_RD
  pic_define_(pic, "*shut-rd*", pic_int_value(pic, SHUT_RD));
#else
  pic_define_(pic, "*shut-rd*", pic_false_value(pic));
#endif
#ifdef SHUT_WR
  pic_define_(pic, "*shut-wr*", pic_int_value(pic, SHUT_WR));
#else
  pic_define_(pic, "*shut-wr*", pic_false_value(pic));
#endif
#ifdef SHUT_RDWR
  pic_define_(pic, "*shut-rdwr*", pic_int_value(pic, SHUT_RDWR));
#else
  pic_define_(pic, "*shut-rdwr*", pic_false_value(pic));
#endif
}
