/*
 * Copyright (c) 2008 Stephen Williams (steve@icarus.com)
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

#define __STDC_FORMAT_MACROS
# include  "protocol.h"
# include  "client.h"
# include  "priv.h"
# include  "lxt2_write.h"
# include  <inttypes.h>
# include  <iostream>
# include  <assert.h>

using namespace std;

protocol_t::protocol_t(struct bus_state*b)
: bus_(b)
{
      sgenrand(&rand_state_, 1);
}

protocol_t::~protocol_t()
{
}

bus_device_map_t& protocol_t::device_map()
{
      return bus_->device_map;
}

void protocol_t::make_trace_(const char*lab, trace_type_t lt_type, int wid)
{
      int use_lt_type = 0;
      switch (lt_type) {
	  case PT_BITS:
	    use_lt_type = LXT2_WR_SYM_F_BITS;
	    break;
	  case PT_STRING:
	    use_lt_type = LXT2_WR_SYM_F_STRING;
	    break;
      }
      string tmp_name = bus_->name + "." + lab;
      struct lxt2_wr_symbol*sym = lxt2_wr_symbol_add(service_lxt,
						     tmp_name.c_str(),
						     0, wid-1, 0, use_lt_type);
      signal_trace_map[lab] = sym;
}

void protocol_t::set_trace_(const char*lab, bit_state_t bit)
{
      struct lxt2_wr_symbol*sym = signal_trace_map[lab];
      char buf[2];
      assert(bit < 4);
      buf[0] = "01zx"[bit];
      buf[1] = 0;
      lxt2_wr_emit_value_bit_string(service_lxt, sym, 0, buf);
}

void protocol_t::set_trace_(const char*lab, const valarray<bit_state_t>&bit)
{
      struct lxt2_wr_symbol*sym = signal_trace_map[lab];
      char buf[1025];
      assert(bit.size() < sizeof buf);
      for (int idx = 0 ; idx < bit.size() ; idx += 1)
	    buf[idx] = "01zx"[bit[bit.size()-1-idx]];

      buf[bit.size()] = 0;
      lxt2_wr_emit_value_bit_string(service_lxt, sym, 0, buf);
}

void protocol_t::set_trace_(const char*lab, const string&bit)
{
      struct lxt2_wr_symbol*sym = signal_trace_map[lab];
      lxt2_wr_emit_value_string(service_lxt, sym, 0,
				const_cast<char*>(bit.c_str()));
}

void protocol_t::advance_time_(uint64_t use_mant, int use_exp)
{
      time_ += simtime_t(use_mant, use_exp);
}

void protocol_t::bus_ready()
{
	// First, clear the ready flags for all the devices. This will
	// force the expectation of a new READY message from all the
	// devices.
      for (bus_device_map_t::iterator dev = bus_->device_map.begin()
		 ; dev != bus_->device_map.end() ;  dev ++) {
	    dev->second->ready_flag = false;
      }

	// Call the protocol engine.
      run_run();

	// If the bus is finished, then just send FINISH commands to
	// all the clients and close their ports.
      if (bus_->finished) {
	    for (bus_device_map_t::iterator dev = bus_->device_map.begin()
		       ; dev != bus_->device_map.end() ;  dev ++) {

		  int fd = dev->second->fd;
		  int rc = write(fd, "FINISH\n", 7);
		  close(fd);
		  dev->second->exited_flag = true;

		  protocol_log << client_state_t::client_map[fd].dev_name()
			       << ":SEND:FINISH" << endl;
	    }

	      // Close the bus.
	    close(bus_->fd);
	    bus_->fd = -1;

	    return;
      }

	// Send the new signal state to the client. This scans through
	// the signal map for each device, and sends the state for all
	// the signals to the client through the UNTIL message.
      for (bus_device_map_t::iterator dev = bus_->device_map.begin()
		 ; dev != bus_->device_map.end() ;  dev ++) {

	    int fd = dev->second->fd;
	    signal_state_map_t&sigs = dev->second->send_signals;

	    char buf[4097];
	    snprintf(buf, sizeof buf, "UNTIL %" PRIu64 "e%d",
		     time_.peek_mant(), time_.peek_exp());

	    char*cp = buf + strlen(buf);
	    for (signal_state_map_t::iterator cur_sig = sigs.begin()
		       ; cur_sig != sigs.end() ; cur_sig ++) {

		  int width = cur_sig->second.size();

		  *cp++ = ' ';
		  strcpy(cp, cur_sig->first.c_str());
		  cp += strlen(cp);
		  *cp++ = '=';
		  for (int idx = 0 ; idx < width ; idx+=1) {
			switch (cur_sig->second[width-idx-1]) {
			    case BIT_0:
			      *cp++ = '0';
			      break;
			    case BIT_1:
			      *cp++ = '1';
			      break;
			    case BIT_Z:
			      *cp++ = 'z';
			      break;
			    case BIT_X:
			      *cp++ = 'x';
			      break;
			    default:
			      cerr << "XXXX " << dev->first
				   << ":" << cur_sig->first
				   << ": bit[" << idx << "] "
				   << " is " << cur_sig->second[width-idx-1]
				   << endl;
			      assert(0);
			}
		  }

	    }

	    *cp = 0;
	    protocol_log << client_state_t::client_map[fd].dev_name()
			 << ":SEND:" << buf << endl;

	    *cp++ = '\n';
	    int rc = write(fd, buf, cp-buf);
	    assert(rc == (cp-buf));
      }
}

bool protocol_t::wrap_up_configuration()
{
      return true;
}

void protocol_t::trace_init()
{
}

void protocol_t::run_init()
{
}

void protocol_t::run_run()
{
      assert(0);
}
