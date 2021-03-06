/*
 * Copyright (c) 2010 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

# include  <vpi_user.h>
# include  <sys/types.h>
# include  <sys/select.h>
# include  <sys/socket.h>
# include  <sys/un.h>
# include  <ctype.h>
# include  <errno.h>
# include  <netdb.h>
# include  <unistd.h>
# include  <stdlib.h>
# include  <string.h>
# include  "priv.h"
# include  <assert.h>

/*
 * Implement these system tasks/functions:
 *
 * $simbus_connect
 *
 * $simbus_ready
 *
 * $simbus_poll(<bus>, <reg>)
 *
 * $simbus_until
 */

# define MAX_INSTANCES 32
/* Largest message length, including the newline. */
# define MAX_MESSAGE 4096

/*
 * Debug capabilities can be turned on by setting bits in this
 * bitmask. The -simbus-debug-mask=<N> command line will set the debug mask.
 */
static unsigned simbus_debug_mask = 0;
# define SIMBUS_DEBUG_PROTOCOL 0x0001
# define SIMBUS_DEBUG_CALLS    0x0002

# define DEBUG(mask, msg...) do { if ((mask)&simbus_debug_mask) vpi_printf("SIMBUS: " msg); } while(0)

struct port_instance {
	/* This is the name that I want to be. Use it for
	   human-readable messages, and also as a key when connecting
	   to the server. */
      char*name;

	/* This fd is the socket that is connected to the bus server. */
      int fd;

	/* this is the identifier that I get back from the bus when I
	   connect. This is used to select the correct instance of
	   non-shared bus signals. */
      unsigned ident;

	/* Use these buffers to manage data received from the server. */
      char   read_buf[MAX_MESSAGE+1];
      size_t read_fil;

	/* When poll-waiting for a message from the server, this
	   member is set to the vpiHandle of the trigger register that
	   is to receive a prod when data is ready. */
      vpiHandle trig;

} instance_table[MAX_INSTANCES];

/*
 * This function tests if the next message for the bus can be read
 * without blocking. If the message is complete in the buffer, return
 * true. Otherwise, return false.
 */
static int check_readable(int idx)
{
      assert(idx < MAX_INSTANCES);
      struct port_instance*inst = instance_table + idx;
      assert(inst->name != 0);

      return (strchr(inst->read_buf, '\n') != 0);
}

static void consume_readable_data(int idx)
{
      assert(idx < MAX_INSTANCES);
      struct port_instance*inst = instance_table + idx;
      assert(inst->name != 0);

      size_t trans = sizeof inst->read_buf - inst->read_fil - 1;
      int rc = read(inst->fd, inst->read_buf+inst->read_fil, trans);
      if (rc <= 0) return;

      assert(rc > 0);
      inst->read_fil += rc;
      inst->read_buf[inst->read_fil] = 0;
}

static PLI_INT32 poll_for_simbus_bus(struct t_cb_data*cb)
{
      int nfds = 0;
      int idx;
      int rc;
      fd_set read_set;
      FD_ZERO(&read_set);

      for (idx = 0 ; idx < MAX_INSTANCES ; idx += 1) {
	    if (instance_table[idx].trig == 0)
		  continue;

	    if (instance_table[idx].fd > nfds)
		  nfds = instance_table[idx].fd;

	    FD_SET(instance_table[idx].fd, &read_set);
      }

      if (nfds == 0)
	    return 0;

      rc = select(nfds+1, &read_set, 0, 0, 0);
      assert(rc != 0);
      if (rc < 0) {
	    vpi_printf("ERROR:poll_for_simbus_bus:%s\n", sys_errlist[errno]);
	    vpi_control(vpiFinish, 1);
	    return 0;
      }
      assert(rc > 0);

      for (idx = 0 ; idx < MAX_INSTANCES ; idx += 1) {
	    s_vpi_value value;

	    if (instance_table[idx].trig == 0)
		  continue;

	    if (! FD_ISSET(instance_table[idx].fd, &read_set))
		  continue;

	      /* This fd is readable, so try to read some data, and
		 check if a command is complete. If not, then continue
		 waiting. Otherwise, let this one be completed. */
	    consume_readable_data(idx);
	    if (!check_readable(idx))
		  continue;

	    value.format = vpiScalarVal;
	    value.value.scalar = vpi1;
	    vpi_put_value(instance_table[idx].trig, &value, 0, vpiNoDelay);
	    instance_table[idx].trig = 0;
      }

	/* Check and see if there are still triggers waiting. If so
	   then reschedule this callback. */
      nfds += 1;
      for (idx = 0 ; idx < MAX_INSTANCES ; idx += 1) {
	    if (instance_table[idx].trig == 0)
		  continue;

	    nfds += 1;
      }

      if (nfds > 0) {
	    struct t_cb_data cb_data;
	    struct t_vpi_time cb_time;
	    cb_time.type = vpiSuppressTime;
	    cb_data.reason = cbReadWriteSynch;
	    cb_data.cb_rtn = poll_for_simbus_bus;
	    cb_data.obj = 0;
	    cb_data.time   = &cb_time;
	    cb_data.value = 0;
	    cb_data.index = 0;
	    cb_data.user_data = 0;
	    vpi_register_cb(&cb_data);
      }

      return 0;
}

