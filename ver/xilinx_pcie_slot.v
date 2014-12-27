/*
 * Copyright (c) 2014 Stephen Williams (steve@icarus.com)
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
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

`default_nettype none
`timescale 1ps/1ps

/*
 * NOTES:
 * The transmit channel is mux'ed by the tx_cfg_gnt signal. the internal
 * core sometimes wants to take over the transmit channel, which it does
 * by asserting the tx_cfg_req signal. The user hands the bus over by
 * asserting the tx_cfg_gnt. The core holds the _req as long as it runs
 * a transaction. It will release the _req signal when it does not need
 * the channel, and the user can withdraw the grant.
 */
module xilinx_pcie_slot
  #(// PCIe is point-to-point, and this slot only simulates an endpoint,
    // so a good default for the device name is "endpoint". If the device
    // wants multiple endpoints, this will need to be overridden.
    parameter  name = "endpoint",

    parameter integer LINK_CAP_MAX_LINK_WIDTH = 6'h8,

    // Identifiers
    parameter ven_id        = "FFFF",
    parameter dev_id        = "FFFF",
    parameter rev_id        = "00",
    parameter subsys_ven_id = "FFFF",
    parameter subsys_id     = "FFFF",
    // BARs
    parameter bar_0    = "FFFFFFF4",
    parameter bar_1    = "FFFFFFFF",
    parameter bar_2    = "00000000",
    parameter bar_3    = "00000000",
    parameter bar_4    = "00000000",
    parameter bar_5    = "00000000",
    parameter xrom_bar = "00000000"
    /* */)
   (// The Xilinx X7 core interface are all synchronous with this
    // clock, so for hte purposes of simbus simulation we'll use this
    // as the simbus clock. We'll also let the remote control the
    // reset signal.
    output reg 				       user_clk_out,
    output reg 				       user_reset_out,
    // Link status from the remote.
    output reg 				       user_lnk_up,
    output reg 				       user_app_rdy,
 
    // The module being emulated has the clock and reset as inputs,
    // so here are place-holders.
    input wire 				       sys_clk,
    input wire 				       sys_rst_n,

    // Assuming these are only used for routing purposes, they are
    // only here in this simulation module for port compatibility.
    // The inputs are ignored, and the outputs are driven constant
    // so that the simulator can optimize them away.
    output reg [(LINK_CAP_MAX_LINK_WIDTH-1):0] pci_exp_txn,
    output reg [(LINK_CAP_MAX_LINK_WIDTH-1):0] pci_exp_txp,

    input wire [(LINK_CAP_MAX_LINK_WIDTH-1):0] pci_exp_rxn,
    input wire [(LINK_CAP_MAX_LINK_WIDTH-1):0] pci_exp_rxp,

    // Interface to the clock manager. These are stubbed locally.
    input wire 				       pipe_mmcm_rst_n,
    input wire 				       pipe_pclk_in,
    input wire 				       pipe_rxusrclk_in,
    input wire 				       pipe_rxoutclk_in,
    input wire 				       pipe_usrclk1_in,
    input wire 				       pipe_usrclk2_in,
    input wire 				       pipe_mmcm_lock_in,
    input wire 				       pipe_dclk_in,
    input wire 				       pipe_oobclk_in,
    output reg 				       pipe_txoutclk_out,
    output reg [1:0] 			       pipe_rxoutclk_out,
    output reg [1:0] 			       pipe_pclk_sel_out,
    output reg 				       pipe_gen3_out,
    output reg [3:0] 			       pipe_cpll_lock,

    // Flow control status
    output reg [11:0] 			       fc_cpld,
    output reg [7:0] 			       fc_cplh,
    output reg [11:0] 			       fc_npd,
    output reg [7:0] 			       fc_nph,
    output reg [11:0] 			       fc_pd,
    output reg [7:0] 			       fc_ph,
    input wire [2:0] 			       fc_sel,

    // Transmit buffers available
    output reg [5:0] 			       tx_buf_av,
    // Transmit buffer was dropped due to error
    output reg 				       tx_err_drop,

    output wire 			       tx_cfg_req,
    input wire 				       tx_cfg_gnt,

    // Transmit channel AXI4 stream (slave side)
    input wire [63:0] 			       s_axis_tx_tdata,
    input wire [7:0] 			       s_axis_tx_tkeep,
    input wire 				       s_axis_tx_tlast,
    output reg 				       s_axis_tx_tready,
    input wire 				       s_axis_tx_tvalid,
    input wire [3:0] 			       s_axis_tx_tuser,

    input wire 				       rx_np_ok,
    input wire 				       rx_np_reg,

    // Receive channel AXI4 Stream (master side)
    output wire [63:0] 			       m_axis_rx_tdata,
    output wire [7:0] 			       m_axis_rx_tkeep,
    output wire 			       m_axis_rx_tlast,
    input wire 				       m_axis_rx_tready,
    output wire 			       m_axis_rx_tvalid,
    output wire [21:0] 			       m_axis_rx_tuser,

    // Configuration interface
    output reg [31:0] 			       cfg_mgmt_do,
    input wire [31:0] 			       cfg_mgmt_di,
    input wire [3:0] 			       cfg_mgmt_byte_en,
    input wire [9:0] 			       cfg_mgmt_dwaddr,
    input wire 				       cfg_mgmt_wr_en,
    input wire 				       cfg_mgmt_rd_en,
    input wire 				       cfg_mgmt_wr_readonly,
    output reg 				       cfg_mgmt_rd_wr_done,
    input wire 				       cfg_mgmt_wr_rw1c_as_rw,

    output reg [15:0] 			       cfg_status,
    output reg [15:0] 			       cfg_command,
    output reg [7:0] 			       cfg_bus_number,
    output reg [4:0] 			       cfg_device_number,
    output reg [2:0] 			       cfg_function_number,
    output reg [15:0] 			       cfg_dstatus,
    output reg [15:0] 			       cfg_dcommand,
    output reg [15:0] 			       cfg_lstatus,
    output reg [15:0] 			       cfg_lcommand,
    output reg [15:0] 			       cfg_dcommand2,
    output reg [2:0] 			       cfg_pcie_link_state,
    output reg 				       cfg_pmcsr_pme_en,
    output reg [1:0] 			       cfg_pmcsr_powerstate,
    output reg 				       cfg_pmcsr_pme_status,
    output reg 				       cfg_received_func_lvl_rst,

    input wire 				       cvg_err_ecrc,
    input wire 				       cfg_err_ur,
    input wire 				       cfg_err_cpl_timeout,
    input wire 				       cfg_err_cpl_unexpect,
    input wire 				       cfg_err_cpl_abort,
    input wire 				       cfg_err_posted,
    input wire 				       cfg_err_cor,
    input wire 				       cfg_err_atomic_egress_blocked,
    input wire 				       cfg_err_internal_cor,
    input wire 				       cfg_err_malformed,
    input wire 				       cfg_err_mc_blocked,
    input wire 				       cfg_err_poisoned,
    input wire 				       cfg_err_norecovery,
    input wire 				       cfg_err_tlp_cpl_header,
    output reg 				       cfg_err_clk_rdy,
    input wire 				       cfg_err_locked,
    input wire 				       cfg_err_acs,
    input wire 				       cfg_err_internal_uncor,

    input wire 				       cfg_trn_pending,

    input wire 				       cfg_pm_halt_aspm_l0s,
    input wire 				       cfg_pm_halt_aspm_l1,
    input wire 				       cfg_pm_force_state_en,
    input wire [1:0] 			       cfg_pm_force_state,
    input wire 				       cfg_pm_wake,
    input wire 				       cfg_pm_send_pme_to,

    input wire [63:0] 			       cfg_dsn,
    input wire 				       cfg_interrupt,
    output reg 				       cfg_interrupt_rdy,
    input wire 				       cfg_interrupt_assert,
    input wire [7:0] 			       cfg_interrupt_di,
    output reg [7:0] 			       cfg_interrupt_do,
    output reg [2:0] 			       cfg_interrupt_mmenable,
    output reg 				       cfg_interrupt_msienable,
    output reg 				       cfg_interrupt_msixenable,
    output reg 				       cfg_interrupt_msixfm,
    input wire 				       cfg_interrupt_stat,

    input wire [4:0] 			       cfg_pciecap_interrupt_msgnum,

    output reg 				       cfg_to_turnoff,
    input wire 				       cfg_turnoff_ok,

    input wire [7:0] 			       cfg_ds_bus_number,
    input wire [4:0] 			       cfg_ds_device_number,
    input wire [2:0] 			       cfg_ds_function_number,

    output reg 				       cfg_msg_received,
    output reg [15:0] 			       cfg_msg_data,

    output reg 				       cfg_bridge_serr_en,
    output reg 				       cfg_slot_control_electromech_il_ctl_pulse,
    output reg 				       cfg_root_control_syserr_corr_err_en,
    output reg 				       cfg_root_control_syserr_non_fatal_err_en,
    output reg 				       cfg_root_control_syserr_fatal_err_en,
    output reg 				       cfg_root_control_pme_int_en,
    output reg 				       cfg_aer_rooterr_corr_err_reporting_en,
    output reg 				       cfg_aer_rooterr_non_fatal_err_reporting_en,
    output reg 				       cfg_aer_rooterr_fatal_err_reporting_en,
    output reg 				       cfg_aer_rooterr_corr_err_received,
    output reg 				       cfg_aer_rooterr_non_fatal_err_received,
    output reg 				       cfg_aer_rooterr_fatal_err_received,
    output reg 				       cfg_msg_received_err_cor,
    output reg 				       cfg_msg_received_err_non_fatal,
    output reg 				       cfg_msg_received_err_fatal,
    output reg 				       cfg_msg_received_pm_as_nak,
    output reg 				       cfg_msg_received_pm_pme,
    output reg 				       cfg_msg_received_pme_to_ack,
    output reg 				       cfg_msg_received_assert_int_a,
    output reg 				       cfg_msg_received_assert_int_b,
    output reg 				       cfg_msg_received_assert_int_c,
    output reg 				       cfg_msg_received_assert_int_d,
    output reg 				       cfg_msg_received_deassert_int_a,
    output reg 				       cfg_msg_received_deassert_int_b,
    output reg 				       cfg_msg_received_deassert_int_c,
    output reg 				       cfg_msg_received_deassert_int_d,
    output reg 				       cfg_msg_received_setslotpowerlimit,
    input wire [1:0] 			       pl_directed_link_change,
    input wire [1:0] 			       pl_directed_link_width,
    input wire 				       pl_directed_link_auton,
    input wire 				       pl_upstream_prefer_deemph,
    output reg 				       pl_sel_lnk_rate,
    output reg [1:0] 			       pl_sel_lnk_width,
    output reg [5:0] 			       pl_ltssm_state,
    output reg [1:0] 			       pl_lane_reversal_mode,
    output reg 				       pl_phy_lnk_up,
    output reg [2:0] 			       pl_tx_pm_state,
    output reg [1:0] 			       pl_rx_pm_state,
    output reg 				       pl_link_upcfg_cap,
    output reg 				       pl_link_gen2_cap,
    output reg 				       pl_link_partner_gen2_supported,
    output reg [2:0] 			       pl_initial_link_width,
    output reg 				       pl_directed_change_done,
    output reg 				       pl_received_hot_rst,
    input wire 				       pl_transmit_hot_rst,
    input wire 				       pl_downstream_deemph_source,

    input wire [127:0] 			       cfg_err_aer_headerlog,
    input wire [4:0] 			       cfg_aer_interrupt_msgnum,
    output reg 				       cfg_err_aer_headerlog_set,
    output reg 				       cfg_aer_ecrc_check_en,
    output reg 				       cfg_aer_ecrc_gen_en,
    output reg [6:0] 			       cfg_vc_tcvc_map

    /* */);

   // The *_drv signals are copies of the port signals that are driven
   // by the $simbus_until system function. After the UNTIL completes,
   // we assign them to the proper pins by a non-blocking assignment so
   // that the relationship with the clock is proper.

   // The *_int signals are copies of the port inputs that are driven
   // by internal state machines. We process some of the signals
   // internally, so we need an internal version of the channel that
   // doesn't interfere with the user signals.
   
   reg 	      user_reset_drv = 1'bz;
   reg 	      user_lnk_up_drv = 1'b0;

   reg [63:0] m_axis_rx_tdata_drv = 64'bz;
   reg [7:0]  m_axis_rx_tkeep_drv = 8'bz;
   wire       m_axis_rx_tready_int = 1'bz;
   reg        m_axis_rx_tlast_drv = 1'bz;
   reg        m_axis_rx_tvalid_drv = 1'bz;
   reg [21:0] m_axis_rx_tuser_drv = 22'bz;

   reg [63:0] m_axis_rx_tdata_int;
   reg [7:0]  m_axis_rx_tkeep_int;
   reg        m_axis_rx_tlast_int;
   reg        m_axis_rx_tvalid_int;
   reg [21:0] m_axis_rx_tuser_int;

   wire [63:0] s_axis_tx_tdata_int = 64'bz;
   wire [7:0]  s_axis_tx_tkeep_int =  8'bz;
   reg 	       s_axis_tx_tready_drv = 1'bz;
   reg 	       s_axis_tx_tready_int = 1'bz;
   wire        s_axis_tx_tlast_int =  1'bz;
   wire        s_axis_tx_tvalid_int = 1'bz;
   wire [3:0]  s_axis_tx_tuser_int  = 4'bz;

   time      deltatime;
   integer   bus;
   reg 	     trig;

   initial begin
      // This connects to the bus and sends a message that logically
      // attaches the design to the system.
      bus = $simbus_connect(name);
      if (bus < 0) begin
	 $display("ERROR: Unable to connect");
	 $finish;
      end

      deltatime = 1;
      forever begin
	 // The deltatime variable is the amount of time that this
	 // node is allowed to advance. The time is normally the time
	 // to the next clock edge or edge for any other asynchronous
	 // signal. On startup, this delay is non-zero but tiny so
	 // that the simulation can settle, and I have initial values
	 // to send to the server.
	 
	 #(deltatime) /* Wait for the next remote event. */;

	 // Report the current state of the pins at the connector,
	 // along with the current time. For the first iteration, this
	 // gets us synchronized with the server with the time.
	 $simbus_ready(bus,
		       /* Receive Interace (to remote) */
		       "m_axis_rx_tready", m_axis_rx_tready_int, 1'bz,
		       /* Transmit Interface (to remote) */
		       "s_axis_tx_tdata",  tx_cfg_gnt? s_axis_tx_tdata_int  : s_axis_tx_tdata, 64'bz,
		       "s_axis_tx_tkeep",  tx_cfg_gnt? s_axis_tx_tkeep_int  : s_axis_tx_tkeep,  8'bz,
		       "s_axis_tx_tlast",  tx_cfg_gnt? s_axis_tx_tlast_int  : s_axis_tx_tlast,  1'bz,
		       "s_axis_tx_tvalid", tx_cfg_gnt? s_axis_tx_tvalid_int : s_axis_tx_tvalid, 1'bz,
		       "s_axis_tx_tuser",  tx_cfg_gnt? s_axis_tx_tuser_int  : s_axis_tx_tuser,  4'bz
		       /* */);

	 // Check if the bus is ready for me to continue. The $simbus_poll
	 // function will set the trig to 0 or 1 depending on whether
	 // the $simbus_until function can continue without blocking.
	 // Wait if it will block. The poll will change the trig later
	 // once data arrives from the server.
	 $simbus_poll(bus, trig);
	 wait (trig) ;

	 // The server responds to this task call when it is ready
	 // for this device to advance some more. This task waits
	 // for the GO message from the server, which includes the
	 // values to drive to the various signals that I indicate.
	 // When the server responds, this task assigns those values
	 // to the arguments, and returns. The $simbus_until function
	 // automatically converts the time delay from the server to
	 // the units of the local scope.
	 deltatime = $simbus_until(bus,
				   /* Common Interface (from remote) */
				   "user_clk",    user_clk_out,
				   "user_reset",  user_reset_drv,
				   "user_lnk_up", user_lnk_up_drv,
				   /* Receive channel (from remote) */
				   "m_axis_rx_tdata", m_axis_rx_tdata_drv,
				   "m_axis_rx_tkeep", m_axis_rx_tkeep_drv,
				   "m_axis_rx_tlast", m_axis_rx_tlast_drv,
				   "m_axis_rx_tvalid",m_axis_rx_tvalid_drv,
				   "m_axis_rx_tuser", m_axis_rx_tuser_drv,
				   /* Transmite channel (from remote) */
				   "s_axis_tx_tready",s_axis_tx_tready_drv
				   /* */);

	 user_reset_out <= user_reset_drv;
	 user_lnk_up <= user_lnk_up_drv;
	 user_app_rdy <= 1'b1;

	 m_axis_rx_tdata_int  <= m_axis_rx_tdata_drv;
	 m_axis_rx_tkeep_int  <= m_axis_rx_tkeep_drv;
	 m_axis_rx_tlast_int  <= m_axis_rx_tlast_drv;
	 m_axis_rx_tvalid_int <= m_axis_rx_tvalid_drv;
	 m_axis_rx_tuser_int  <= m_axis_rx_tuser_drv;

	 if (tx_cfg_gnt) begin
	    s_axis_tx_tready_int <= s_axis_tx_tready_drv;
	    s_axis_tx_tready     <= 1'b0;
	 end else begin
	    s_axis_tx_tready_int <= 1'b0;
	    s_axis_tx_tready     <= s_axis_tx_tready_drv;
	 end
      end // forever begin
   end // initial begin

   xilinx_pcie_cfg_space
     #(.ven_id(ven_id),
       .dev_id(dev_id),
       .rev_id(rev_id),
       .subsys_id(subsys_id),
       .subsys_ven_id(subsys_ven_id),
       .bar_0(bar_0),
       .bar_1(bar_1),
       .bar_2(bar_2),
       .bar_3(bar_3),
       .bar_4(bar_4),
       .bar_5(bar_5),
       .xrom_bar(xrom_bar)
       /* */) cfg_space
       (.user_clk(user_clk_out),
	.user_reset(user_reset_out),
	// Transmit channel arbitration
	.tx_cfg_req(tx_cfg_req),
	.tx_cfg_gnt(tx_cfg_gnt),
	// Receive channel AXI4 Stream
	.m_axis_rx_tdata (m_axis_rx_tdata_int),
	.m_axis_rx_tkeep (m_axis_rx_tkeep_int),
	.m_axis_rx_tlast (m_axis_rx_tlast_int),
	.m_axis_rx_tready(m_axis_rx_tready_int),
	.m_axis_rx_tvalid(m_axis_rx_tvalid_int),
	.m_axis_rx_tuser (m_axis_rx_tuser_int),
	// Receive channel AXI4 Stream (filtered for user)
	.o_axis_rx_tdata (m_axis_rx_tdata),
	.o_axis_rx_tkeep (m_axis_rx_tkeep),
	.o_axis_rx_tlast (m_axis_rx_tlast),
	.o_axis_rx_tready(m_axis_rx_tready),
	.o_axis_rx_tvalid(m_axis_rx_tvalid),
	.o_axis_rx_tuser (m_axis_rx_tuser),
	// Transmit channel AXI4 Stream
	.s_axis_tx_tdata (s_axis_tx_tdata_int),
	.s_axis_tx_tkeep (s_axis_tx_tkeep_int),
	.s_axis_tx_tlast (s_axis_tx_tlast_int),
	.s_axis_tx_tready(s_axis_tx_tready_int),
	.s_axis_tx_tvalid(s_axis_tx_tvalid_int),
	.s_axis_tx_tuser (s_axis_tx_tuser_int)
	/* */);

