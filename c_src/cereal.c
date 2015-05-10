/*
Copyright 2011, Joe Williams <joe@joetify.com>

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#include <erl_nif.h>
#include <erl_driver.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cereal.h"

int lookup_speed(unsigned int speed, unsigned int *rate);

static ERL_NIF_TERM atom_ok;
static ERL_NIF_TERM atom_error;

static int
load(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info)
{
  atom_ok = enif_make_atom(env, "ok");
  atom_error = enif_make_atom(env, "error");

  return 0;
}

static int
reload(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info)
{
  return 0;
}

static int
upgrade(ErlNifEnv* env, void** priv, void** old_priv, ERL_NIF_TERM load_info)
{
  return 0;
}

static void
unload(ErlNifEnv* env, void* priv)
{
  return;
}

ERL_NIF_TERM
mk_atom(ErlNifEnv* env, const char* atom)
{
  ERL_NIF_TERM ret;

  if(!enif_make_existing_atom(env, atom, &ret, ERL_NIF_LATIN1))
    {
      return enif_make_atom(env, atom);
    }

  return ret;
}

ERL_NIF_TERM
mk_error(ErlNifEnv* env, const char* mesg)
{
  return enif_make_tuple(env, atom_error, mk_atom(env, mesg));
}

static ERL_NIF_TERM
mk_errno(ErlNifEnv *env, int errnum)
{
  return enif_make_tuple2(env, atom_error,
    enif_make_atom(env, erl_errno_id(errnum)));
}


/**********************************************************************
 * Name: set_raw_tty_mode
 *
 * Desc: Configures the given tty for raw-mode.
 */

static ERL_NIF_TERM
set_raw_tty_mode(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
  struct termios ttymodes;

  int fd;

  if (enif_get_int(env, argv[0], &fd) < 1)
    {
      return enif_make_badarg(env);
    }

  /* Get ttymodes */

  if (tcgetattr(fd, &ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  /* Configure for raw mode (see man termios) */
  ttymodes.c_cc[VMIN] = 1;         /* at least one character */
  ttymodes.c_cc[VTIME] = 0;        /* do not wait to fill buffer */

  ttymodes.c_iflag &= ~(ICRNL |    /* disable CR-to-NL mapping */
      INLCR |    /* disable NL-to-CR mapping */
      IGNCR |    /* disable ignore CR */
      ISTRIP |   /* disable stripping of eighth bit */
      IXON |     /* disable output flow control */
      BRKINT |   /* disable generate SIGINT on brk */
      IGNPAR |
      PARMRK |
      IGNBRK |
      INPCK);    /* disable input parity detection */

  ttymodes.c_lflag &= ~(ICANON |   /* enable non-canonical mode */
      ECHO |     /* disable character echo */
      ECHOE |    /* disable visual erase */
      ECHOK |    /* disable echo newline after kill */
      ECHOKE |   /* disable visual kill with bs-sp-bs */
      ECHONL |   /* disable echo nl when echo off */
      ISIG |     /* disable tty-generated signals */
      IEXTEN);   /* disable extended input processing */

  ttymodes.c_cflag |= CS8;         /* enable eight bit chars */
  ttymodes.c_cflag &= ~PARENB;     /* disable input parity check */

  ttymodes.c_oflag &= ~OPOST;      /* disable output processing */

  /* roland */
  ttymodes.c_cflag |= CLOCAL;

  /* Apply changes */

  if (tcsetattr(fd, TCSANOW, &ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  return atom_ok;
}

/**********************************************************************
 * Name: set_tty_speed
 *
 * Desc: set input and output speeds of a given connection.
 */

static ERL_NIF_TERM
set_tty_speed(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
  int fd = -1;
  unsigned int speed = 0;
  unsigned int new_ispeed = 0;
  unsigned int new_ospeed = 0;
  struct termios ttymodes = {0};

  if (enif_get_int(env, argv[0], &fd) < 1)
    {
      return enif_make_badarg(env);
    }

  if (enif_get_uint(env, argv[1], &speed) < 1 ||
          lookup_speed(speed, &new_ispeed) < 0)
    {
      return enif_make_badarg(env);
    }

  if (enif_get_uint(env, argv[2], &speed) < 1 ||
          lookup_speed(speed, &new_ospeed) < 0)
    {
      return enif_make_badarg(env);
    }

  /* Get ttymodes */

  if (tcgetattr(fd,&ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  if (cfsetispeed(&ttymodes,new_ispeed) < 0)
    {
      return mk_errno(env, errno);
    }

  if (cfsetospeed(&ttymodes,new_ospeed) < 0)
    {
      return mk_errno(env, errno);
    }

  /* Apply changes */

  if (tcsetattr(fd, TCSANOW, &ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  return atom_ok;
}


int
lookup_speed(unsigned int speed, unsigned int *rate)
{
    int i = 0;

    for (i = 0; i < sizeof(speeds)/sizeof(struct cereal_speed); i++) {
        if (speeds[i].speed == speed) {
            *rate = speeds[i].rate;
            return 0;
        }
    }

    return -1;
}

/**********************************************************************
 * Name: set_tty_flow
 *
 * Desc: enable/disable hardware flow control
 */

static ERL_NIF_TERM
set_tty_flow(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
  int fd;
  int enable;
  struct termios ttymodes;

  if (enif_get_int(env, argv[0], &fd) < 1)
    {
      return enif_make_badarg(env);
    }

  if (enif_get_int(env, argv[1], &enable) < 1)
    {
      return enif_make_badarg(env);
    }

  /* Get ttymodes */

  if (tcgetattr(fd,&ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  if (enable)
    ttymodes.c_cflag |= CRTSCTS;     /* enable RTS/CTS flow control */
  else
    ttymodes.c_cflag &= ~CRTSCTS;

  /* Apply changes */

  if (tcsetattr(fd, TCSANOW, &ttymodes) < 0)
    {
      return mk_errno(env, errno);
    }

  return atom_ok;
}

/**********************************************************************
 * Name: open_tty
 *
 * Desc: open tty and return fd
 */

static ERL_NIF_TERM
open_tty(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
  char ttyname[MAXPATHLEN];
  int ttyfd = -1;

  if (enif_get_string(env, argv[0], ttyname, sizeof(ttyname), ERL_NIF_LATIN1) < 1)
    {
      return enif_make_badarg(env);
    }

  ttyfd = open(ttyname, O_RDWR|O_NOCTTY|O_NONBLOCK);

  if (ttyfd < 0)
    {
      return mk_errno(env, errno);
    }

  return enif_make_tuple2(env, atom_ok,
    enif_make_int(env, ttyfd));
}

/**********************************************************************
 * Name: close_tty
 *
 * Desc: close the tty, return ok or errno
 */
static ERL_NIF_TERM
close_tty(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
  int fd = -1;

  if (enif_get_int(env, argv[0], &fd) < 1)
    {
      return enif_make_badarg(env);
    }

  if (close(fd) < 0)
    {
      return mk_errno(env, errno);
    }

  return atom_ok;
}

static ErlNifFunc nif_funcs[] = {
  {"open-tty", 1, open_tty},
  {"close-tty", 1, close_tty},
  {"set-raw-tty-mode", 1, set_raw_tty_mode},
  {"set-tty-speed", 3, set_tty_speed},
  {"set-tty-flow", 2, set_tty_flow}
};

ERL_NIF_INIT(cereal, nif_funcs, &load, &reload, &upgrade, &unload);