/*
 * Read the next network message from the specified server
 * connection. This function will manage the read buffer to get text
 * until the message is complete.
 */
static int read_message(int idx, char*buf, size_t nbuf)
{
      assert(idx < MAX_INSTANCES);
      struct port_instance*inst = instance_table + idx;
      assert(inst->name != 0);

	/* This function is certain to read data, so make sure the
	   trig is cleared. */
      inst->trig = 0;

      for (;;) {
	    char*cp;
	      /* If there is a line in the buffer now, then pull that
		 line out of the read buffer and give it to the
		 caller. */
	    if ( (cp = strchr(inst->read_buf, '\n')) != 0 ) {
		  size_t len = cp - inst->read_buf;
		  assert(len < nbuf);

		  *cp++ = 0;
		  strcpy(buf, inst->read_buf);
		  assert(len < inst->read_fil);
		  inst->read_fil -= len+1;
		  if (inst->read_fil > 0)
			memmove(inst->read_buf, cp, inst->read_fil);

		  inst->read_buf[inst->read_fil] = 0;
		  return len;
	    }

	    consume_readable_data(idx);
      }
}


/*
 * This routine returns 1 if the argument supports a valid string value,
 * otherwise it returns 0.
 */
static int is_string_obj(vpiHandle obj)
{
    int rtn = 0;

    assert(obj);

    switch(vpi_get(vpiType, obj)) {
      case vpiConstant:
      case vpiParameter: {
	  /* These must be a string or binary constant. */
	PLI_INT32 ctype = vpi_get(vpiConstType, obj);
	if (ctype == vpiStringConst || ctype == vpiBinaryConst) rtn = 1;
	break;
      }
    }

    return rtn;
}

static PLI_INT32 simbus_connect_compiletf(char*my_name)
{
      vpiHandle callh = vpi_handle(vpiSysTfCall, 0);
      vpiHandle argv = vpi_iterate(vpiArgument, callh);

      /* Check that there is an argument and that it is a string. */
      if (argv == 0) {
            vpi_printf("ERROR: %s line %d: ", vpi_get_str(vpiFile, callh),
                       (int)vpi_get(vpiLineNo, callh));
            vpi_printf("%s requires a single string argument.\n", my_name);
            vpi_control(vpiFinish, 1);
            return 0;
      }

      if (! is_string_obj(vpi_scan(argv))) {
            vpi_printf("ERROR: %s line %d: ", vpi_get_str(vpiFile, callh),
                       (int)vpi_get(vpiLineNo, callh));
            vpi_printf("%s's argument must be a constant string.\n", my_name);
            vpi_control(vpiFinish, 1);
      }

      /* Make sure there are no extra arguments. */
      while (vpi_scan(argv)) { ; }

      return 0;
}

