#include "ruby.h"
#include "ruby/io.h"
#include "ruby/thread.h"

#ifdef HAVE_SYS_EPOLL_H

#include <sys/epoll.h>
#include <fcntl.h>

#define EPOLL_WAIT_MAX_EVENTS 256

VALUE cEpoll;
VALUE cEpoll_Constants;
VALUE cEpoll_Event;

static VALUE
rb_epoll_initialize(VALUE self)
{
  rb_io_t *fp;
  int fd;

#if defined(HAVE_EPOLL_CREATE1) && defined(EPOLL_CLOEXEC)
  fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd == -1)
    rb_sys_fail("epoll_create1(2) was failed");
#else
  fd = epoll_create(1024);
  if (fd == -1)
    rb_sys_fail("epoll_create(2) was failed");
#endif
  rb_update_max_fd(fd);

  MakeOpenFile(self, fp);
  fp->fd = fd;
  fp->mode = FMODE_READABLE|FMODE_BINMODE;
  rb_io_ascii8bit_binmode(self);

  /**
  * It's also using GC mark.
  * So, I don't know other way how to GC guard io objects.
  */
  rb_ivar_set(self, rb_intern("@evlist"), rb_ary_new());
  return self;
}

static VALUE
rb_epoll_ctl(int argc, VALUE *argv, VALUE self)
{
  struct epoll_event ev;
  VALUE flag;
  VALUE io;
  VALUE events;
  VALUE evlist;
  rb_io_t *fptr;
  rb_io_t *fptr_io;
  int fd;

  fptr = RFILE(self)->fptr;
  rb_io_check_initialized(fptr);

  switch (rb_scan_args(argc, argv, "21", &flag, &io, &events)) {
    case 2:
      if (FIX2INT(flag) != EPOLL_CTL_DEL)
        rb_raise(rb_eArgError, "too few argument for CTL_ADD or CTL_MOD");
    break;
    case 3:
      if (FIX2INT(flag) == EPOLL_CTL_DEL)
        rb_raise(rb_eArgError, "too many argument for CTL_DEL");

      if ((FIX2LONG(events) & (EPOLLIN|EPOLLPRI|EPOLLRDHUP|EPOLLOUT|EPOLLET|EPOLLONESHOT)) == 0)
        rb_raise(rb_eIOError, "undefined events");

      ev.events = FIX2LONG(events);
      ev.data.ptr = (void*)io;
    break;
  }

  GetOpenFile(rb_io_get_io(io), fptr_io);
  fd = fptr_io->fd;

  if (epoll_ctl(fptr->fd, FIX2INT(flag), fd, &ev) == -1) {
    char buf[128];
    sprintf(buf, "epoll_ctl(2) was failed(epoll fd:%d, io fd:%d)", fptr->fd, fd);
    rb_sys_fail(buf);
  }

  switch (FIX2INT(flag)) {
    case EPOLL_CTL_ADD:
      evlist = rb_ivar_get(self, rb_intern("@evlist"));
      rb_ary_push(evlist, io);
      rb_ivar_set(self, rb_intern("@evlist"), evlist);
    break;
    case EPOLL_CTL_DEL:
      evlist = rb_ivar_get(self, rb_intern("@evlist"));
      rb_ary_delete(evlist, io);
      rb_ivar_set(self, rb_intern("@evlist"), evlist);
    break;
  }

  return self;
}

struct epoll_wait_args {
  int fd;
  int ev_len;
  struct epoll_event *evlist;
  int timeout;
};

static void *
rb_epoll_wait_func(void *ptr)
{
  const struct epoll_wait_args *data = ptr;
  return (void*)(long)epoll_wait(data->fd, data->evlist, data->ev_len, data->timeout);
}

static VALUE
rb_epoll_wait(int argc, VALUE *argv, VALUE self)
{
  struct epoll_event evlist[EPOLL_WAIT_MAX_EVENTS];
  struct epoll_wait_args data;
  int i, ready, timeout, ev_len;
  VALUE ready_evlist;
  VALUE event;
  rb_io_t *fptr;
  GetOpenFile(self, fptr);

  switch (argc) {
    case 0:
      timeout = -1;
    break;
    case 1:
      timeout = FIX2INT(argv[0]);
    break;
    default:
      rb_raise(rb_eArgError, "too many argument");
    break;
  }

  ev_len = RARRAY_LEN(rb_ivar_get(self, rb_intern("@evlist")));
  if (ev_len <= 0)
    rb_raise(rb_eIOError, "empty interest list");

  data.fd = fptr->fd;
  data.ev_len = ev_len < EPOLL_WAIT_MAX_EVENTS ? ev_len : EPOLL_WAIT_MAX_EVENTS;
  data.evlist = evlist;
  data.timeout = timeout;

RETRY:
  ready = (int)(long)rb_thread_call_without_gvl(rb_epoll_wait_func, &data, RUBY_UBF_IO, 0);
  if (ready == -1) {
    if (errno == EINTR)
      goto RETRY;
    else
      rb_sys_fail("epoll_wait(2) was failed");
  }

  ready_evlist = rb_ary_new_capa(ready);
  for (i = 0; i < ready; i++) {
    event = rb_obj_alloc(cEpoll_Event);
    RSTRUCT_SET(event, 0, (VALUE) evlist[i].data.ptr);
    RSTRUCT_SET(event, 1, LONG2FIX(evlist[i].events));
    rb_ary_store(ready_evlist, i, event);
  }
  return ready_evlist;
}

#endif // HAVE_SYS_EPOLL_H

void
Init_core()
{
#ifdef HAVE_SYS_EPOLL_H
  cEpoll = rb_define_class("Epoll", rb_cIO);
  rb_define_method(cEpoll, "initialize", rb_epoll_initialize, 0);
  rb_define_method(cEpoll, "ctl", rb_epoll_ctl, -1);
  rb_define_method(cEpoll, "wait", rb_epoll_wait, -1);

  cEpoll_Constants = rb_define_module_under(cEpoll, "Constants");
  rb_define_const(cEpoll_Constants, "IN", INT2FIX(EPOLLIN));
  rb_define_const(cEpoll_Constants, "PRI", INT2FIX(EPOLLPRI));
  rb_define_const(cEpoll_Constants, "RDHUP", INT2FIX(EPOLLRDHUP));
  rb_define_const(cEpoll_Constants, "OUT", INT2FIX(EPOLLOUT));
  rb_define_const(cEpoll_Constants, "ET", INT2FIX(EPOLLET));
  rb_define_const(cEpoll_Constants, "ONESHOT", INT2FIX(EPOLLONESHOT));
  rb_define_const(cEpoll_Constants, "ERR", INT2FIX(EPOLLERR));
  rb_define_const(cEpoll_Constants, "HUP", INT2FIX(EPOLLHUP));
  rb_define_const(cEpoll_Constants, "CTL_ADD", INT2FIX(EPOLL_CTL_ADD));
  rb_define_const(cEpoll_Constants, "CTL_MOD", INT2FIX(EPOLL_CTL_MOD));
  rb_define_const(cEpoll_Constants, "CTL_DEL", INT2FIX(EPOLL_CTL_DEL));

  cEpoll_Event = rb_struct_define_under(cEpoll, "Event", "data", "events", NULL);
#endif
}