endmodule // xilinx_pcie_slot

/*
 * The PCIe core handles configuration space details internally, so this
 * module picks off config TLPs and handles them.
 */
module xilinx_pcie_cfg_space
  #(parameter ven_id = "FFFF",
    parameter dev_id = "FFFF",
    parameter rev_id = "00",
    parameter subsys_ven_id = "FFFF",
    parameter subsys_id     = "FFFF",
    // BARs
    parameter bar_0    = "FFFFFFF4",
    parameter bar_1    = "FFFFFFFF",
    parameter bar_2    = "00000000",
    parameter bar_3    = "00000000",
    parameter bar_4    = "00000000",
    parameter bar_5    = "00000000",
    parameter xrom_bar = "00000000"
    /* */)
   (input wire user_clk,
    input wire 	       user_reset,

    output reg 	       tx_cfg_req,
    input wire 	       tx_cfg_gnt,

    // Receive channel AXI4 Stream
    input wire [63:0]  m_axis_rx_tdata,
    input wire [7:0]   m_axis_rx_tkeep,
    input wire 	       m_axis_rx_tlast,
    output wire        m_axis_rx_tready,
    input wire 	       m_axis_rx_tvalid,
    input wire [21:0]  m_axis_rx_tuser,

    // Receive channel AXI4 Stream (passed to user)
    output wire [63:0] o_axis_rx_tdata,
    output wire [7:0]  o_axis_rx_tkeep,
    output wire        o_axis_rx_tlast,
    input wire 	       o_axis_rx_tready,
    output wire        o_axis_rx_tvalid,
    output wire [21:0] o_axis_rx_tuser,

    // Transmit channel AXI4 stream
    output reg [63:0]  s_axis_tx_tdata,
    output reg [7:0]   s_axis_tx_tkeep,
    output reg 	       s_axis_tx_tlast,
    input wire 	       s_axis_tx_tready,
    output reg 	       s_axis_tx_tvalid,
    output reg [3:0]   s_axis_tx_tuser
    /* */);

   reg [31:0]  cfg_mem[0 : 1023];

   reg [31:0]  bar_mask[0:5];

   // State for receiving a TLP.
   reg [31:0]  tlp [0:3];
   reg [7:0]   ntlp;
   reg 	       tlp_is_config, tlp_is_skip;

   // TREADY signal from the buffer. The flow control through this module
   // relies on the buf as well as the config receiver itself.
   wire        m_axis_rx_tready_buf;
   reg 	       m_axis_rx_tready_int;
   assign m_axis_rx_tready = m_axis_rx_tready_buf & m_axis_rx_tready_int;

   // State for transmitting a completion TLP.
   reg [63:0]  cmp_data[0:1];
   reg [7:0]   cmp_keep[0:1];
   reg [7:0]   ncmp, cmp_cur;

   // Configuration writes are sometimes non-trivial, because not all
   // registers, and in some cases not all bits, are necessarily writable.
   // This task figures all that out and causes the correct memory bits
   // to be written.
   task do_cfg_write(input [11:2]adr, input [31:0]val, input [3:0]ben);
      reg [31:0] mask;
      begin
	 mask[31:24] = ben[3]? 8'hff : 8'h00;
	 mask[23:16] = ben[2]? 8'hff : 8'h00;
	 mask[15: 8] = ben[1]? 8'hff : 8'h00;
	 mask[ 7: 0] = ben[0]? 8'hff : 8'h00;
	 case (adr)
	   0: begin
	   end
	   // BARs can only have some of their bits written.
	   4,5,6,7,8,9: begin
	      mask = mask & bar_mask[adr-4];
	      cfg_mem[adr] = val&mask | cfg_mem[adr]&~mask;
	   end

	   default: begin
	      cfg_mem[adr] = val&mask | cfg_mem[adr]&~mask;
	   end
	 endcase
      end
   endtask // do_cfg_write

   // A configuration TLP is finished, so process it and make
   // a completion.
   task make_completion;
      reg [7:0]  tlp_tag;
      reg [11:2] tlp_adr;
      reg [31:0] tlp_val;
      reg [3:0]  tlp_ben;
      begin
	 if (ncmp != 0) begin
	    $display("%m: ERROR: Completion buffer overrun?!");
	    $finish(1);
	 end
	 // Extract parts that we will need to make up the completion
	 tlp_tag = tlp[1][15:8];
	 tlp_ben = tlp[1][3:0];
	 tlp_adr = tlp[2][11:2];
	 tlp_val = ntlp==4? tlp[3] : 32'h00000000;

	 // Generate the completion based on the request.
	 case (tlp[0][31:24])
	   'b000_00100: begin // CfgRd0
	      $display("%m: Got a CfgRd0 (tag=%h, adr=%h, , ben=%b, val=%h)",
		       tlp_tag, tlp_adr, tlp_ben, cfg_mem[tlp_adr]);
	      cmp_data[0] = 64'h00000000_4a000001;
	      cmp_data[1] = {cfg_mem[tlp_adr], 16'h0000, tlp_tag, 8'h00};
	      cmp_keep[0] = 8'hff;
	      cmp_keep[1] = 8'hff;
	      ncmp <= 2;
	      cmp_cur <= 0;
	   end

	   'b010_00100: begin // CfgWr0
	      $display("%m: Got a CfgWr0 (tag=%h, adr-%h, ben=%b val=%h)", tlp_tag, tlp_adr, tlp_ben, tlp_val);
	      do_cfg_write(tlp_adr, tlp_val, tlp_ben);
	      cmp_data[0] = 64'h00000000_0a000000;
	      cmp_data[1] = {32'h00000000, 16'h0000, tlp_tag, 8'h00};
	      cmp_keep[0] = 8'hff;
	      cmp_keep[1] = 8'h0f;
	      ncmp <= 2;
	      cmp_cur <= 0;
	   end

	   default: begin // Unknown or unsupported
	   end
	 endcase // case (tlp[0][31:24])
      end
   endtask

   always @(posedge user_clk) begin : tlp_in_block

      reg  [3:0] idx, nbyte, keep_bit;
      reg [31:0] val;

      if (user_reset) begin
	 ntlp <= 0;
	 tlp_is_config <= 0;
	 tlp_is_skip   <= 0;
	 m_axis_rx_tready_int <= 1;

      end else if ((tlp_is_config||~tlp_is_skip) && m_axis_rx_tready && m_axis_rx_tvalid) begin

	 // Extract the bytes of the TLP from the word.
	 nbyte = 0;
	 for (idx = 0 ; idx < 8 ; idx = idx+1) begin
	    keep_bit = {idx[2], 2'd3-idx[1:0]};
	    if (m_axis_rx_tkeep[keep_bit]) begin
	       val = {val[23:0], m_axis_rx_tdata[8*keep_bit +: 8]};
	       nbyte = nbyte+1;
	       if (nbyte == 4) begin
		  tlp[ntlp] = val;
		  ntlp = ntlp+1;
		  nbyte = 0;
	       end
	    end
	 end

	 // The remote probably always sends an even number of TLP words
	 // in each beat of the AXI4Stream. It is theoretically possible,
	 // but I don't the the Xilinx PCIe core does that.
	 if (nbyte != 0) begin
	    $display("%m: ERROR: I don't know how to handle odd bytes in tdata!");
	    $finish(1);
	 end

	 // If this is the first word of the TLP, then check if it is a
	 // a config. If it is, then set a flag so that we continue to
	 // capture the tlp. Otherwise, set a different flag so that we
	 // know to skip the rest of this TLP.
	 if (! (tlp_is_config || tlp_is_skip)) begin
	    $display("%m: First word of tlp is %h", tlp[0]);
	    case (tlp[0][31:24])
	      'b000_00100: tlp_is_config <= 1; // CfgRd0
	      'b010_00100: tlp_is_config <= 1; // CfgWr0
	      'b000_00101: tlp_is_config <= 1; // CfgRd1
	      'b010_00101: tlp_is_config <= 1; // CfgWr1
	      default:     tlp_is_skip   <= 1;
	    endcase
	 end

	 if (m_axis_rx_tlast) begin
	    // Now we have a Config TLP, process it by forming a completion.
	    $display("%m: Got a TLP: ntlp=%0d", ntlp);
	    make_completion;
	   
	    // Done with the TLP, clear the rx state machine
	    ntlp = 0;
	    tlp_is_config <= 0;
	    tlp_is_skip   <= 0;
	 end

      end else if (m_axis_rx_tready && m_axis_rx_tvalid && m_axis_rx_tlast) begin
	 // This was not a config TLP, so now that it is done,
	 // restart the state machine to watch for the next TLP.
	 ntlp = 0;
	 tlp_is_config <= 0;
	 tlp_is_skip   <= 0;

      end else begin
	
      end
   end

   task drive_cmp_word(input reg [7:0] idx);
      begin
	 s_axis_tx_tdata  <= cmp_data[idx];
	 s_axis_tx_tkeep  <= cmp_keep[idx];
	 s_axis_tx_tlast  <= (idx+1) == ncmp;
	 s_axis_tx_tvalid <= 1;
      end
   endtask // drive_cmp_word

   always @(posedge user_clk) begin : cmp_machine

      reg [15:0] val16;
      if (user_reset) begin
	 cmp_cur <= 0;
	 ncmp    <= 0;
	 s_axis_tx_tvalid <= 0;
	 s_axis_tx_tuser  <= 0;
	 tx_cfg_req       <= 0;

      end else if (s_axis_tx_tready & s_axis_tx_tvalid) begin
	 // If the remote consumes a word, set up the next word. If the
	 // previous word was the last (tlast) then clear the cmp buffer
	 // and the stream signals. Otherwise, drive the next word and
	 // increment the transmit count.
	 if (s_axis_tx_tlast) begin
	    s_axis_tx_tvalid <= 0;
	    s_axis_tx_tlast  <= 0;
	    cmp_cur <= 0;
	    ncmp    <= 0;
	    tx_cfg_req <= 0;

	 end else begin
	    drive_cmp_word(cmp_cur);
	    cmp_cur <= cmp_cur+1;
	 end

      end else if (s_axis_tx_tvalid==0 && ncmp!=0) begin
	 // Is there a completion waiting to be started?

	 // Request the transmit stream, if not requested already.
	 if (tx_cfg_req == 0)
	   tx_cfg_req <= 1;
	 // Drive the first word of the completion TLP. The remaining
	 // words will be handled in another condition.
	 if (tx_cfg_gnt) begin
	    drive_cmp_word(0);
	    cmp_cur <= 1;
	 end

      end else begin
      end
   end // block: cmp_machine

   xilinx_pcie_rx_buffer buffer
     (.user_clk(user_clk),
      .user_reset(user_reset),

      .tlp_pass(tlp_is_skip),
      .tlp_drop(tlp_is_config),

      .i_axis_rx_tdata(m_axis_rx_tdata),
      .i_axis_rx_tkeep(m_axis_rx_tkeep),
      .i_axis_rx_tlast(m_axis_rx_tlast),
      .i_axis_rx_tready(m_axis_rx_tready_buf),
      .i_axis_rx_tvalid(m_axis_rx_tvalid),
      .i_axis_rx_tuser(m_axis_rx_tuser),

      .o_axis_rx_tdata(o_axis_rx_tdata),
      .o_axis_rx_tkeep(o_axis_rx_tkeep),
      .o_axis_rx_tlast(o_axis_rx_tlast),
      .o_axis_rx_tready(o_axis_rx_tready),
      .o_axis_rx_tvalid(o_axis_rx_tvalid),
      .o_axis_rx_tuser(o_axis_rx_tuser)
      /* */);

   // Pre-reset initialization
   initial begin : init
      integer rc;
      reg [15:0] val16;
      reg [31:0] val32;

      // Initialize the config space.
      rc = $sscanf(ven_id, "%h", val16);
      cfg_mem[0][15:0] = val16;
      rc = $sscanf(dev_id, "%h", val16);
      cfg_mem[0][31:16] = val16;
      rc = $sscanf(bar_0, "%h", val32);
      cfg_mem[4] = val32;
      bar_mask[0] = val32 & 32'hfffffff0;
      rc = $sscanf(bar_1, "%h", val32);
      cfg_mem[5] = val32;
      bar_mask[1] = val32 & 32'hfffffff0;
      rc = $sscanf(bar_2, "%h", val32);
      cfg_mem[6] = val32;
      bar_mask[2] = val32 & 32'hfffffff0;
      rc = $sscanf(bar_3, "%h", val32);
      cfg_mem[7] = val32;
      bar_mask[3] = val32 & 32'hfffffff0;
      rc = $sscanf(bar_4, "%h", val32);
      cfg_mem[8] = val32;
      bar_mask[4] = val32 & 32'hfffffff0;
      rc = $sscanf(bar_5, "%h", val32);
      cfg_mem[9] = val32;
      bar_mask[5] = val32 & 32'hfffffff0;
      rc = $sscanf(subsys_ven_id, "%h", val16);
      cfg_mem[11][15:0] = val16;
      rc = $sscanf(subsys_id, "%h", val16);
      cfg_mem[11][31:16] = val16;
      rc = $sscanf(xrom_bar, "%h", val32);
      cfg_mem[12] = val32;
      $display("%m: Initialize config space:");
      for (rc = 0 ; rc < 16 ; rc = rc+4)
	$display("%m: %h: %h %h %h %h", rc[7:0], cfg_mem[rc+0], cfg_mem[rc+1], cfg_mem[rc+2], cfg_mem[rc+3]);
   end

endmodule // xilinx_pcie_cfg_space

/*
 * This module captures and buffers a TLP from the remote. Once it is
 * determined by the context that the incoming TLP is to be passed
 * through, it starts directing it through to the output. If it is
 * determinted that the TLP is to be consumed, then it is ignored
 * and dropped.
 */
module xilinx_pcie_rx_buffer
  (/* */
   input wire 	     user_clk,
   input wire 	     user_reset,

   input wire 	     tlp_pass,
   input wire 	     tlp_drop,

   // Receive channel AXI4 Stream (master side)
   input wire [63:0] i_axis_rx_tdata,
   input wire [7:0]  i_axis_rx_tkeep,
   input wire 	     i_axis_rx_tlast,
   output reg 	     i_axis_rx_tready,
   input wire 	     i_axis_rx_tvalid,
   input wire [21:0] i_axis_rx_tuser,

    // Receive channel AXI4 Stream (master side)
   output reg [63:0] o_axis_rx_tdata,
   output reg [7:0]  o_axis_rx_tkeep,
   output reg 	     o_axis_rx_tlast,
   input wire 	     o_axis_rx_tready,
   output reg 	     o_axis_rx_tvalid,
   output reg [21:0] o_axis_rx_tuser
   /* */);

   reg [63:0] 	     tdata_buf[0:3];
   reg [7:0] 	     tkeep_buf[0:3];
   reg 		     tlast_buf[0:3];
   reg [21:0] 	     tuser_buf[0:3];

   reg [1:0] 	     ptr;
   reg [2:0] 	     fill;

   task push_beat;
      reg [1:0] nxt;
      begin
	 nxt = ptr + fill;
	 tdata_buf[nxt] = i_axis_rx_tdata;
	 tkeep_buf[nxt] = i_axis_rx_tkeep;
	 tlast_buf[nxt] = i_axis_rx_tlast;
	 tuser_buf[nxt] = i_axis_rx_tuser;
	 fill = fill + 1;
      end
   endtask // always

   task pull_beat;
      begin
	 o_axis_rx_tdata  <= tdata_buf[ptr];
	 o_axis_rx_tkeep  <= tkeep_buf[ptr];
	 o_axis_rx_tlast  <= tlast_buf[ptr];
	 o_axis_rx_tvalid <= 1;
	 o_axis_rx_tuser  <= tuser_buf[ptr];
	 ptr = ptr+1;
      end
   endtask // always


   always @(posedge user_clk)
     if (user_reset) begin
	ptr  = 0;
	fill = 0;

	i_axis_rx_tready <= 1;

	o_axis_rx_tlast  <= 0;
	o_axis_rx_tvalid <= 0;

     end else if (tlp_drop) begin
	// If we are in drop mode, then forget the beats that
	// we collected so far, and ignore the remaining until
	// we exit from drop mode.
	ptr  = 0;
	fill = 0;

     end else if (tlp_pass) begin
	// If we are in pass mode, then clock the TLP out to the
	// destination as quickly as it will go.
	
	if (fill>0 && o_axis_rx_tready && o_axis_rx_tvalid)
	  pull_beat;
	else if (fill>0 && !o_axis_rx_tvalid)
	  pull_beat;
	else if (fill==0 && o_axis_rx_tready && o_axis_rx_tvalid)
	  o_axis_rx_tvalid <= 0;

	if (i_axis_rx_tready & i_axis_rx_tvalid)
	  push_beat;

     end else begin
     end

endmodule // xilinx_pcie_rx_buffer