static int tcp_server(vpiHandle sys, const char*my_name, const char*dev_name,
		      char*addr)
{

	/* Split the string into a host name and a port number. If
	   there is no host name, then use "localhost". */
      char*host_name = 0;
      char*host_port = 0;
      char*cp = strchr(addr, ':');
      if (cp != 0) {
	    *cp++ = 0;
	    host_name = addr;
	    host_port = cp;
      } else {
	    host_name = "localhost";
	    host_port = addr;
      }

	/* Given the host string from the command line, look it up and
	   get the address and port numbers. */
      struct addrinfo hints, *res;
      memset(&hints, 0, sizeof(struct addrinfo));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      int rc = getaddrinfo(host_name, host_port, &hints, &res);
      if (rc != 0) {
	    vpi_printf("%s:%d: %s(%s) cannot find host %s:%s\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		       my_name, dev_name, host_name, host_port);
	    return -1;
      }

	/* Connect to the server. */
      int server_fd = -1;
      struct addrinfo *rp;
      for (rp = res ; rp != 0 ; rp = rp->ai_next) {
	    server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	    if (server_fd < 0)
		  continue;

	    if (connect(server_fd, rp->ai_addr, rp->ai_addrlen) < 0) {
		  close(server_fd);
		  server_fd = -1;
		  continue;
	    }

	    break;
      }

      freeaddrinfo(res);

      if (server_fd == -1) {
	    vpi_printf("%s:%d: %s(%s) cannot connect to server %s:%s\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		       my_name, dev_name, host_name, host_port);

	    return -1;
      }

      return server_fd;
}

static int pipe_server(vpiHandle sys, const char*my_name, const char*dev_name,
		       const char*path)
{
      int fd = socket(PF_UNIX, SOCK_STREAM, 0);
      assert(fd >= 0);

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof addr);

      assert(strlen(path) < sizeof addr.sun_path);
      addr.sun_family = AF_UNIX;
      strcpy(addr.sun_path, path);

      int rc = connect(fd, (const struct sockaddr*)&addr, sizeof addr);
      assert(rc >= 0);

      return fd;
}

static PLI_INT32 simbus_connect_calltf(char*my_name)
{
      int idx;
      char buf[4096];
      s_vpi_value value;
      s_vpi_vlog_info vlog;

      vpiHandle sys = vpi_handle(vpiSysTfCall, 0);
      vpiHandle argv = vpi_iterate(vpiArgument, sys);
      vpiHandle arg;
      assert(argv);

	/* Get the one and only argument, and get its string value. */
      arg = vpi_scan(argv);
      assert(arg);

      value.format = vpiStringVal;
      vpi_get_value(arg, &value);
      char*dev_name = strdup(value.value.str);

      DEBUG(SIMBUS_DEBUG_CALLS, "Call $connect(%s)\n", dev_name);

	/* Synthesize a bus server argument string and search for it
	   on the command line. That string will have the host and
	   port number (or just port number) for the bus that we are
	   supposed to connect to. */
      snprintf(buf, sizeof buf,
	       "+simbus-%s-bus=", dev_name);

      vpi_get_vlog_info(&vlog);
      char*host_string = 0;
      for (idx = 0 ; idx < vlog.argc ; idx += 1) {
	    if (strncmp(vlog.argv[idx], buf, strlen(buf)) == 0) {
		  host_string = strdup(vlog.argv[idx]+strlen(buf));
		  break;
	    }
      }

      if (host_string == 0) {
	    vpi_printf("%s:%d: %s(%s) cannot find %s<server> on command line\n",
		       vpi_get_str(vpiFile, sys),
		       (int)vpi_get(vpiLineNo, sys),
		       my_name, dev_name, buf);

	    free(dev_name);
	    value.format = vpiIntVal;
	    value.value.integer = -1;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    return 0;
      }

      DEBUG(SIMBUS_DEBUG_CALLS, "$connect(%s): host string=%s\n", dev_name, host_string);

      int server_fd = -1;
      if (strncmp(host_string, "tcp:", 4) == 0) {
	    server_fd = tcp_server(sys, my_name, dev_name, host_string+4);

      } else if (strncmp(host_string, "pipe:", 5) == 0) {
	    server_fd = pipe_server(sys, my_name, dev_name, host_string+5);

      } else {
	    server_fd = tcp_server(sys, my_name, dev_name, host_string);
      }

      if (server_fd == -1) {
	    DEBUG(SIMBUS_DEBUG_CALLS, "$connect(%s): Failed connect to server.\n", dev_name);
	    free(dev_name);
	    free(host_string);
	    value.format = vpiIntVal;
	    value.value.integer = -1;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    return 0;
      }

	/* Send HELLO message to the server. */

      snprintf(buf, sizeof buf, "HELLO %s", dev_name);

	/* Add to the HELLO string a string of setting tokens for all
	   the remaining arguments of the $simbus_connect statement. */
      char *bp = buf + strlen(buf);
      size_t bp_len = sizeof buf - (bp-buf);

      assert(argv);
      for (arg = vpi_scan(argv) ; arg ; arg = vpi_scan(argv)) {
	    value.format = vpiStringVal;
	    vpi_get_value(arg, &value);
	    if (value.format != vpiStringVal) {
		  vpi_printf("%s:%d: $connect(%s) Can't get string for argument.\n",
			     vpi_get_str(vpiFile, sys),
			     vpi_get(vpiLineNo,sys),
			     dev_name);
		  continue;
	    }

	    DEBUG(SIMBUS_DEBUG_CALLS, "$connect(%s): HELLO key: %s\n", dev_name, value.value.str);

	      /* Remove leading and trailing white space. */
	    char*sp = value.value.str;
	    while (*sp && isspace(*sp))
		  sp += 1;

	    char*ep = sp+strlen(sp);
	    while (ep > sp && isspace(ep[-1])) {
		  ep -= 1;
		  ep[0] = 0;
	    }

	    if (ep==sp) {
		  continue;
	    }

	    if (strchr(sp, ' ') || ! strchr(sp, '=')) {
		  vpi_printf("%s:%d: %s(%s): Malformed string argument: %s\n",
			     vpi_get_str(vpiFile, sys),
			     vpi_get(vpiLineNo,sys),
			     my_name, host_string, sp);
		  continue;
	    }

	    if ((ep-sp)+1 >= bp_len) {
		  vpi_printf("%s:%d: %s(%s): Internal error: Ran out of string space.\n",
			     vpi_get_str(vpiFile, sys),
			     vpi_get(vpiLineNo,sys),
			     my_name, host_string);
		  continue;
	    }

	    *bp++ = ' ';
	    bp_len -= 1;

	    strcpy(bp, sp);
	    bp += (ep-sp);
	    bp_len -= (ep-sp);
      }

      *bp++ = '\n';
      *bp = 0;

      DEBUG(SIMBUS_DEBUG_PROTOCOL, "Send %s", buf);
      int rc = write(server_fd, buf, strlen(buf));
      assert(rc == strlen(buf));

	/* Read response from server. */
      rc = read(server_fd, buf, sizeof buf);
      assert(rc > 0);
      assert(strchr(buf, '\n'));
      DEBUG(SIMBUS_DEBUG_PROTOCOL, "Recv %s", buf);

      if (strcmp(buf, "NAK\n") == 0) {
	    vpi_printf("%s:%d: %s(%s) Server %s doesn't want me.\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		       my_name, host_string, dev_name);

	    free(dev_name);
	    free(host_string);
	    close(server_fd);
	    value.format = vpiIntVal;
	    value.value.integer = -1;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    return 0;
      }

      unsigned ident = 0;
      if (strncmp(buf, "YOU-ARE ", 8) == 0) {
	    sscanf(buf, "YOU-ARE %u", &ident);
      } else {
	    vpi_printf("%s:%d: %s(%s) Server %s protocol error.\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		       my_name, dev_name, host_string);

	    free(dev_name);
	    free(host_string);
	    close(server_fd);
	    value.format = vpiIntVal;
	    value.value.integer = -1;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    return 0;
      }

	/* Create an instance for this connection. */
      idx = 0;
      while (instance_table[idx].name && idx < MAX_INSTANCES) {
	    idx += 1;
      }

      assert(idx < MAX_INSTANCES);
      instance_table[idx].name = dev_name;
      instance_table[idx].fd = server_fd;
	/* Empty the read buffer. */
      instance_table[idx].read_buf[0] = 0;
      instance_table[idx].read_fil = 0;

      vpi_printf("%s:%d: %s(%s) Bus server %s ready.\n",
		 vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		 my_name, dev_name, host_string);

      DEBUG(SIMBUS_DEBUG_CALLS, "Return %d from $connect\n", idx);
      value.format = vpiIntVal;
      value.value.integer = idx;
      vpi_put_value(sys, &value, 0, vpiNoDelay);
      return 0;
}

static PLI_INT32 simbus_ready_compiletf(char*my_name)
{
      vpiHandle sys = vpi_handle(vpiSysTfCall, 0);

      vpi_printf("%s:%d: %s() STUB compiletf\n",
		 vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		 my_name);
      return 0;
}

static PLI_INT32 simbus_ready_calltf(char*my_name)
{
      s_vpi_value value;
      s_vpi_time now;

      vpiHandle sys = vpi_handle(vpiSysTfCall, 0);
	/* vpiHandle scope = vpi_handle(vpiScope, sys); */
      vpiHandle argv = vpi_iterate(vpiArgument, sys);
      vpiHandle arg;
      assert(argv);

      arg = vpi_scan(argv);
      assert(arg);

	/* Get the BUS identifier to use. */
      value.format = vpiIntVal;
      vpi_get_value(arg, &value);
      int bus_id = value.value.integer;
      assert(bus_id < MAX_INSTANCES);
      assert(instance_table[bus_id].fd >= 0);

      DEBUG(SIMBUS_DEBUG_CALLS, "Call $ready(%d...)\n", bus_id);

	/* Get the simulation time. */
      now.type = vpiSimTime;
      vpi_get_time(0, &now);
      uint64_t now_int = ((uint64_t)now.high) << 32;
      now_int += (uint64_t) now.low;

	/* Get the units for the simulation. */
      int scale  = vpi_get(vpiTimePrecision, 0);

	/* Minimize the mantissa by increasing the scale so long as
	   the mantissa is a multiple of 10. This is not a functional
	   requirement, but it does help prevent a runaway precision
	   expansion. */
      while (now_int >= 10 && (now_int%10 == 0)) {
	    now_int /= 10;
	    scale += 1;
      }

      char message[MAX_MESSAGE+1];
      snprintf(message, sizeof message, "READY %" PRIu64 "e%d", now_int, scale);

      char*cp = message + strlen(message);

	/* Send the current state of all the named signals. The format
	   passed in to the argument list is "name", value. Write
	   these values in the proper message format. */
      for (arg = vpi_scan(argv) ; arg ; arg = vpi_scan(argv)) {
	    *cp++ = ' ';

	    value.format = vpiStringVal;
	    vpi_get_value(arg, &value);
	    strcpy(cp, value.value.str);
	    cp += strlen(cp);

	    *cp++ = '=';

	    vpiHandle sig = vpi_scan(argv);
	    assert(sig);

	    value.format = vpiVectorVal;
	    vpi_get_value(sig, &value);
	    int bit;
	    char*sig_string = cp;
	    for (bit = vpi_get(vpiSize, sig) ; bit > 0 ; bit -= 1) {
		  int word = (bit-1) / 32;
		  int mask = 1 << ((bit-1) % 32);
		  if (value.value.vector[word].aval & mask)
			if (value.value.vector[word].bval & mask)
			      *cp++ = 'x';
			else
			      *cp++ = '1';
		  else
			if (value.value.vector[word].bval & mask)
			      *cp++ = 'z';
			else
			      *cp++ = '0';
	    }

	      /* The second value after the signal name is the drive
		 reference. It is the value that the server is driving
		 (or 'bz if this is output-only). Look at the driver
		 value, and if it is non-z and equal to the value that
		 I see in the verilog, then assume that this is the
		 driver driving the value and subtract it. */
	    vpiHandle drv = vpi_scan(argv);
	    assert(drv);
	    assert(vpi_get(vpiSize,drv) == vpi_get(vpiSize,sig));

	    value.format = vpiVectorVal;
	    vpi_get_value(drv, &value);

	    char*drv_reference = malloc(vpi_get(vpiSize,drv));
	    for (bit = vpi_get(vpiSize,drv) ; bit > 0 ; bit -= 1) {
		  int word = (bit-1) / 32;
		  int mask = 1 << ((bit-1) % 32);
		  if (value.value.vector[word].aval & mask)
			if (value.value.vector[word].bval & mask)
			      drv_reference[bit-1] = 'x';
			else
			      drv_reference[bit-1] = '1';
		  else
			if (value.value.vector[word].bval & mask)
			      drv_reference[bit-1] = 'z';
			else
			      drv_reference[bit-1] = '0';

	    }

	      /* Get the strength values from the signal. */
	    value.format = vpiStrengthVal;
	    vpi_get_value(sig, &value);

	      /* Now given the sig_string that is the current result,
		 and the drive reference that is the value that is
		 being driven by the server, subtract out from the
		 sig_string the server driver, and any pullups from
		 the port. */
	    for (bit = vpi_get(vpiSize,drv); bit > 0; bit -= 1, sig_string+=1) {

		  if (drv_reference[bit-1] == *sig_string) {
			*sig_string = 'z';
			continue;
		  }

		  if (*sig_string == 'z')
			continue;
		  if (*sig_string == 'x')
			continue;

		    /* Do not pass pullup/pulldown values to the
		       server. If the strength of the net is less then
		       a strong drive, then clear it to z. */
		  struct t_vpi_strengthval*str = value.value.strength + bit - 1;
		  if (str->s0 < vpiStrongDrive && str->s1 < vpiStrongDrive)
			*sig_string = 'z';
	    }

	    free(drv_reference);
	    assert(sig_string == cp);
      }

      *cp++ = '\n';
      *cp = 0;

      DEBUG(SIMBUS_DEBUG_PROTOCOL, "Send %s", message);
      int rc = write(instance_table[bus_id].fd, message, strlen(message));
      assert(rc == strlen(message));

      DEBUG(SIMBUS_DEBUG_CALLS, "Return from $ready(%d...)\n", bus_id);

      return 0;
}

static PLI_INT32 simbus_poll_compiletf(char*my_name)
{
      vpiHandle sys  = vpi_handle(vpiSysTfCall, 0);
      vpiHandle argv = vpi_iterate(vpiArgument, sys);

      vpiHandle bus_h = vpi_scan(argv);
      if (bus_h == 0) {
	    vpi_printf("%s:%d: Missing argument to $simbus_poll function\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys));
	    vpi_control(vpiFinish, 1);
	    return 0;
      }

      vpiHandle trig = vpi_scan(argv);
      if (trig == 0) {
	    vpi_printf("%s:%d: Missing argument to $simbus_poll function\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys));
	    vpi_control(vpiFinish, 1);
	    return 0;
      }

      if (vpi_get(vpiType, trig) != vpiReg || vpi_get(vpiSize, trig) != 1) {
	    vpi_printf("%s:%d: Trigger argument to $simbus_poll must be a single-bit reg.\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys));
	    vpi_control(vpiFinish, 1);
	    return 0;
      }

      vpi_free_object(argv);

      return 0;
}

static PLI_INT32 simbus_poll_calltf(char*my_name)
{
      int poll_state;
      s_vpi_value value;

      vpiHandle sys = vpi_handle(vpiSysTfCall, 0);
      vpiHandle argv = vpi_iterate(vpiArgument, sys);

      vpiHandle bus_h = vpi_scan(argv);
      assert(bus_h);

      value.format = vpiIntVal;
      vpi_get_value(bus_h, &value);

      int bus = value.value.integer;
      assert(bus >= 0 && bus < MAX_INSTANCES);

      vpiHandle trig = vpi_scan(argv);
      assert(trig);

      vpi_free_object(argv);

      DEBUG(SIMBUS_DEBUG_CALLS, "Call $poll(%d...)\n", bus);

      poll_state = check_readable(bus);

      value.format = vpiScalarVal;
      value.value.scalar = poll_state? vpi1 : vpi0;
      vpi_put_value(trig, &value, 0, vpiNoDelay);

      if (poll_state == 0) {
	    struct t_cb_data cb_data;
	    struct t_vpi_time cb_time;
	    cb_time.type = vpiSuppressTime;
	    cb_data.reason = cbReadWriteSynch;
	    cb_data.cb_rtn = poll_for_simbus_bus;
	    cb_data.obj = 0;
	    cb_data.time   = &cb_time;
	    cb_data.value = 0;
	    cb_data.index = 0;
	    cb_data.user_data = 0;
	    vpi_register_cb(&cb_data);

	    instance_table[bus].trig = trig;

      } else {
	    instance_table[bus].trig = 0;
      }

      DEBUG(SIMBUS_DEBUG_CALLS, "return $poll(%d...)\n", bus);
      return 0;
}

static PLI_INT32 simbus_until_compiletf(char*my_name)
{
      vpiHandle sys  = vpi_handle(vpiSysTfCall, 0);

      vpi_printf("%s:%d: %s() STUB compiletf\n",
		 vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		 my_name);
      return 0;
}

struct signal_list_cell {
      struct signal_list_cell*next;
      char*key;
      vpiHandle sig;
};

static struct signal_list_cell* find_key_in_list(struct signal_list_cell*ll, const char*key)
{
      while (ll && strcmp(ll->key,key)!=0)
	    ll = ll->next;

      return ll;
}

static void free_signal_list(struct signal_list_cell*ll)
{
      while (ll) {
	    struct signal_list_cell*tmp = ll->next;
	    free(ll->key);
	    free(ll);
	    ll = tmp;
      }
}

static void set_handle_to_value(vpiHandle sig, const char*val)
{
      size_t width = strlen(val);
      size_t vv_count = (width+31)/32;

      s_vpi_value value;

      value.value.vector = calloc(vv_count, sizeof(s_vpi_vecval));
      int idx;
      for (idx = 0 ; idx < width ; idx += 1) {
	    int word = idx / 32;
	    int bit = idx % 32;
	    char src = val[width-idx-1];
	    PLI_INT32 amask = 0;
	    PLI_INT32 bmask = 0;
	    switch (src) {
		case '0':
		  continue;
		case '1':
		  amask = 1;
		  bmask = 0;
		  break;
		case 'x':
		  amask = 1;
		  bmask = 1;
		  break;
		case 'z':
		  amask = 0;
		  bmask = 1;
		  break;
	    }

	    s_vpi_vecval*vp = value.value.vector+word;

	    vp->aval |= amask << bit;
	    vp->bval |= bmask << bit;
      }

      if (vpi_get(vpiSize, sig) != width) {
	    vpi_printf("ERROR: %s is %d bits, got %zu from server\n",
		       vpi_get_str(vpiName, sig), vpi_get(vpiSize, sig), width);
	    vpi_flush();
      }

      assert(vpi_get(vpiSize, sig) == width);
      assert(vpi_get(vpiType, sig) == vpiReg);

      value.format = vpiVectorVal;
      vpi_put_value(sig, &value, 0, vpiNoDelay);

      free(value.value.vector);
}

static PLI_INT32 simbus_until_calltf(char*my_name)
{
      s_vpi_time now;
      s_vpi_value value;

      vpiHandle sys = vpi_handle(vpiSysTfCall, 0);
      vpiHandle scope = vpi_handle(vpiScope, sys);
      vpiHandle argv = vpi_iterate(vpiArgument, sys);

      vpiHandle bus_h = vpi_scan(argv);
      assert(bus_h);

      value.format = vpiIntVal;
      vpi_get_value(bus_h, &value);

      int bus = value.value.integer;
      assert(bus >= 0 && bus < MAX_INSTANCES);

      DEBUG(SIMBUS_DEBUG_CALLS, "Call $until(%d...)\n", bus);

	/* Get a list of the signals and their mapping to a handle. We
	   will use list list to map names from the UNTIL command back
	   to the handle. */
      struct signal_list_cell*signal_list = 0;
      vpiHandle key, sig;
      for (key = vpi_scan(argv) ; key ; key = vpi_scan(argv)) {
	    sig = vpi_scan(argv);
	    assert(sig);

	    struct signal_list_cell*tmp = calloc(1, sizeof(struct signal_list_cell));
	    value.format = vpiStringVal;
	    vpi_get_value(key, &value);
	    assert(value.format == vpiStringVal);
	    assert(value.value.str);
	    tmp->key = strdup(value.value.str);
	    tmp->sig = sig;
	    tmp->next = signal_list;
	    signal_list = tmp;
      }

	/* Now read the command from the server. This will block until
	   the server data actually arrives. */
      char buf[MAX_MESSAGE+1];
      int rc = read_message(bus, buf, sizeof buf);

      if (rc <= 0) {
	    vpi_printf("%s:%d: %s() read from server failed\n",
		       vpi_get_str(vpiFile, sys), (int)vpi_get(vpiLineNo, sys),
		       my_name);

	    vpi_control(vpiStop);

	    free_signal_list(signal_list);

	      /* Set the return value and return. */
	    value.format = vpiIntVal;
	    value.value.integer = 0;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    DEBUG(SIMBUS_DEBUG_CALLS, "Return 0 from $until(%d...)\n", bus);
	    return 0;
      }

      DEBUG(SIMBUS_DEBUG_PROTOCOL, "Recv %s\n", buf);

	/* Chop the message into tokens. */
      int   msg_argc = 0;
      char* msg_argv[MAX_MESSAGE/2];

      char*cp = buf;
      while (*cp != 0) {
	    msg_argv[msg_argc++] = cp;
	    cp += strcspn(cp, " ");
	    if (*cp) {
		  *cp++ = 0;
		  cp += strspn(cp, " ");
	    }
      }
      msg_argv[msg_argc] = 0;

	/* If we get a FINISH command from the server, then $finish
	   the local simulation. */
      if (strcmp(msg_argv[0],"FINISH") == 0) {
	    vpi_printf("Server disconnected with FINISH command\n");
	    vpi_control(vpiFinish);
	    free_signal_list(signal_list);

	      /* Set the return value and return. */
	    value.format = vpiIntVal;
	    value.value.integer = 0;
	    vpi_put_value(sys, &value, 0, vpiNoDelay);
	    DEBUG(SIMBUS_DEBUG_CALLS, "Return 0 from $until(%d...)\n", bus);
	    return 0;
      }

      assert(strcmp(msg_argv[0],"UNTIL") == 0);

      assert(msg_argc >= 2);

      uint64_t until_mant = strtoull(msg_argv[1],&cp,10);
      assert(cp && *cp=='e');
      cp += 1;
      int until_exp = strtol(cp,0,0);

	/* Get the units for the scope */
      int units = vpi_get(vpiTimeUnit, scope);

	/* Put the until time into units of the scope. */
      while (units < until_exp) {
	    until_mant *= 10;
	    until_exp -= 1;
      }
      while (units > until_exp) {
	    until_mant = (until_mant + 5)/10;
	    until_exp += 1;
      }

      	/* Get the simulation time and put it into scope units. */
      now.type = vpiSimTime;
      vpi_get_time(0, &now);
      uint64_t deltatime = ((uint64_t)now.high) << 32;
      deltatime += (uint64_t) now.low;

      int prec  = vpi_get(vpiTimePrecision, 0);
      while (prec < units) {
	    prec += 1;
	    deltatime = (until_mant + 5)/10;
      }

	/* Now we can calculate the delta time. */
      if (deltatime > until_mant)
	    deltatime = 0;
      else
	    deltatime = until_mant - deltatime;

	/* Set the return value and return it. */
      value.format = vpiIntVal;
      value.value.integer = deltatime;
      vpi_put_value(sys, &value, 0, vpiNoDelay);

	/* Process the signal values. */
      int idx;
      for (idx = 2 ; idx < msg_argc ; idx += 1) {

	    char*mkey = msg_argv[idx];
	    char*val = strchr(mkey, '=');
	    assert(val && *val=='=');
	    *val++ = 0;

	    struct signal_list_cell*cur = find_key_in_list(signal_list, mkey);
	    if (cur == 0) {
		  vpi_printf("%s:%d: %s() Unexpected signal %s from bus.\n",
			     vpi_get_str(vpiFile, sys),
			     (int)vpi_get(vpiLineNo, sys),
			     my_name, mkey);
		  continue;
	    }

	    set_handle_to_value(cur->sig, val);
      }

      free_signal_list(signal_list);

      DEBUG(SIMBUS_DEBUG_CALLS, "Return %" PRIu64 " from $until(%d...)\n", deltatime, bus);
      return 0;
}


static struct t_vpi_systf_data simbus_connect_tf = {
      vpiSysFunc,
      vpiSysFuncInt,
      "$simbus_connect",
      simbus_connect_calltf,
      simbus_connect_compiletf,
      0 /* sizetf */,
      "$simbus_connect"
};

static struct t_vpi_systf_data simbus_ready_tf = {
      vpiSysTask,
      0,
      "$simbus_ready",
      simbus_ready_calltf,
      simbus_ready_compiletf,
      0 /* sizetf */,
      "$simbus_ready"
};

static struct t_vpi_systf_data simbus_poll_tf = {
      vpiSysTask,
      0,
      "$simbus_poll",
      simbus_poll_calltf,
      simbus_poll_compiletf,
      0 /* sizetf */,
      "$simbus_poll"
};

static struct t_vpi_systf_data simbus_until_tf = {
      vpiSysFunc,
      vpiSysFuncSized,
      "$simbus_until",
      simbus_until_calltf,
      simbus_until_compiletf,
      0 /* sizetf */,
      "$simbus_until"
};

static void simbus_register(void)
{
      vpi_register_systf(&simbus_connect_tf);
      vpi_register_systf(&simbus_ready_tf);
      vpi_register_systf(&simbus_poll_tf);
      vpi_register_systf(&simbus_until_tf);
}

static void simbus_setup(void)
{
      struct t_vpi_vlog_info vlog_info;
      int idx;
      int version_flag = 1;
      vpi_get_vlog_info(&vlog_info);

      for (idx = 0 ; idx < vlog_info.argc ; idx += 1) {

	    if (strncmp(vlog_info.argv[idx],"-simbus-debug-mask=",19) == 0) {
		  simbus_debug_mask = strtoul(vlog_info.argv[idx]+19,0,0);

	    } else if (strcmp(vlog_info.argv[idx],"-simbus-version") == 0) {
		  version_flag = 1;
	    } else if (strcmp(vlog_info.argv[idx],"-no-simbus-version") == 0) {
		  version_flag = 0;
	    }
      }

      if (version_flag)
	    vpi_printf("SIMBUS VPI version: %s\n", simbus_version);

      if (simbus_debug_mask == 0) {
	    const char*text = getenv("SIMBUS_DEBUG_MASK");
	    if (text)
		  simbus_debug_mask = strtoul(text, 0, 0);
      }

      if (simbus_debug_mask)
	    vpi_printf("Using -simbus-debug-mask=0x%04x\n", simbus_debug_mask);

      for (idx = 0 ; idx < MAX_INSTANCES ; idx += 1) {
	    instance_table[idx].name = 0;
	    instance_table[idx].fd = -1;
	    instance_table[idx].trig = 0;
      }
}

void (*vlog_startup_routines[])(void) = {
      simbus_setup,
      simbus_register,
      simbus_mem_register,
      0
};
