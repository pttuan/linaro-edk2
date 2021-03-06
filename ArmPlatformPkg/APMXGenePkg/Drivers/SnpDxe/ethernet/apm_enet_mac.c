/**
 * Copyright (c) 2013, AppliedMicro Corp. All rights reserved.
 *
 * This program and the accompanying materials
 * are licensed and made available under the terms and conditions of the BSD License
 * which accompanies this distribution.  The full text of the license may be found at
 * http://opensource.org/licenses/bsd-license.php
 *
 * THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 * WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 *
 **/

#include "apm_enet_access.h"
#include "apm_enet_mac.h"
#include "apm_enet_csr.h"

#define MAX_LOOP_POLL_TIMEMS	500
#define MAX_LOOP_POLL_CNT	10
#define ACCESS_DELAY_TIMEMS	(MAX_LOOP_POLL_TIMEMS / MAX_LOOP_POLL_CNT)

/* No need to poll CSR faster than 1 ms */
#if defined(PHY_CSR_ACCESS_NO_DELAY)
#define PHY_CSR_POLL_DELAY
#else
#ifdef BOOTLOADER
#define PHY_CSR_POLL_DELAY
#else
#define PHY_CSR_POLL_DELAY	msleep(1);
#endif
#endif

/* Start MAC related functions */
void apm_gmac_set_mac_addr(struct apm_data_priv *priv,
			unsigned char *dev_addr)
{
	u32 a_hi;
	u32 a_lo;

	a_hi = *(u32 *) &dev_addr[0];
	a_lo = (u32) *(u16 *) &dev_addr[4];

	/* Write higher 4 octects to station register */
	apm_enet_write(priv, BLOCK_MCX_MAC, STATION_ADDR0_ADDR, a_hi);

	a_lo <<= 16;
	a_lo |= (priv->phy_addr & 0xFFFF);

	/* Write lower 2 octects to station register */
	apm_enet_write(priv, BLOCK_MCX_MAC, STATION_ADDR1_ADDR, a_lo);
}

int apm_gmac_is_rx_flow_control(struct apm_data_priv *priv)
{
	u32 data;

	/* Read current value of the config 1 register */
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &data);

	return RX_FLOW_EN1_RD(data);
}

static void enet_ecc_init(struct apm_data_priv *priv)
{
	u32 data;
	int mwait = 0, i;

	apm_enet_write(priv, BLOCK_ETH_DIAG_CSR, ENET_CFG_MEM_RAM_SHUTDOWN_ADDR, 0x0);

	apm_enet_read(priv, BLOCK_ETH_DIAG_CSR, ENET_CFG_MEM_RAM_SHUTDOWN_ADDR, &data);

	apm_enet_read(priv, BLOCK_ETH_DIAG_CSR, ENET_BLOCK_MEM_RDY_ADDR, &data);
	do {
		apm_enet_read(priv, BLOCK_ETH_DIAG_CSR, ENET_BLOCK_MEM_RDY_ADDR, &data);
		if (mwait++ >= 1000)
			break;
		udelay(10);
	} while (data != 0xffffffff);

	ENET_DEBUG("enet_ecc_init mwait=%d data=0x%x\n", mwait, data);

	for (i = 0; i < 5; i++) {
		apm_enet_read(priv, BLOCK_ETH_DIAG_CSR, ENET_BLOCK_MEM_RDY_ADDR, &data);
	}
}

void enet_sds_ind_csr_reg_wr(struct apm_data_priv *priv, u32 offset, u32 data)
{
	u32 value, timeout;
	u32 SDS_Ind_Command_Addr;
	u32 SDS_Ind_Command_Done;

	/* write 1 to clear indirect error detected/ind cmd done */
	value = (CFG_IND_ADDR_WR(0)
		| CFG_IND_ERR_WR(1)
		| CFG_IND_CMD_DONE_WR(1)
		| CFG_IND_RD_CMD_WR(0)
		| CFG_IND_WR_CMD_WR(0));
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, value);
	PHY_PRINT("%s cmd value=0x%x -> 0x%llx\n", __func__, value,
		SATA_ENET_SDS_CSR_REG_BASE_ADDR + SATA_ENET_SDS_IND_CMD_REG_ADDR);

	value = data;
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_WDATA_REG_ADDR, value);
	PHY_PRINT("%s data value=0x%x -> 0x%llx\n", __func__, value,
		SATA_ENET_SDS_CSR_REG_BASE_ADDR + SATA_ENET_SDS_IND_WDATA_REG_ADDR);

	SDS_Ind_Command_Addr = offset; /* offset[15:0] */
	value = (CFG_IND_ADDR_WR(SDS_Ind_Command_Addr)
		| CFG_IND_ERR_WR(0)
		| CFG_IND_CMD_DONE_WR(0)
		| CFG_IND_RD_CMD_WR(0)
		| CFG_IND_WR_CMD_WR(1));
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, value);
	SDS_Ind_Command_Done = 0;
	while (SDS_Ind_Command_Done == 0) {
		apm_enet_read(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, &value);
		PHY_PRINT("%s cmd_done=0x%x <- 0x%llx\n", __func__, value,
			SATA_ENET_SDS_CSR_REG_BASE_ADDR + SATA_ENET_SDS_IND_CMD_REG_ADDR);
		SDS_Ind_Command_Done = CFG_IND_CMD_DONE_RD(value);
	}
	for(timeout=0;timeout<1000;timeout++);
}

u32 enet_sds_ind_csr_reg_rd(struct apm_data_priv *priv, u32 offset)
{
	u32 value, SDS_Ind_Command_Addr, SDS_Ind_Command_Done;

	PHY_PRINT("%s offset=0x%x\n", __func__, offset);
	/* write 1 to clear indirect error detected/ind cmd done */
	value = (CFG_IND_ADDR_WR(0)
		| CFG_IND_ERR_WR(1)
		| CFG_IND_CMD_DONE_WR(1)
		| CFG_IND_RD_CMD_WR(0)
		| CFG_IND_WR_CMD_WR(0));
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, value);
	PHY_PRINT("%s value=0x%x -> 0x%llx\n", __func__, value,
		  SATA_ENET_SDS_CSR_REG_BASE_ADDR + SATA_ENET_SDS_IND_CMD_REG_ADDR);

	SDS_Ind_Command_Addr = offset; /* offset[15:0] */
	value = (CFG_IND_ADDR_WR(SDS_Ind_Command_Addr)
		| CFG_IND_ERR_WR(0)
		| CFG_IND_CMD_DONE_WR(0)
		| CFG_IND_RD_CMD_WR(1)
		| CFG_IND_WR_CMD_WR(0));
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, value);
	PHY_PRINT("%s cmd value=0x%x\n", __func__, value);

	SDS_Ind_Command_Done = 0;
	while (SDS_Ind_Command_Done == 0) {
		apm_enet_read(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_CMD_REG_ADDR, &value);
		PHY_PRINT("%s read cmd_done=0x%x <- 0x%llx\n", __func__, value,
			SATA_ENET_SDS_CSR_REG_BASE_ADDR + SATA_ENET_SDS_IND_CMD_REG_ADDR);
		SDS_Ind_Command_Done = CFG_IND_CMD_DONE_RD(value);
	}

	apm_enet_read(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_IND_RDATA_REG_ADDR, &value);
	PHY_PRINT("%s value=0x%x\n", __func__, value);

	return value;
}

u32 sm_enet_set(u32 ori_val, u32 set_val, u32 end, u32 start)
{
	u32 mask = 0, new_val;
	int i;

	for (i = 0; i < 16; i++) {
		if ((i < start) || (i > end))
			mask = mask | (1 << i);
		else
			mask = mask | (0 << i);
	}
	new_val = (ori_val & mask) | (set_val << start);
	return new_val;
}

// function taken from SATA code-Satish 25/02/2013
void serdes_reset_rxd_rxa(struct apm_data_priv *priv, int port)
{
	u32 value;
	int inst_base;

	if ((port == ENET_0) || (port == ENET_2)) {
		inst_base = 0x0400;
		/* printf (" CH0 RX Reset Digital ...\n\r"); */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 0, 8, 8); /* digital reset == 1'b0*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);

		/* printf (" CH0 RX Reset Analog ...\n\r"); */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 0, 7, 7); /* analog reset == 1'b0*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
	} else {
		inst_base = 0x0600;
		/* printf (" CH1 RX Reset Digital ...\n\r"); */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 0, 8, 8); /* digital reset == 1'b0*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);

		/* printf (" CH1 RX Reset Analog ...\n\r"); */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 0, 7, 7); /* analog reset == 1'b0*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
	}

	/* Remove analog reset first, followed by removing digital reset */
	udelay(2);

	if ((port == ENET_0) || (port == ENET_2)) {
		inst_base = 0x0400;
		/* CH0 RX Remove Reset Analog */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 1, 7, 7); /* analog reset == 1'b1*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
		udelay(2);
		/* CH0 RX Remove Reset Digital */
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 1, 8, 8); /* digital reset == 1'b1*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
	} else {
		inst_base = 0x0600;
		/* printf (" CH1 RX Remove Reset Analog ...\n\r");*/
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 1, 7, 7); /* analog reset == 1'b1*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
		udelay(2);
		/* printf (" CH1 RX Remove Reset Digital ...\n\r");*/
		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0x7*2);
		value = sm_enet_set(value, 1, 8, 8); /* digital reset == 1'b1*/
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0x7*2, value);
	}
	udelay(2);
}

#define CMU 0
void serdes_calib(struct apm_data_priv *priv)
{
	u32 value, timeout;
	int inst_base;

	/* TERM CALIBRATION CH0 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0xd, 14, 8); /*   (pvt_code_r2a)  =0x0d */
	value = sm_enet_set(value, 0, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 1, 15, 15); /*   (pvt_term_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0, 15, 15); /*   (pvt_term_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	/* TERM CALIBRATION CH1 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0xd, 14, 8); /*   (pvt_code_r2a)  =0x0d */
	value = sm_enet_set(value, 1, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 1, 15, 15); /*   (pvt_term_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0, 15, 15); /*   (pvt_term_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);
	
	/* down calibration CH0 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0x26, 14, 8); /*   (pvt_code_r2a)  =0x26 */
	value = sm_enet_set(value, 0, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 1, 0, 0); /*   (pvt_dn_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 0, 0, 0); /*   (pvt_dn_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	/* DOWN CALIBRATION CH1 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0x26, 14, 8); /*   (pvt_code_r2a)  =0x26 */
	value = sm_enet_set(value, 1, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 1, 0, 0); /*   (pvt_dn_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 0, 0, 0); /*   (pvt_dn_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	/* UP CALIBRATION CH0 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0x28, 14, 8); /*   (pvt_code_r2a)  =0x26 */
	value = sm_enet_set(value, 0, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 1, 1, 1); /*   (pvt_up_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 0, 0, 0); /*   (pvt_up_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	/* UP CALIBRATION CH1 */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*17);
	value = sm_enet_set(value, 0x28, 14, 8); /*   (pvt_code_r2a)  =0x26 */
	value = sm_enet_set(value, 1, 7, 5); /*   (reserved)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*17, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 1, 1, 1); /*   (pvt_up_man_ena)  =0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 0, 0, 0); /*   (pvt_up_man_ena)  =0x0 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	/* SUMMER CALIBRATION CH0/CH1 */
	inst_base = 0x0400;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 14*2);
	value = sm_enet_set(value, 0x10, 5, 0); /*   (clte_latcal_man_prog) = 0xe */
	value = sm_enet_set(value, 0, 6, 6); /*   (clte_latcal_man_ena)  = 0x1 */
	enet_sds_ind_csr_reg_wr(priv,inst_base + 14*2, value);

	inst_base = 0x0600;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 14*2);
	value = sm_enet_set(value, 0xe, 5, 0); /*   (clte_latcal_man_prog) = 0xe */
	value = sm_enet_set(value, 0, 6, 6); /*   (clte_latcal_man_ena)  = 0x1 */
	enet_sds_ind_csr_reg_wr(priv,inst_base + 14*2, value);

	/* SUMMer calib toggle */
	inst_base = 0x0400;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_sum_cal_start)  = 0x1 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 1, 1, 1);
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_sum_cal_start)  = 0x0 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 0, 1, 1); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	inst_base = 0x0600;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_sum_cal_start)  = 0x1 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 1, 1, 1); 
	enet_sds_ind_csr_reg_wr(priv, inst_base + 127*2, value);

	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_sum_cal_start)  = 0x0 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 0, 1, 1); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	/* latch calib toggle */
	inst_base = 0x0400;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_lat_cal_start)  = 0x1 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 1, 2, 2); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_lat_cal_start)  = 0x0 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 0, 2, 2); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	inst_base = 0x0600;
	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_lat_cal_start)  = 0x1 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 1, 2, 2); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
	/*   (force_lat_cal_start)  = 0x0 -> SATISH incorrect, should be 2,2 */
	value = sm_enet_set(value, 0, 2, 2); 
	enet_sds_ind_csr_reg_wr(priv,inst_base + 127*2, value);

	inst_base = 0x0400;
	enet_sds_ind_csr_reg_wr(priv, inst_base + 28*2, 0x7);
	enet_sds_ind_csr_reg_wr(priv, inst_base + 31*2, 0x2a00);
	inst_base = 0x0600;
	enet_sds_ind_csr_reg_wr(priv, inst_base + 28*2, 0x7);
	enet_sds_ind_csr_reg_wr(priv, inst_base + 31*2, 0x2a00);

	for(timeout=0; timeout<0x8000; ++timeout);	//Anil 020813
}

void enet_sds_CMU_cfg(struct apm_data_priv *priv)
{
	u32 value;

	/*//////////////////////// */
	/* HSPLL Controls */
	/*//////////////////////// */
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*0);
	value = sm_enet_set(value, 7, 7, 5); /* cal_counter_res */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*0, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*1);
	value = sm_enet_set(value, 1, 13, 10); /* (pll_cp) = 0x1 */
	value = sm_enet_set(value, 5, 9, 5); /* (pll_cp_sel) = 0x5 */
	value = sm_enet_set(value, 0, 3, 3); /* (pll_manualcal) = 0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*1, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*2);
	value = sm_enet_set(value, 0xa , 4, 1); /*  (pll_lfres) = 0xa */
	value = sm_enet_set(value, 124, 13, 5); /* (pll_fbdiv) = 124 */
	value = sm_enet_set(value, 1, 15, 14); /* (pll_refdiv) = 0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*2, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*3);
	value = sm_enet_set(value, 7, 3, 0); /* (vcovarsel) = 0x1 */
	value = sm_enet_set(value, 0x1b, 9, 4); /* (vco_momsel_init) = 0x15 */
	value = sm_enet_set(value, 0x1b, 15, 10); /* (vco_mammomsel) = 0x15 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*3, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*5);
	value = sm_enet_set(value, 3, 15, 14); /* (pll_lfsmcap) = 0x3 */
	value = sm_enet_set(value, 4, 13, 12); /* (pll_lfcap) = 0x3 */
	value = sm_enet_set(value, 7, 3, 1); /* (pll_lock_resolution) = 0x7 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*5, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*6);
	value = sm_enet_set(value, 0, 10, 9); /* (pll_vregtrim) = 0x0 */
	value = sm_enet_set(value, 1, 2, 2); /* man_pvt_cal = 1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*6, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*8);
	value = sm_enet_set(value, 255, 15, 8); /* (tx_data_rate_ch3,2,1,0) = 0xFFFF */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*8, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*9);
	value = sm_enet_set(value, 1, 9, 7); /* (tx_word_mode_ch1) = 0x1 */
	value = sm_enet_set(value, 1, 6, 4); /* (tx_word_mode_ch0) = 0x1 */
	value = sm_enet_set(value, 1, 3, 3); /*   (pll_post_divby2) = 0x1 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*9, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*13);
	value = sm_enet_set(value, 0xFFFF, 15, 0); /*  (state_delay1,2,3,4) = 0xFFFF */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*13, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*14);
	value = sm_enet_set(value, 0xFFFF, 15, 0); /*  (state_delay5,6,7,8) = 0xFFFF */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*14, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
	value = sm_enet_set(value, 0, 6, 6); /* (calibration done override) = 0x1 */
	value = sm_enet_set(value, 0, 5, 5); /* (bypass_pll_lock) = 0x1 */
	value = sm_enet_set(value, 7, 4, 2); /* (vcocal_wait_btw_code) = 0x4 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*30);
	value = sm_enet_set(value, 0, 3, 3); /* (pcie_mode) = 0x0 */
	value = sm_enet_set(value, 3, 2, 1); /* (lock_count) = 0x3 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*30, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*31);
	value = sm_enet_set(value, 0xF, 4, 0); /* (los_override _ch0,1,2,3,6,7,8) = 0xF */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*31, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*32);
	value = sm_enet_set(value, 3, 2, 1); /* (pvt_cal_wait_swel) = 0x3 */
	value = sm_enet_set(value, 3, 8, 7); /* (iref_adj) = 0x3 */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*32, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*34);
	value = sm_enet_set(value, 0x8d27, 15, 0); /* (vco_cal_vth_lo_min/max, vco_cal_vth_hi_min/max) = 0x2A2A */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*34, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*37);
	value = sm_enet_set(value, 0xF, 15, 12); /* (CTLE_cal_done_ovr)  = 0xF */
	value = sm_enet_set(value, 0xF, 3, 0); /*   (FT_search_done_ovr) = 0xF */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*37, value);
}

void enet_sds_rxtx_cfg(struct apm_data_priv *priv)
{
	u32 value, inst, inst_base;
		    	
	for(inst = 0;inst < 2;inst++) {
		inst_base = 0x0400 + inst*0x0200;

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 147*2);
		/* (STMC_OVERRIDE) = 0x6 -> SATISH incorrect, should be 15, 0 */
		value = sm_enet_set(value, 6, 15, 0);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 147*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0*2);
		value = sm_enet_set(value, 0x10, 15, 11); /*   (CTLE_EQ_HR) = 0x10 */
		value = sm_enet_set(value, 0x10, 10, 6); /*   (CTLE_EQ_QR) = 0x10 */
		value = sm_enet_set(value, 0x10, 5, 1); /*   (CTLE_EQ_FR) = 0x10 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 0*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 1*2);
		value = sm_enet_set(value, 0x7, 15, 12); /*   (rxacvcm ) = 0x7 */
		value = sm_enet_set(value, 0x0, 11, 7); /*   (CTLE_EQ) = 0x09 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 1*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 2*2);
		value = sm_enet_set(value, 1, 8, 8); /*   (vtt_ena) = 0x1 */
		value = sm_enet_set(value, 1, 7, 6); /*   (vtt_sel) = 0x1 */
		value = sm_enet_set(value, 1, 5, 5); /*   (tx_fifo_ena) = 0x1 -> SATISH incorrect, should be 5,5 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 2*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 4*2);
		value = sm_enet_set(value, 3, 15, 14); /*   (TX_data_rate) = 0x3 */
		value = sm_enet_set(value, 1, 13, 11); /*   (TX_word_mode) = 0x1 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 4*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 5*2);
		value = sm_enet_set(value, 0, 15, 11); /*   (tx_cn1) = 0x0 */
		value = sm_enet_set(value, 0xf, 10, 5); /*   (tx_cp1) = 0x10 */
		value = sm_enet_set(value, 0, 4, 0); /*   (tx_CN2) = 0x0 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 5*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 6*2);
		value = sm_enet_set(value, 0xf, 10, 7); /*   (txamp_cntl) = 0xf */
		value = sm_enet_set(value, 1, 6, 6); /*   (txamp_ena) = 0x1 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 6*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 7*2);
		value = sm_enet_set(value, 0, 6, 6); /*   (bist_ena_rx) = 0x0 */
		value = sm_enet_set(value, 1, 13, 11); /* (rx_word_mode) = 0x1 */
		value = sm_enet_set(value, 3, 10, 9); /*   (rx_data_rate) = 0x3 --> SATISH incorrect, should be 10:9 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 7*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 8*2);
		value = sm_enet_set(value, 1, 14, 14); /*   (CDR_Loop_ena) = 0x1 */
		value = sm_enet_set(value, 0, 11, 11); /*   (cdr_bypass_rxlos) = 0x0 */
		value = sm_enet_set(value, 0, 9, 9); /*   (SSC_enable) = 0x0 */
		value = sm_enet_set(value, 0, 8, 8); /*   (sd_disabl) = 0x0 */
		value = sm_enet_set(value, 4, 6, 6); /*   (sd_vref) = 0x4 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 8*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 11*2);
		value = sm_enet_set(value, 0, 15, 11); /* (phase_adjust_limit) = 0x0 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 11*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 12*2);
		value = sm_enet_set(value, 1, 13, 13); /*   (Latch_off_ena) = 0x1 */
		value = sm_enet_set(value, 0, 2, 2); /*   (sumos_enable) = 0x0 */
		value = sm_enet_set(value, 0, 1, 1); /*   (rx_det_term_enable) = 0x0 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 12*2, value);

		/* TX Rate Change enable: Toggle 0-1-0 */
		value = enet_sds_ind_csr_reg_rd(priv, CMU +2*16);
		value = sm_enet_set(value, 0, 15, 15);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 15, 15);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 15, 15);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU +2*16);
		value = sm_enet_set(value, 0, 13, 13);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 13, 13);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 13, 13);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU +2*16);
		value = sm_enet_set(value, 0, 11, 11);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 11, 11);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 11, 11);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU +2*16);
		value = sm_enet_set(value, 0, 9, 9);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 9, 9);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value,0, 9, 9);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		/* RX rate change enable: Toggle 0-1-0 */
		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 14, 14);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 14, 14);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 14, 14);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 12, 12);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 12, 12);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 12, 12);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 10, 10);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 10, 10);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 10, 10);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 8, 8);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 1, 8, 8);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*16);
		value = sm_enet_set(value, 0, 8, 8);
		enet_sds_ind_csr_reg_wr(priv, CMU + 2*16, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 26*2);
		value = sm_enet_set(value, 0, 13, 11); /*   (period_error_latch) = 0x0 */
		value = sm_enet_set(value, 1, 3, 3); /*   (blwc_ena) = 0x1 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 26*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 28*2);
		/*   (DFE_tap_ena) = 0x7 -> SATISH incorrect, should be 15:0 */
		value = sm_enet_set(value, 7, 15, 0); 
		enet_sds_ind_csr_reg_wr(priv, inst_base + 28*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 31*2);
		/*   (DFE_preset_H0) = 0x7 -> SATISH incorrect, should be 15:0 */
		/* kchidvil 05/09 */ 
		value = sm_enet_set(value, 0, 15, 0); 
		enet_sds_ind_csr_reg_wr(priv, inst_base + 31*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 61*2);
		value = sm_enet_set(value, 1, 4, 4); /*   (iscan_inbert) = 0x1 */
		value = sm_enet_set(value, 1, 13, 10); /*   (SPD_sel_cdr) = 0x6 */
		value = sm_enet_set(value, 0, 7, 6); /*   (eye_count_width_sel) = 0x0 */
		value = sm_enet_set(value, 0, 3, 3); /*   (LoadFrq_shift) = 0x0 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 61*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 62*2);
		value = sm_enet_set(value, 0, 13, 11); /*   (period_h1_qlatc) = 0x0 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 62*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 81*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 81*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 83*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 83*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 83*2);
		/*value = sm_enet_set(value, , 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 83*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 84*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 84*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 85*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 85*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 86*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe > SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 86*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 87*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 87*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 88*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 88*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 89*2);
		/*value = sm_enet_set(value, 0xe, 15, 0); */
		/*   (mu_dfe1-9 - mu_ph1-9 ) = 0xe -> SATISH incorrect */
		value = sm_enet_set(value, 0xe, 15, 11);
		value = sm_enet_set(value, 0xe, 10, 6);
		value = sm_enet_set(value, 0xe, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 89*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 96*2);
		/*value = sm_enet_set(value, 0x10, 15, 0); */
		/*   (mu_freq1-9 ) = 0x10 -> SATISH incorrect */
		value = sm_enet_set(value, 0x10, 15, 11);
		value = sm_enet_set(value, 0x10, 10, 6);
		value = sm_enet_set(value, 0x10, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 96*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 97*2);
		/*value = sm_enet_set(value, 0x10, 15, 0); */
		/*   (mu_freq1-9 ) = 0x10 -> SATISH incorrect */
		value = sm_enet_set(value, 0x10, 15, 11);
		value = sm_enet_set(value, 0x10, 10, 6);
		value = sm_enet_set(value, 0x10, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 97*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 98*2);
		/*value = sm_enet_set(value, 0x10, 15, 0); */
		/*   (mu_freq1-9 ) = 0x10 -> SATISH incorrect */
		value = sm_enet_set(value, 0x10, 15, 11);
		value = sm_enet_set(value, 0x10, 10, 6);
		value = sm_enet_set(value, 0x10, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 98*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 99*2);
		/*value = sm_enet_set(value, 7, 15, 0); */
		/*   (mu_phase1-9) = 0x7 -> SATISH incorrect */
		value = sm_enet_set(value, 7, 15, 11);
		value = sm_enet_set(value, 7, 10, 6);
		value = sm_enet_set(value, 7, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 99*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 100*2);
		/*value = sm_enet_set(value, 7, 15, 0); */
		/*   (mu_phase1-9) = 0x7 -> SATISH incorrect */
		value = sm_enet_set(value, 7, 15, 11);
		value = sm_enet_set(value, 7, 10, 6);
		value = sm_enet_set(value, 7, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 100*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 101*2);
		/*value = sm_enet_set(value, 7, 15, 0); */
		/*   (mu_phase1-9) = 0x7 -> SATISH incorrect */
		value = sm_enet_set(value, 7, 15, 11);
		value = sm_enet_set(value, 7, 10, 6);
		value = sm_enet_set(value, 7, 5, 1);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 101*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 102*2);
		/*  (freqloop_limit) = 0x2 -> SATISH (incorrect field, should be 6,5) */
		value = sm_enet_set(value, 0, 6, 5);
		enet_sds_ind_csr_reg_wr(priv, inst_base + 102*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 125*2);
		value = sm_enet_set(value, 0x0, 15, 3); /*   (pq_reg) = 0x0 */
		value = sm_enet_set(value, 0x0, 8, 8); /*   (sign_pq) = 0x0  */
		value = sm_enet_set(value, 1, 1, 1); /*   (phz_manual) = 0x1 */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 125*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 127*2);
		/*   (latch_man_cal_ena) = 0x0 -> SATISH (incorrect field 1,1, should be 3,3) */
		value = sm_enet_set(value, 0, 3, 3); 
		enet_sds_ind_csr_reg_wr(priv, inst_base + 127*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 128*2);
		/*   (latch_cal_wait_sel) = 0x2 -> SATISH (incorrect field 1,1 should be 3,2) */
		value = sm_enet_set(value, 2, 3, 2); 
		enet_sds_ind_csr_reg_wr(priv, inst_base + 128*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 145*2);
		value = sm_enet_set(value, 3, 15, 14); /*   (rxdfe_config) = 0x3 */
		/*   (tx_idle_sata) = 0x0 --> SATISH (incorrect comment) */
		value = sm_enet_set(value, 0, 0, 0); 
		enet_sds_ind_csr_reg_wr(priv, inst_base + 145*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 148*2);
		value = sm_enet_set(value, 0xffff, 15, 0); /* (rx_bist_word_cnt_0) = 0xffff */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 148*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 149*2);
		value = sm_enet_set(value, 0xffff, 15, 0); /* (rx_bist_word_cnt_1) = 0xffff */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 149*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 150*2);
		value = sm_enet_set(value, 0xffff, 15, 0); /* (rx_bist_word_cnt_2) = 0xffff */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 150*2, value);

		value = enet_sds_ind_csr_reg_rd(priv, inst_base + 151*2);
		value = sm_enet_set(value, 0xffff, 15, 0); /* (rx_bist_word_cnt_3) = 0xffff */
		enet_sds_ind_csr_reg_wr(priv, inst_base + 151*2, value);
	}
}

/*////////////////////////////////////////////////////////////////////////////// */
/* Serdes programming */
/*////////////////////////////////////////////////////////////////////////////// */
void sm_enet_module_program_all_regs(struct apm_data_priv *priv) {

	enet_sds_CMU_cfg(priv);
	enet_sds_rxtx_cfg(priv);
	return;
}

void apm_enet_serdes_init(struct apm_data_priv *priv)
{
	u32 value;
	u32 refclk, refclksel, refclk_cmos_sel;
#if 0
	u32 rx_clk_inv = 0;
#else
	u32 rx_clk_inv = 1;
#endif
	u32 pll_ready = 0, loop = 1000;
	u32 inst, inst_base;
	u32 pll_lock, vco_calibration;
#ifdef PHY_DEBUG
	u32 tx_ready, rx_ready;
#endif
#if 0
	char *chip_revision = apm88xxx_chip_revision();
	if (!strncmp(chip_revision, "A1", 2))
		rx_clk_inv = 1;
#endif
	/* select Ethernet from bus mux (1=Ethernet     0=SATA) */
	apm_enet_write(priv, BLOCK_SATA_ENET_CSR, SATA_ENET_CONFIG_REG_ADDR, 0x1);
	
	/* module_init_enet_serdes */
	refclk = 0; /* External differential clk */
	refclksel = (refclk >> 1) & 1;
	refclk_cmos_sel = refclk & 1;

	PHY_PRINT("INIT_SERDES : assert all reset\n");
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_RST_CTL_ADDR, 0x20);
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_RST_CTL_ADDR, 0xDE);
	/* set cfg_i_customer_pin_mode = 0,
 	   all register are directly programmed through indirect programming */
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_CTL0_ADDR, 0);

	PHY_PRINT("INIT_SERDES : Config Ref Clock\n");
	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*12);
	value = sm_enet_set(value, 2, 7, 4); /* Txready delay setting */
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*1, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*0); /* CMU_reg0 */
	value = sm_enet_set(value, refclksel, 13, 13);
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*0, value);

	value = enet_sds_ind_csr_reg_rd(priv, CMU + 2*1);
	value = sm_enet_set(value, refclk_cmos_sel, 0, 0);
	enet_sds_ind_csr_reg_wr(priv, CMU + 2*1, value);
	/* This function programs the serdes as per KC_serdes_40nm_setting_0v26.xls */
	sm_enet_module_program_all_regs(priv);

	if (rx_clk_inv) {
		PHY_PRINT("INIT_SERDES : Enable RXCLK Inversion.");
		for (inst = 0; inst < 2;  inst++) {
			PHY_PRINT("INIT_SERDES : RXTX Inst %x\n", inst);
			inst_base = 0x0400 + inst*0x0200;
			value = enet_sds_ind_csr_reg_rd(priv, inst_base + 0xD*2);
			value = sm_enet_set(value, 1, 13, 13);
			enet_sds_ind_csr_reg_wr(priv,  inst_base + 0xD*2, value);
		} 
	}
	/* TODO loopback? */
#ifdef APM_ENET_SERDES_LOOPBACK
	/* rxtx.reg4.tx_loopback_buf_en = 1 */
	value = enet_sds_ind_csr_reg_rd(priv, 0x0400 + 0x4*2);
	value = value | 1 << 6;
	enet_sds_ind_csr_reg_wr(priv, 0x0400 + 0x4*2, value);
	/* rxtx.reg7.loopback_ena_ctle = 1 */
	value = enet_sds_ind_csr_reg_rd(priv, 0x0400 + 0x7*2);
	value = value | 1 << 14;
	enet_sds_ind_csr_reg_wr(priv, 0x0400+ 0x7*2, value);
#endif
	apm_enet_write(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_RST_CTL_ADDR, 0xDF);
	/* Added by Anil - taken from SATA */
	serdes_calib(priv);
	serdes_reset_rxd_rxa(priv, priv->port);
	serdes_reset_rxd_rxa(priv, priv->port + 1);

	/* INIT_SERDES : Check PLL Ready/LOCK and VCO Calibration status */
 	PHY_PRINT("INIT_SERDES : Check PLL Ready/LOCK and VCO Calibration status\n");
	while (pll_ready == 0) {
		udelay(10);
		apm_enet_read(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS_CMU_STATUS0_ADDR, &value);
		pll_ready = CFG_CMU_O_PLL_READY0_RD(value);
		pll_lock = CFG_CMU_O_PLL_LOCK0_RD(value);
		vco_calibration = CFG_CMU_O_VCO_CALDONE0_RD(value);
		if (((pll_ready && pll_lock && vco_calibration)) ||
			(loop -- == 0))
			break;
	}

#ifdef PHY_DEBUG
	PHY_PRINT("INIT_SERDES : PLL is %sREADY\n", pll_ready ? "" : "not ");
	PHY_PRINT("INIT_SERDES : PLL %sLOCKed\n", pll_lock ? "" : "not ");
	//PHY_PRINT("INIT_SERDES : PLL VCO Calibration %s\n", vco_calibration ? "Successful" : "not Successful");

	PHY_PRINT("INIT_SERDES : Check TX/RX Ready\n");
	apm_enet_read(priv, BLOCK_ETH_SDS_CSR, SATA_ENET_SDS0_RXTX_STATUS_ADDR, &value);

	tx_ready = CFG_TX_O_TX_READY_F1_RD(value);
	rx_ready = CFG_RX_O_RX_READY_F1_RD(value);
	PHY_PRINT("INIT_SERDES : TX is %sready\n", tx_ready ? "" : "not ");
	PHY_PRINT("INIT_SERDES : RX is %sready\n", rx_ready ? "" : "not ");
#endif
}

void enet_serdes_deassert(struct apm_data_priv *priv)
{
	u32 data;

	/* Clear Software Reset for Serdes */
	apm_enet_read(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, &data);
	PHY_PRINT("%s read 0x%x <- 0x%llx\n", __func__,
		data, ENET_CLKRST_CSR_BASE_ADDR + ENET_SRST_ADDR);

	data = ENET_SRST_DEFAULT;
	PHY_PRINT("%s write 0x%x -> 0x%llx\n", __func__,
		data & ~ENET_DSEN_ADDR, ENET_CLKRST_CSR_BASE_ADDR + ENET_SRST_ADDR);
	apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, data);
	udelay(2);
}

void enet_reset(struct apm_data_priv *priv)
{
	/* ENET_0 or ENET_2, can reset */
	/* ENET_1 or ENET_3, need the flag std_alone to indicate it is the only interface on ENET controller*/
	if (((priv->std_alone) && ((priv->port == ENET_1) || (priv->port == ENET_3))) ||
		(priv->port == ENET_0) || (priv->port == ENET_2)) {
		ENET_DEBUG("%s port=%d\n", __func__, priv->port);
		apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, 0x1f);
		udelay(1000);
		apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, 0x0);
		udelay(100);
		apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, 0x1f);

		apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR,
			ENET_SDS_RESET_WR(1) |
			CSR0_RESET_WR(1) | ENET0_RESET_WR(1) |
			CSR1_RESET_WR(1) | ENET1_RESET_WR(1));
		udelay(10);
		apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, 0x0);
		udelay(10);
		enet_ecc_init(priv);
		apm_enet_serdes_init(priv);
	}
}

void apm_gmac_pad_crc_enable(struct apm_data_priv *priv, u8 enable)
{
	u32 data;

	/* Read current value of the config 2 register */
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_2_ADDR, &data);

	/* Modify value to set or reset pad and crc check enable bit */
	if (enable)
		data |= PAD_CRC2_MASK;
	else
		data &= ~PAD_CRC2_MASK;

	/* Write value back to config 2 register */
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_2_ADDR, data);
}

void apm_gmac_crc_enable(struct apm_data_priv *priv, u8 enable)
{
	u32 data;

	/* Read current value of the config 2 register */
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_2_ADDR, &data);

	/* Modify value to set or reset crc check enable bit */
	if (enable)
		data |= CRC_EN2_MASK;
	else
		data &= ~CRC_EN2_MASK;

	/* Write value back to config 2 register */
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_2_ADDR, data);
}

void apm_gmac_set_ipg(struct apm_data_priv *priv, u16 ipg)
{
	u32 data;

	/* Read current value of the IPG IFG register */
	apm_enet_read(priv, BLOCK_MCX_MAC, IPG_IFG_ADDR, &data);

	/* Write value back to IPG IFG register */
	apm_enet_write(priv, BLOCK_MCX_MAC, IPG_IFG_ADDR,
			B2B_IPG_SET(data, ipg));

	ENET_DEBUG("Setting IPG to %d bits", ipg);
}

void apm_gmac_change_mtu(struct apm_data_priv *priv, u32 new_mtu)
{
	u32 data;

	/* Read current value of the Maximum Frame Length register */
	apm_enet_read(priv, BLOCK_MCX_MAC, MAX_FRAME_LEN_ADDR, &data);

	/* Write value back to Maximum Frame Length register */
	apm_enet_write(priv, BLOCK_MCX_MAC, MAX_FRAME_LEN_ADDR,
			MAX_FRAME_LEN_SET(data, new_mtu));
}

static int apm_gmac_link_status(struct apm_data_priv *priv)
{
	u32 data, speed;
		
	if (priv->port == MENET) {
		u32 sts, ctl, anar, lpar, anlpar;
		u32 ganar = 0, glpar = 0;
		/* Link status is latched in PHY so read it twice */
		sts = 0x0;
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_STAT_REG, &sts);
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_STAT_REG, &sts);
		PHY_PRINT("MII_STAT_REG: 0x%x 0x%x\n", sts, (sts & MII_SR_LINK_STATUS));
		priv->link_status = (sts & MII_SR_LINK_STATUS) ? 1 : 0;

		/* If no link then return SPEED_0 */
		if (!(sts & MII_SR_LINK_STATUS)) {
			speed = SPEED_0;
			return 0;
		}

		apm_genericmiiphy_read(priv, priv->phy_addr, MII_CTRL_REG, &ctl);
		PHY_PRINT("MII_CTRL_REG: 0x%x \n", ctl);
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_AN_ADS_REG, &anar);
		PHY_PRINT("MII_AN_ADS_REG: 0x%x \n", anar);
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_AN_PRTN_REG, &lpar);
		PHY_PRINT("MII_AN_PRTN_REG: 0x%x \n", lpar);
		if (sts & MII_SR_EXT_STS) {
			apm_genericmiiphy_read(priv, priv->phy_addr, MII_MASSLA_CTRL_REG, &ganar);
			PHY_PRINT("MII_MASSLA_CTRL_REG: 0x%x \n", ganar);
			apm_genericmiiphy_read(priv, priv->phy_addr, MII_MASSLA_STAT_REG, &glpar);
			PHY_PRINT("MII_MASSLA_STAT_REG: 0x%x \n", glpar);
		}

		/*
		 * If autoneg is on, figure out the link speed from the
		 * advertisement and partner ability registers. If autoneg is
		 * off, use the settings in the control register.
		 */
		speed = SPEED_0;
		if (ctl & MII_CR_AUTO_EN) {
			anlpar = anar & lpar;
			if ((ganar & MII_MASSLA_CTRL_1000T_FD) &&
					(glpar & MII_MASSLA_STAT_LP1000T_FD)) {
				speed = SPEED_1000;
			} else if ((ganar & MII_MASSLA_CTRL_1000T_HD) &&
					(glpar & MII_MASSLA_STAT_LP1000T_HD)) {
				speed = SPEED_1000;
			} else if (anlpar & MII_ANAR_100TX_FD) {
				speed = SPEED_100;
			} else if (anlpar & MII_ANAR_100TX_HD) {
				speed = SPEED_100;
			} else if (anlpar & MII_ANAR_10TX_FD) {
				speed = SPEED_10;
			} else if (anlpar & MII_ANAR_10TX_HD) {
				speed = SPEED_10;
			}
		} else {
			if ((ctl & (MII_CR_100 | MII_CR_1000)) ==
					(MII_CR_100 | MII_CR_1000)) {
				speed = SPEED_1000;
			} else if (ctl & MII_CR_100) {
				speed = SPEED_100;
			} else {
				speed = SPEED_10;
			}
		}
	} else {
		apm_enet_read(priv, BLOCK_ETH_CSR, LINK_STATUS_ADDR, &data);
		if ((priv->port == ENET_0) || (priv->port == ENET_2))
			priv->link_status = PORT0_F6_RD(data);
		else
			priv->link_status = PORT1_F6_RD(data);
		if (!priv->link_status) {
			priv->speed = SPEED_10;
			PHY_PRINT("Port%d is down\n", priv->port);
			return 0;
		}

		/* Get the final speed information from SGMII */
		apm_genericmiiphy_read(priv, INT_PHY_ADDR,
			SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR >> 2, &data);
		PHY_PRINT("SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR data: 0x%x\n", data);
		speed = LINK_SPEED_F1_RD(data);
	}
	switch(speed) {
	case PHY_SPEED_10:
		speed = SPEED_10;
		break;
	case PHY_SPEED_100:
		speed = SPEED_100;
		break;
	default:
		speed = SPEED_1000;
		break;
	}
	if (priv->link_status)
		ENET_PRINT("Phy Speed is :%d \n", speed);
	else 
		ENET_PRINT("Port%d is down\n", priv->port);
	priv->speed = speed;
	return 1;
}

void apm_gmac_phy_link_mode(struct apm_data_priv *priv, u32 *speed, u32 *state)
{
#ifndef BYPASS_GMAC_PHY
	u32 sts, ctl, anar, lpar, anlpar;
	u32 ganar = 0, glpar = 0;

	if (priv->phy_mode == PHY_MODE_SGMII) {
#if 1
		u32 data;

		apm_genericmiiphy_read(priv, INT_PHY_ADDR,
			SGMII_STATUS_ADDR >> 2, &data);
		if (LINK_STATUS_RD(data) == 0) {/* No link wih SGMII */
			PHY_PRINT("No link. RTL_BMSR_ADR data: 0x%x\n", data);
			*state = 0;
			*speed = priv->speed = SPEED_10;
			return;
		} 
		/* Get the final speed information from SGMII */
		apm_genericmiiphy_read(priv, INT_PHY_ADDR,
			SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR >> 2, &data);
		PHY_PRINT("Linkup. SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR"
				" data: 0x%x\n", data);
		*state = 1;
		//speed = LINK_SPEED_F1_RD(data);
		switch(LINK_SPEED_F1_RD(data)) {
		case PHY_SPEED_10:
			priv->speed = SPEED_10;
			break;
		case PHY_SPEED_100:
			priv->speed = SPEED_100;
			break;
		default:
			priv->speed = SPEED_1000;
			break;
		}
		*speed =  priv->speed;
		PHY_PRINT("Phy Speed is :%d \n", priv->speed);
		return;
#else
		apm_get_link_speed(priv);
#endif
		return;
	}

	/* Link status is latched in PHY so read it twice */
	sts = 0x0;
	apm_genericmiiphy_read(priv, priv->phy_addr, MII_STAT_REG, &sts);
	apm_genericmiiphy_read(priv, priv->phy_addr, MII_STAT_REG, &sts);
	PHY_PRINT("MII_STAT_REG: 0x%x 0x%x\n", sts, (sts & MII_SR_LINK_STATUS));
	*state = (sts & MII_SR_LINK_STATUS) ? 1 : 0;

	/* If no link then return SPEED_0 */
	if (!(sts & MII_SR_LINK_STATUS)) {
		*speed = SPEED_0;
		return;
	}

	apm_genericmiiphy_read(priv, priv->phy_addr, MII_CTRL_REG, &ctl);
	PHY_PRINT("MII_CTRL_REG: 0x%x \n", ctl);
	apm_genericmiiphy_read(priv, priv->phy_addr, MII_AN_ADS_REG, &anar);
	PHY_PRINT("MII_AN_ADS_REG: 0x%x \n", anar);
	apm_genericmiiphy_read(priv, priv->phy_addr, MII_AN_PRTN_REG, &lpar);
	PHY_PRINT("MII_AN_PRTN_REG: 0x%x \n", lpar);
	if (sts & MII_SR_EXT_STS) {
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_MASSLA_CTRL_REG, &ganar);
		PHY_PRINT("MII_MASSLA_CTRL_REG: 0x%x \n", ganar);
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_MASSLA_STAT_REG, &glpar);
		PHY_PRINT("MII_MASSLA_STAT_REG: 0x%x \n", glpar);
	}

	/*
	 * If autoneg is on, figure out the link speed from the
	 * advertisement and partner ability registers. If autoneg is
	 * off, use the settings in the control register.
	 */
	*speed = SPEED_0;
	if (ctl & MII_CR_AUTO_EN) {
		anlpar = anar & lpar;
		if ((ganar & MII_MASSLA_CTRL_1000T_FD) &&
		    (glpar & MII_MASSLA_STAT_LP1000T_FD)) {
			*speed = SPEED_1000;
		} else if ((ganar & MII_MASSLA_CTRL_1000T_HD) &&
			   (glpar & MII_MASSLA_STAT_LP1000T_HD)) {
			*speed = SPEED_1000;
		} else if (anlpar & MII_ANAR_100TX_FD) {
			*speed = SPEED_100;
		} else if (anlpar & MII_ANAR_100TX_HD) {
			*speed = SPEED_100;
		} else if (anlpar & MII_ANAR_10TX_FD) {
			*speed = SPEED_10;
		} else if (anlpar & MII_ANAR_10TX_HD) {
			*speed = SPEED_10;
		}
	} else {
		if ((ctl & (MII_CR_100 | MII_CR_1000)) ==
					(MII_CR_100 | MII_CR_1000)) {
			*speed = SPEED_1000;
		} else if (ctl & MII_CR_100) {
			*speed = SPEED_100;
		} else {
			*speed = SPEED_10;
		}
	}
#else
	*state = 1;
	*speed = SPEED_1000;
#endif
}

int apm_gmac_phy_enable_scan_cycle(struct apm_data_priv *priv, int enable)
{
	u32 val;

	/* Enable scan cycle command for link scanning in MAC */
	apm_enet_read(priv, BLOCK_MCX_MAC, MII_MGMT_COMMAND_ADDR, &val);
	if (enable)
		val |= SCAN_CYCLE_MASK;
	else
		val &= ~SCAN_CYCLE_MASK;
	apm_enet_write(priv, BLOCK_MCX_MAC, MII_MGMT_COMMAND_ADDR, val);

	/* Program phy address start scan from 0 and register at address 0x1 */
	apm_enet_read(priv, BLOCK_MCX_MAC, MII_MGMT_ADDRESS_ADDR, &val);
	val = PHY_ADDR_SET(val, 0);
	val = REG_ADDR_SET(val, 1);
	apm_enet_write(priv, BLOCK_MCX_MAC, MII_MGMT_ADDRESS_ADDR, val);
	return 0;
}

void apm_gmac_reset(struct apm_data_priv *priv)
{
	u32 value;
#if defined(CONFIG_APM862xx)
	u32 mcx_mac_addr_v = priv->mcx_mac_addr_v;
	static u32 apm_enet0_mac_init_done = 0;
#endif
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &value);
	if (!(value & SOFT_RESET1_MASK)) {
		return;
	}

#if defined(CONFIG_APM862xx)
	if (priv->port && !apm_enet0_mac_init_done) {
		priv->mcx_mac_addr_v = priv->mac_mii_addr_v;
		apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &value);
		if (!(value & SOFT_RESET1_MASK)) {
			priv->mcx_mac_addr_v = mcx_mac_addr_v;
			apm_enet0_mac_init_done = 1;
		}
	}

_reset_gmac_access_phy:
#endif
	/* Reset MAC subsystem */
	value = RESET_TX_FUN1_WR(1) |
			RESET_RX_FUN1_WR(1) |
			RESET_TX_MC1_WR(1)  |
			RESET_RX_MC1_WR(1)  |
			SIM_RESET1_WR(1)    |
			SOFT_RESET1_WR(1);

	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, value);
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &value);
	udelay(100);
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, 0);

#if defined(CONFIG_APM862xx)
	if (priv->port && !apm_enet0_mac_init_done) {
		priv->mcx_mac_addr_v = mcx_mac_addr_v;
		apm_enet0_mac_init_done = 1;
		goto _reset_gmac_access_phy;
	}
#endif
}

int apm_gmac_phy_autoneg_done(struct apm_data_priv *priv)
{
#ifndef BYPASS_GMAC_PHY
	u32 sts, sts1;
	u32 phy_addr = priv->phy_addr;	/* Always external PHY */

read_sts_again:
	apm_genericmiiphy_read(priv, phy_addr,
		SGMII_STATUS_ADDR >> 2, &sts);
	apm_genericmiiphy_read(priv, phy_addr,
		SGMII_STATUS_ADDR >> 2, &sts1);
	if (sts != sts1) {
		goto read_sts_again;
	}
	return (sts & AUTO_NEGOTIATION_COMPLETE_MASK) ? 1 : 0;
#else
	return 1;
#endif
}

void apm_gmac_phy_init(struct apm_data_priv *priv)
{
	int retry;
	u32 data, sts;
	u32 anar, ganar, ctl;

	/* Get PHY status */
	sts = 0x0;
	apm_genericmiiphy_read(priv, priv->phy_addr, MII_STAT_REG, &sts);

	/* Set autoneg advertisement to advertise all modes. */
	anar = MII_ANAR_100TX_HD|MII_ANAR_100TX_FD;
	if (sts & MII_SR_EXT_STS)
		ganar = MII_MASSLA_CTRL_1000T_FD|MII_MASSLA_CTRL_1000T_HD;
	ctl = MII_CR_AUTO_EN|MII_CR_RESTART;

	/* Power-down PHY */
	data = MII_CR_POWER_DOWN;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_CTRL_REG, data);

	/* Power-up PHY */
	data = 0x0;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_CTRL_REG, data);

	/* Reset PHY */
	data = MII_CR_RESET;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_CTRL_REG, data);

	/* Wait till PHY reset completes */
	retry = 1000;
	while ((data & MII_CR_RESET) && (retry > 0)) {
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_CTRL_REG, &data);
		retry--;
		PHY_CSR_POLL_DELAY
	}

	/* If the extended capabilities bit is set, this is a gigE
	 * PHY, so make sure we advertise gigE modes.
	 */
	if (sts & MII_SR_EXT_STS) {
		/* Enable advertisement of gigE modes. */
		data = MII_MASSLA_CTRL_1000T_FD|MII_MASSLA_CTRL_1000T_HD;
		apm_genericmiiphy_write(priv, priv->phy_addr, MII_MASSLA_CTRL_REG, data);
	}

	/* Enable auto-neg and restart PHY */
	data = MII_CR_AUTO_EN|MII_CR_RESTART;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_CTRL_REG, data);

	apm_genericmiiphy_read(priv, priv->phy_addr, MII_AN_ADS_REG, &data);
	data &= ~(MII_ANAR_10TX_HD|MII_ANAR_10TX_FD|
			MII_ANAR_100TX_HD|MII_ANAR_100TX_FD);
	data |= anar;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_AN_ADS_REG, data);

	if (sts & MII_SR_EXT_STS) {
		apm_genericmiiphy_read(priv, priv->phy_addr, MII_MASSLA_CTRL_REG, &data);
		data &= ~(MII_MASSLA_CTRL_1000T_HD|MII_MASSLA_CTRL_1000T_FD);
		data |= ganar;
		apm_genericmiiphy_write(priv, priv->phy_addr, MII_MASSLA_CTRL_REG, data);
	}

	apm_genericmiiphy_read(priv, priv->phy_addr, MII_CTRL_REG, &data);
	data &= ~(MII_CR_FDX|MII_CR_100|MII_CR_AUTO_EN|MII_CR_RESTART);
	data |= ctl;
	apm_genericmiiphy_write(priv, priv->phy_addr, MII_CTRL_REG, data);
}

static void module_enet_config_sgmii_autoneg(struct apm_data_priv *priv, int autoneg)
{
	u32 data, speed, times = 0;

	/*
	 * All internal PHY addresses are shifted by 2 bits
	 */
reset_phy:
	PHY_PRINT("%s autoneg=%d\n", __func__, autoneg);
	 /* Bring the SGMII core out of reset, non-autoneg mode */
	apm_genericmiiphy_write(priv, INT_PHY_ADDR,
		SGMII_TBI_CONTROL_ADDR >> 2, 0x0);
	if (autoneg) {
		/* Reset PHY */
		PHY_PRINT(" %s Reset PHY\n", __func__);
		apm_genericmiiphy_write(priv, INT_PHY_ADDR,
			SGMII_CONTROL_ADDR >> 2, 0x9140);
		udelay(2);
		/* Bring PHY out of reset; Enable An, autonegotiation */
		PHY_PRINT(" Bring PHY out of reset; Enable An\n");
		apm_genericmiiphy_write(priv, INT_PHY_ADDR,
			SGMII_CONTROL_ADDR >> 2, 0x1140);
	}
	else {
		/* Reset PHY */
		PHY_PRINT(" %s Reset PHY\n", __func__);
		apm_genericmiiphy_write(priv, INT_PHY_ADDR,
			SGMII_CONTROL_ADDR >> 2, 0x8000);
		udelay(2);
		/* Bring PHY out of reset; No autonegotiation */
		PHY_PRINT(" Bring PHY out of reset; NO An\n");
		apm_genericmiiphy_write(priv, INT_PHY_ADDR,
		                      SGMII_CONTROL_ADDR >> 2, 0x0);
	}
	udelay(1000);
	if (autoneg) {
		int loop = 50;

		/* Check autonegotiation status */
		apm_genericmiiphy_read(priv, INT_PHY_ADDR,
				SGMII_STATUS_ADDR >> 2, &data);
		while (AUTO_NEGOTIATION_COMPLETE_RD(data) == 0 ||
				LINK_STATUS_RD(data) == 0) {
			apm_genericmiiphy_read(priv, INT_PHY_ADDR,
				SGMII_STATUS_ADDR >> 2, &data);
			if (loop-- == 0)
				break;
			udelay(100);
		}
		if (LINK_STATUS_RD(data) == 0) {
			if (times ++ < 5) {
				apm_genericmiiphy_write(priv, INT_PHY_ADDR,
					SGMII_TBI_CONTROL_ADDR >> 2, 0x8000);
				serdes_reset_rxd_rxa(priv, priv->port);
				PHY_PRINT(".");
				goto reset_phy;
			}
		}
	} else {
		PHY_PRINT(" %s non-autoneg NOT supported\n", __func__);
	}

	apm_enet_read(priv, BLOCK_ETH_CSR, LINK_STATUS_ADDR, &data);
	if ((priv->port == ENET_0) ||  (priv->port == ENET_2))
		priv->link_status = PORT0_F6_RD(data);
	else
		priv->link_status = PORT1_F6_RD(data);

	/* Get the final speed information from SGMII */
	apm_genericmiiphy_read(priv, INT_PHY_ADDR,
		SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR >> 2, &data);
	PHY_PRINT("SGMII_AN_SGMII_PARTNER_BASE_PAGE_ABILITY_ADDR data: 0x%x\n", data);
	speed = LINK_SPEED_F1_RD(data);
	switch(speed) {
	case PHY_SPEED_10:
		speed = SPEED_10;
		break;
	case PHY_SPEED_100:
		speed = SPEED_100;
		break;
	default:
		speed = SPEED_1000;
		break;
	}
	if (priv->link_status)
		PHY_PRINT("Phy Speed is :%d \n", speed);
	else 
		PHY_PRINT("Port%d is down\n", priv->port);
	priv->speed = speed;
}

int apm_gmac_init(struct apm_data_priv *priv,
		unsigned char *dev_addr, int speed, int mtu, int crc)
{
	u32 value;
	u32 temp;
	u32 addr_hi;
	u32 addr_lo;

	u32 interface_control;
	u32 mac_config_2;
	u32 rgmii;
	u32 icm_config0 = 0x0008503f;
	u32 icm_config2 = 0x0010000f;
#if !defined(CONFIG_APM862xx)
	u32 ecm_config0 = 0x00000032;
#endif
	u32 enet_spare_cfg = 0x00006040;
	u8 port = priv->port;

	PHY_PRINT("apm_gmac_init phy_mode=0x%x, phyid=0x%x port=0x%x\n",
		priv->phy_mode, priv->phy_addr, priv->port);
#if 0	/* TODO */
	if (priv->phy_mode != PHY_MODE_RGMII)
		goto _skip_rgmii_clk_init;

	PHY_PRINT("Setting Port%d Link Speed %d Mbps \n", port, speed);
	apm86xxx_read_scu_reg(SCU_SOCDIV4_ADDR, &value);

	switch(speed) {
	case SPEED_10:
#if defined(CONFIG_APM862xx)
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0xC8);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0xC8);
		}
#else
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0x190);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0x190);
		}
#endif
		break;

	case SPEED_100:
#if defined(CONFIG_APM862xx)
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0x14);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0x14);
		}
#else
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0x28);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0x28);
		}
#endif
		break;

	default:
#if defined(CONFIG_APM862xx)
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0x04);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0x04);
		}
#else
		if (port == 0) {
			value = RGMII0_CLK_FREQ_SEL4_SET(value, 0x08);
		} else {
			value = RGMII1_CLK_FREQ_SEL4_SET(value, 0x08);
		}
#endif
	}

	apm86xxx_write_scu_reg(SCU_SOCDIV4_ADDR, value);
_skip_rgmii_clk_init:
#endif
	/* Reset subsystem */
	value = RESET_TX_FUN1_WR(1) |
		RESET_RX_FUN1_WR(1) |
		RESET_TX_MC1_WR(1)  |
		RESET_RX_MC1_WR(1)  |
		SIM_RESET1_WR(1)    |
		SOFT_RESET1_WR(1);

	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, value);
	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &temp);
	udelay(100);
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, 0);
	udelay(100);

	/* Initialize the MAC configuration #1 register */
	value = TX_EN1_WR(1)	  |
		RX_EN1_WR(1)	  |
		TX_FLOW_EN1_WR(0) |
#ifdef APM_ENET_MAC_LOOPBACK
		LOOP_BACK1_WR(1)  |
#else
		LOOP_BACK1_WR(0)  |
#endif
		RX_FLOW_EN1_WR(0);
	/* Need this? rd_phy_reg_per_port(port_id,0x1e,M_SGMII_SGMII_TBI_CONTROL__ADDR,&data); */
	ENET_DEBUG ("MCX GMAC config_1: %x\n", value);
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, value);

	/* Reset SGMII Port  */
	if (priv->phy_mode == PHY_MODE_SGMII) {
#ifndef APM_ENET_MAC_LOOPBACK

#ifdef APM_ENET_SERDES_LOOPBACK
		module_enet_config_sgmii_autoneg(priv, 0);
#else
		if (priv->autoneg_set) {
			PHY_PRINT(" %s Enabling autoneg\n", __FUNCTION__);
			module_enet_config_sgmii_autoneg(priv, 1);
		} else {
			PHY_PRINT(" %s Disabling autoneg\n", __FUNCTION__);
			module_enet_config_sgmii_autoneg(priv, 0);
		}
#endif
#endif
	}

	apm_enet_read(priv, BLOCK_ETH_CSR,
			ENET_SPARE_CFG_REG_ADDR, &enet_spare_cfg);

	if (speed == SPEED_10) {
		interface_control = ENET_LHD_MODE_WR(0) |
			ENET_GHD_MODE_WR(0);
		mac_config_2 = FULL_DUPLEX2_WR(1)  |
			LENGTH_CHECK2_WR(0)        |
			HUGE_FRAME_EN2_WR(0)       |
			ENET_INTERFACE_MODE2_WR(1)      | /* 10Mbps */
			PAD_CRC2_WR(crc)           |
			CRC_EN2_WR(crc)            |
			PREAMBLE_LENGTH2_WR(7);
		rgmii = 0;
		icm_config0 = 0x0000503f;
		icm_config2 = 0x000101f4;
#if !defined(CONFIG_APM862xx)
		ecm_config0 = 0x600032;
#endif
#if defined(CONFIG_APM862xx)
		if (port == 0)
			enet_spare_cfg = enet_spare_cfg | (0x0000c040);
		else
			enet_spare_cfg = enet_spare_cfg | (0x00030040);
#endif
	} else if (speed == SPEED_100) {
		interface_control = ENET_LHD_MODE_WR(1);
		mac_config_2 = FULL_DUPLEX2_WR(1)  |
			LENGTH_CHECK2_WR(0)        |
			HUGE_FRAME_EN2_WR(0)       |
			ENET_INTERFACE_MODE2_WR(1)      | /* 100Mbps */
			PAD_CRC2_WR(crc)           |
			CRC_EN2_WR(crc)            |
			PREAMBLE_LENGTH2_WR(7);
		rgmii = 0;
		icm_config0 = 0x0004503f;
		icm_config2 = 0x00010050;
#if !defined(CONFIG_APM862xx)
		ecm_config0 = 0x600032;
#endif
#if defined(CONFIG_APM862xx)
		if (port == 0)
			enet_spare_cfg = enet_spare_cfg | (0x0000c040);
		else
			enet_spare_cfg = enet_spare_cfg | (0x00030040);
#endif
	} else {
		interface_control = ENET_GHD_MODE_WR(1);
		mac_config_2 = FULL_DUPLEX2_WR(1)  |
			LENGTH_CHECK2_WR(0)        |
			HUGE_FRAME_EN2_WR(0)       |
			ENET_INTERFACE_MODE2_WR(2)      | /* 1Gbps */
			PAD_CRC2_WR(crc)           |
			CRC_EN2_WR(crc)            |
			PREAMBLE_LENGTH2_WR(7);
		rgmii = CFG_SPEED_1250_WR(1) | CFG_TXCLK_MUXSEL0_WR(4);
		icm_config0 = 0x0008503f;
		icm_config2 = 0x0001000f;
		ecm_config0 = 0x32;
		if ((port % 2) == 0)
			enet_spare_cfg = (enet_spare_cfg & ~0x0000c000) | (0x00000040);
		else
			enet_spare_cfg = (enet_spare_cfg & ~0x00030000) | (0x00000040);
	}

	PHY_PRINT("\n apm_gmac_init writing speed %d \n", speed);
	PHY_PRINT("\n apm_gmac_init writing rgmii_config_0 0x%x \n", rgmii);
	PHY_PRINT("\n apm_gmac_init writing icm_config0 0x%x \n", icm_config0);
	PHY_PRINT("\n apm_gmac_init writing icm_config2 0x%x \n", icm_config2);
	PHY_PRINT("\n apm_gmac_init writing ecm_config0 0x%x \n", ecm_config0);
	PHY_PRINT("\n apm_gmac_init writing mac_config2 0x%x \n", mac_config_2);
	PHY_PRINT("\n apm_gmac_init writing interface control 0x%x \n", interface_control);

	enet_spare_cfg |= 0x00006040;

	/* Initialize the MAC configuration #2 register */
	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_2_ADDR, mac_config_2);

	/* Initialize the Interface Control Register */
	//interface_control = 0x74521808;
	apm_enet_write(priv, BLOCK_MCX_MAC, INTERFACE_CONTROL_ADDR,
			interface_control);

	/* Initialize the Maximum Frame Length register */
	value = MAX_FRAME_LEN_WR(0x0600);
	apm_enet_write(priv, BLOCK_MCX_MAC, MAX_FRAME_LEN_ADDR, value);

	/* Program the station MAC address */
	addr_hi = *(u32 *) &dev_addr[0];
	addr_lo = *(u16 *) &dev_addr[4];
	addr_lo <<= 16;

	if (priv->phy_mode == PHY_MODE_SGMII)
		      addr_lo = (addr_lo & 0xFFFF0100) | 0x1e;
	else
		      addr_lo |= (priv->phy_addr & 0xFFFF);

	ENET_DEBUG("MAC addr hi: %x\n", addr_hi);
	apm_enet_write(priv, BLOCK_MCX_MAC, STATION_ADDR0_ADDR, addr_hi);
	ENET_DEBUG("MAC addr lo: %x\n", addr_lo);
	apm_enet_write(priv, BLOCK_MCX_MAC, STATION_ADDR1_ADDR, addr_lo);

	ENET_DEBUG("apm_gmac_init TODO Configuring MISC regs for port%d\n", port);

	/* Adjust MDC clock frequency */
	apm_enet_read(priv, BLOCK_MCX_MAC, MII_MGMT_CONFIG_ADDR, &value);
	value = MGMT_CLOCK_SEL_SET(value, 7);
	apm_enet_write(priv, BLOCK_MCX_MAC, MII_MGMT_CONFIG_ADDR, value);

	/* Enable drop if FP not available */
	apm_enet_read(priv, BLOCK_ETH_CSR, RSIF_CONFIG_REG_ADDR, &value);
	value |= CFG_RSIF_FPBUFF_TIMEOUT_EN_WR(1);
	apm_enet_write(priv, BLOCK_ETH_CSR, RSIF_CONFIG_REG_ADDR, value);

	if ((port % 2) == 0) { 	/* For port0 and port2 */
		/* Rtype should be copied from FP */
		value = 0;
		apm_enet_write(priv, BLOCK_ETH_CSR,
				RSIF_RAM_DBG_REG0_ADDR, value);
#if defined(CONFIG_TUNING_TX_RX_DELAY_PORT0)
		ENET_DEBUG("rgmii = %08x\n", rgmii);
		rgmii |= CFG_TXCLK_MUXSEL0_SET(rgmii, txclk0_muxsel);
		rgmii |= CFG_RXCLK_MUXSEL0_SET(rgmii, rxclk0_muxsel);
		ENET_DEBUG("txclk_muxsel=%d, rxclk_muxsel=%d, rgmii=%08x\n",
				txclk0_muxsel, rxclk0_muxsel, rgmii);
#endif
	 	/* Initialize RGMII PHY */
		if (priv->phy_mode == PHY_MODE_RGMII)
			apm_enet_write(priv, BLOCK_ETH_CSR, RGMII_REG_0_ADDR, rgmii);

		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ICM_CONFIG0_REG_0_ADDR, icm_config0);

		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ICM_CONFIG2_REG_0_ADDR, icm_config2);

		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ECM_CONFIG0_REG_0_ADDR, ecm_config0);
		apm_enet_write(priv, BLOCK_ETH_CSR,
			ENET_SPARE_CFG_REG_ADDR, enet_spare_cfg);

		/* Rx-Tx traffic resume */
		apm_enet_write(priv, BLOCK_ETH_CSR,
				CFG_LINK_AGGR_RESUME_0_ADDR, TX_PORT0_WR(0x1));

	} else if ((port % 2) == 1) { 	/* For port1 and port3 */
		/* Rtype should be copied from FP */
		value = 0;
		apm_enet_write(priv, BLOCK_ETH_CSR,
				RSIF_RAM_DBG_REG0_ADDR, value);

#if 0	/* STORM TODO? */
		/* Enable HdrParser Timeout */
		value = CFG_PRS_EN_TIMEOUT0_WR(1) |
			CFG_PRS_MAX_HDR_SIZE0_WR(0x80);
		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				HDRPRS_CONFIG3_REG_1_ADDR, value);
#endif
		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ICM_CONFIG0_REG_1_ADDR, icm_config0);

		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ICM_CONFIG2_REG_1_ADDR, icm_config2);

#if !defined(CONFIG_APM862xx)
		apm_enet_write(priv, BLOCK_MCX_MAC_CSR,
				ECM_CONFIG0_REG_1_ADDR, ecm_config0);
#endif

		apm_enet_write(priv, BLOCK_ETH_CSR,
				ENET_SPARE_CFG_REG_ADDR, enet_spare_cfg);

		/* TCP MSS  */
		apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR, &value);

		apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR,
				CFG_TSIF_MSS_SZ10_SET(value, DEFAULT_TCP_MSS));

		apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR, &value);

		/* Rx-Tx traffic resume */
		apm_enet_write(priv, BLOCK_ETH_CSR,
				CFG_LINK_AGGR_RESUME_1_ADDR, TX_PORT1_WR(0x1));
	}

	if (speed != SPEED_10 && speed != SPEED_100) {
		apm_enet_read(priv, BLOCK_ETH_CSR, DEBUG_REG_ADDR, &value);
		value |= CFG_BYPASS_UNISEC_TX_WR(1) |
			CFG_BYPASS_UNISEC_RX_WR(1);
		apm_enet_write(priv, BLOCK_ETH_CSR, DEBUG_REG_ADDR, value);
	}

#ifdef PCM_LOOPBACK
	apm_enet_read(priv, BLOCK_ETH_CSR, DEBUG_REG_ADDR, &value);
	value |= CFG_DEBUG_RXTSOTXBUF_LOOPBACK_WR(1);
	apm_enet_write(priv, BLOCK_ETH_CSR, DEBUG_REG_ADDR, value);
	apm_gmac_crc_enable(priv, 0);
	apm_gmac_pad_crc_enable(priv, 0);
#endif

	/* Rx-Tx DV Gate Off */
	if ((port % 2) == 0) {    /* For port0 and port2 */
		apm_enet_read(priv, BLOCK_MCX_MAC_CSR, RX_DV_GATE_REG_0_ADDR, &value);
		value = TX_DV_GATE_EN0_SET(value, 0);
		value = RX_DV_GATE_EN0_SET(value, 0);
		value = RESUME_RX0_SET(value, 1);
		apm_enet_write(priv, BLOCK_MCX_MAC_CSR, RX_DV_GATE_REG_0_ADDR, value);

		//apm_enet_read(priv, BLOCK_ETH_CSR, CFG_LPBK_GATE_TX_ADDR, &value);
	 	//value = PORT0_F7_SET(value, 0);
		//apm_enet_write(priv, BLOCK_ETH_CSR, CFG_LPBK_GATE_TX_ADDR, value);
	} else {                 /* For port1 and port3 */
		apm_enet_read(priv, BLOCK_MCX_MAC_CSR, RX_DV_GATE_REG_1_ADDR, &value);
		value = TX_DV_GATE_EN1_SET(value, 0);
		value = RX_DV_GATE_EN1_SET(value, 0);
		value = RESUME_RX1_SET(value, 1);
		apm_enet_write(priv, BLOCK_MCX_MAC_CSR, RX_DV_GATE_REG_1_ADDR, value);
	}
	ENET_DEBUG("Bypass traffic gating\n");
	apm_enet_write(priv, BLOCK_ETH_CSR, CFG_BYPASS_ADDR, RESUME_TX_WR(1));
#if 0
	udelay(1000);
	apm_enet_read(priv, BLOCK_MCX_STATS, RFCS_ADDR, &value);
	if (value) {
		module_enet_config_sgmii_autoneg(priv, 1);
		ENET_DEBUG("RFCS serdes reset\n");

	}
#endif
	return 0;
}

/* Start Statistics related functions */

void apm_gmac_get_detailed_stats(struct apm_data_priv *priv,
					struct eth_detailed_stats *stats)
{
	apm_gmac_get_tx_rx_stats(priv, &(stats->eth_combined_stats));
	apm_gmac_get_rx_stats(priv, &(stats->rx_stats));
	apm_gmac_get_tx_stats(priv, &(stats->tx_stats));
}

void apm_gmac_get_tx_rx_stats(struct apm_data_priv *priv,
					struct eth_frame_stats *tx_rx_stats)
{
	/* Read Stats */
	apm_enet_read(priv, BLOCK_MCX_STATS, TR64_ADDR,
				 &tx_rx_stats->c_64B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TR127_ADDR,
				 &tx_rx_stats->c_65_127B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TR255_ADDR,
				 &tx_rx_stats->c_128_255B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TR511_ADDR,
				 &tx_rx_stats->c_256_511B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TR1K_ADDR,
				 &tx_rx_stats->c_512_1023B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TRMAX_ADDR,
				 &tx_rx_stats->c_1024_1518B_frames);
	apm_enet_read(priv, BLOCK_MCX_STATS, TRMGV_ADDR,
				 &tx_rx_stats->c_1519_1522B_frames);

	/* Mask out unnecessary bits in all the fields */
	tx_rx_stats->c_64B_frames &= TX_RX_64B_FRAME_CNTR4_MASK;
	tx_rx_stats->c_65_127B_frames &= TX_RX_127B_FRAME_CNTR7_MASK;
	tx_rx_stats->c_128_255B_frames &= TX_RX_255B_FRAME_CNTR5_MASK;
	tx_rx_stats->c_256_511B_frames &= TX_RX_511B_FRAME_CNTR1_MASK;
	tx_rx_stats->c_512_1023B_frames &= TX_RX_1KB_FRAME_CNTR_MASK;
	tx_rx_stats->c_1024_1518B_frames &= TX_RX_MAXB_FRAME_CNTR_MASK;
	tx_rx_stats->c_1519_1522B_frames &= TRMGV_MASK;
}

void apm_gmac_get_rx_stats(struct apm_data_priv *priv, struct eth_rx_stat *rx_stat)
{
	/* Read Stats */
	apm_enet_read(priv, BLOCK_MCX_STATS, RBYT_ADDR,
				 &rx_stat->rx_byte_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RPKT_ADDR,
				 &rx_stat->rx_packet_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RFCS_ADDR,
				 &rx_stat->rx_fcs_err_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RMCA_ADDR,
				 &rx_stat->rx_multicast_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RBCA_ADDR,
				 &rx_stat->rx_broadcast_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RXCF_ADDR,
				 &rx_stat->rx_cntrl_frame_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RXPF_ADDR,
				 &rx_stat->rx_pause_frame_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RXUO_ADDR,
				 &rx_stat->rx_unknown_op_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RALN_ADDR,
				 &rx_stat->rx_alignment_err_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RFLR_ADDR,
				 &rx_stat->rx_frm_len_err_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RCDE_ADDR,
				 &rx_stat->rx_code_err_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RCSE_ADDR,
				 &rx_stat->rx_carrier_sense_err_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RUND_ADDR,
				 &rx_stat->rx_undersize_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, ROVR_ADDR,
				 &rx_stat->rx_oversize_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RFRG_ADDR,
				 &rx_stat->rx_fragment_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RJBR_ADDR,
				 &rx_stat->rx_jabber_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, RDRP_ADDR,
				 &rx_stat->rx_drop_pkt_count);

	/* Mask out unnecessary bits in all the fields */
	rx_stat->rx_byte_count &= RX_BYTE_CNTR_MASK;
	rx_stat->rx_packet_count &= RX_PKT_CNTR_MASK;
	rx_stat->rx_fcs_err_count &= RX_FCS_ERROR_CNTR_MASK;
	rx_stat->rx_multicast_pkt_count &= RX_MC_PKT_CNTR_MASK;
	rx_stat->rx_broadcast_pkt_count &= RX_BC_PKT_CNTR_MASK;
	rx_stat->rx_cntrl_frame_pkt_count &= RX_CTRL_PKT_CNTR_MASK;
	rx_stat->rx_pause_frame_pkt_count &= RX_PAUSE_PKT_CNTR_MASK;
	rx_stat->rx_unknown_op_pkt_count &= RX_UNK_OPCODE_CNTR_MASK;
	rx_stat->rx_alignment_err_pkt_count &= RX_ALIGN_ERR_CNTR_MASK;
	rx_stat->rx_frm_len_err_pkt_count &= RX_LEN_ERR_CNTR_MASK;
	rx_stat->rx_code_err_pkt_count &= RX_CODE_ERR_CNTR_MASK;
	rx_stat->rx_carrier_sense_err_pkt_count &= RX_FALSE_CARRIER_CNTR_MASK;
	rx_stat->rx_undersize_pkt_count &= RX_UNDRSIZE_PKT_CNTR_MASK;
	rx_stat->rx_oversize_pkt_count &= RX_OVRSIZE_PKT_CNTR_MASK;
	rx_stat->rx_fragment_count &= RX_FRAG_CNTR_MASK;
	rx_stat->rx_jabber_count &= RX_JABBER_CNTR_MASK;
	rx_stat->rx_drop_pkt_count &= RX_DROPPED_PKT_CNTR_MASK;
}

void apm_gmac_get_tx_stats(struct apm_data_priv *priv,
			struct eth_tx_stats *tx_stats)
{
	/* Read Stats */
	apm_enet_read(priv, BLOCK_MCX_STATS, TBYT_ADDR,
				 &tx_stats->tx_byte_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TPKT_ADDR,
				 &tx_stats->tx_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TMCA_ADDR,
				 &tx_stats->tx_multicast_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TBCA_ADDR,
				 &tx_stats->tx_broadcast_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TXPF_ADDR,
				 &tx_stats->tx_pause_frame_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TDFR_ADDR,
				 &tx_stats->tx_deferral_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TEDF_ADDR,
				 &tx_stats->tx_exesiv_def_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TSCL_ADDR,
				 &tx_stats->tx_single_coll_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TMCL_ADDR,
				 &tx_stats->tx_multi_coll_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TLCL_ADDR,
				 &tx_stats->tx_late_coll_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TXCL_ADDR,
				 &tx_stats->tx_exesiv_coll_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TNCL_ADDR,
				 &tx_stats->tx_toll_coll_pkt_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TPFH_ADDR,
				 &tx_stats->tx_pause_frm_hon_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TDRP_ADDR,
				 &tx_stats->tx_drop_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TJBR_ADDR,
				 &tx_stats->tx_jabber_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TFCS_ADDR,
				 &tx_stats->tx_fcs_err_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TXCF_ADDR,
				 &tx_stats->tx_control_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TOVR_ADDR,
				 &tx_stats->tx_oversize_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TUND_ADDR,
				 &tx_stats->tx_undersize_frm_count);
	apm_enet_read(priv, BLOCK_MCX_STATS, TFRG_ADDR,
				 &tx_stats->tx_fragments_frm_count);

	/* Mask values with appropriate width of the fields */
	tx_stats->tx_byte_count &= TX_BYTE_CNTR_MASK;
	tx_stats->tx_pkt_count &= TX_PKT_CNTR_MASK;
	tx_stats->tx_multicast_pkt_count &= TX_MC_PKT_CNTR_MASK;
	tx_stats->tx_broadcast_pkt_count &= TX_BC_PKT_CNTR_MASK;
	tx_stats->tx_pause_frame_count &= TX_PAUSE_PKT_CNTR_MASK;
	tx_stats->tx_deferral_pkt_count &= TX_DEFER_PKT_CNTR_MASK;
	tx_stats->tx_exesiv_def_pkt_count &= TX_EXC_DEFER_PKT_CNTR_MASK;
	tx_stats->tx_single_coll_pkt_count &= TX_COL_PKT_CNTR_MASK;
	tx_stats->tx_multi_coll_pkt_count &= TX_MUL_COL_PKT_CNTR_MASK;
	tx_stats->tx_late_coll_pkt_count &= TX_LATE_COL_PKT_CNTR_MASK;
	tx_stats->tx_exesiv_coll_pkt_count &= TX_EXC_COL_PKT_CNTR_MASK;
	tx_stats->tx_toll_coll_pkt_count &= TX_TOTAL_COL_CNTR_MASK;
	tx_stats->tx_pause_frm_hon_count &= TX_PAUSE_FRAME_CNTR_MASK;
	tx_stats->tx_drop_frm_count &= TX_DROP_FRAME_CNTR_MASK;
	tx_stats->tx_jabber_frm_count &= TX_JABBER_FRAME_CNTR_MASK;
	tx_stats->tx_fcs_err_frm_count &= TX_FCS_ERROR_CNTR_MASK;
	tx_stats->tx_control_frm_count &= TX_CTRL_FRAME_CNTR_MASK;
	tx_stats->tx_oversize_frm_count &= TX_OVRSIZE_FRAME_CNTR_MASK;
	tx_stats->tx_undersize_frm_count &= TX_UNDSIZE_FRAME_CNTR_MASK;
	tx_stats->tx_fragments_frm_count &= TX_FRAG_CNTR_MASK;
}

void apm_menet_clk_rst_cfg(struct apm_data_priv *priv)
{
    u32 wrdata;
    ENET_DEBUG ("MENET CLK_RST configuration\n");

    // disable all clocks
    wrdata = CSR0_CLKEN_WR(0) | ENET0_CLKEN_WR(0) |
		CSR1_CLKEN_WR(0) | ENET1_CLKEN_WR(0);
    apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, wrdata);
    udelay(100);

    // enable all clocks
    wrdata = CSR0_CLKEN_WR(1) | ENET0_CLKEN_WR(1) |
		CSR1_CLKEN_WR(1) | ENET1_CLKEN_WR(1);
    apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, wrdata);
    udelay(100);

    // put csr and core reset
    wrdata = CSR0_RESET_WR(1) | ENET0_RESET_WR(1) |
		CSR1_RESET_WR(1) | ENET1_RESET_WR(1);
    apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, wrdata);
    udelay(100);

    // release csr and core reset
    wrdata = CSR0_RESET_WR(0) | ENET0_RESET_WR(0) |
		CSR1_RESET_WR(0) | ENET1_RESET_WR(0);
    apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, wrdata);
    udelay(100);

    //apm_menet_loopback_cfg( priv );

    enet_ecc_init( priv );
}

void apm_enet_config_qmi_assoc(struct apm_data_priv *priv)
{
    // Configure Ethernet QMI: WQ and FPQ association to QML
    ENET_DEBUG("apm_enet_config_qmi_assoc\n");
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIWQASSOC_ADDR, 0xffffffff);
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIFPQASSOC_ADDR, 0xffffffff);
    //apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIQMLITEFPQASSOC_ADDR, 0xffffffff);
    //apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIQMLITEWQASSOC_ADDR, 0xffffffff);
}

void
apm_menet_config_qmi_assoc(struct apm_data_priv *priv)
{
    // Configure Ethernet QMI: WQ and FPQ association to QML
    ENET_DEBUG("apm_menet_config_qmi_assoc\n");
    ENET_DEBUG("apm_menet_config_qmi_assoc write 0xffffffff to 0x%x\n", ENET_CFGSSQMIWQASSOC_ADDR); 
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIWQASSOC_ADDR, 0xffffffff);
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIFPQASSOC_ADDR, 0xffffffff);
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIQMLITEFPQASSOC_ADDR, 0xffffffff);
    apm_enet_write(priv, BLOCK_ETH_QMI, ENET_CFGSSQMIQMLITEWQASSOC_ADDR, 0xffffffff);
}

void
apm_enet_cle_bypass_mode_cfg(struct apm_data_priv *priv, u32 cle_dstqid, u32 cle_fpsel)
{
    u32 reg;

    // TODO: code from validation team
    // verify these values

    ENET_DEBUG("Enet Bypass CLE\n");

    apm_enet_read(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG0_0_ADDR, &reg);
    reg = CFG_CLE_BYPASS_EN0_SET(reg, 1);
    reg = CFG_CLE_IP_PROTOCOL0_SET(reg, 3);
    apm_enet_write(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG0_0_ADDR, reg);

    // configure dstqid  & fpsel
    apm_enet_read(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG1_0_ADDR, &reg);
    reg = CFG_CLE_DSTQID0_SET(reg, cle_dstqid);
    reg = CFG_CLE_FPSEL0_SET(reg, cle_fpsel);
    apm_enet_write(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG1_0_ADDR, reg);
   
    // configure Henqnum
    apm_enet_read(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG8_0_ADDR, &reg);
    reg = CFG_CLE_HENQNUM0_SET(reg, cle_dstqid);
    apm_enet_write(priv, BLOCK_ETH_CSR, CLE_BYPASS_REG8_0_ADDR, reg);
}

void apm_gmac_rx_state(struct apm_data_priv *priv, u32 enable)
{
	u32 data, rx_en;

	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &data);

	if (enable) 
		rx_en = RX_EN1_SET(data, 1);
	else
		rx_en = RX_EN1_SET(data, 0);

	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, rx_en);
}

void apm_gmac_tx_state(struct apm_data_priv *priv, u32 enable)
{
	u32 data, tx_en;

	apm_enet_read(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, &data);

	if (enable)
		tx_en = TX_EN1_SET(data, 1);
	else
		tx_en = TX_EN1_SET(data, 0);

	apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, tx_en);

}

void apm_gmac_tx_offload(struct apm_data_priv *priv, u32 command, u32 value)
{
	u32 data;

	switch (priv->port) {
	case ENET_0:
	case ENET_2:
	case MENET:
		switch (command) {
		/* TCP MSS 0 */
		case APM_ENET_MSS0:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_0_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_0_ADDR,
				CFG_TSIF_MSS_SZ00_SET(data, value));
			break;
		/* TCP MSS 1 */
		case APM_ENET_MSS1:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_0_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_0_ADDR,
				CFG_TSIF_MSS_SZ10_SET(data, value));
			break;
		/* TCP MSS 2 */
		case APM_ENET_MSS2:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR,
				CFG_TSIF_MSS_SZ20_SET(data, value));
			break;
		/* TCP MSS 3 */
		case APM_ENET_MSS3:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_0_ADDR,
				CFG_TSIF_MSS_SZ30_SET(data, value));
			break;
		/* Program TSO config */
		case APM_ENET_TSO_CFG:
			apm_enet_write(priv, BLOCK_ETH_CSR, TSO_CFG_0_ADDR, value);
			break;
		/* Insert Inser tVLAN TAG */
		case APM_ENET_INSERT_VLAN:
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSO_CFG_INSERT_VLAN_0_ADDR, value);
			break;
		}
		break;
	case ENET_1:
	case ENET_3:
		switch (command) {
		/* TCP MSS 0 */
		case APM_ENET_MSS0:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_1_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_1_ADDR,
				CFG_TSIF_MSS_SZ01_SET(data, value));
			break;
		/* TCP MSS 1 */
		case APM_ENET_MSS1:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_1_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG0_1_ADDR,
				CFG_TSIF_MSS_SZ11_SET(data, value));
			break;
		/* TCP MSS 2 */
		case APM_ENET_MSS2:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_1_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_1_ADDR,
				CFG_TSIF_MSS_SZ21_SET(data, value));
			break;
		/* TCP MSS 3 */
		case APM_ENET_MSS3:
			apm_enet_read(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_1_ADDR, &data);
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSIF_MSS_REG1_1_ADDR,
				CFG_TSIF_MSS_SZ31_SET(data, value));
			break;
		/* Program TSO config */
		case APM_ENET_TSO_CFG:
			apm_enet_write(priv, BLOCK_ETH_CSR, TSO_CFG_1_ADDR, value);
			break;
		/* Insert Inser tVLAN TAG */
		case APM_ENET_INSERT_VLAN:
			apm_enet_write(priv, BLOCK_ETH_CSR,
				TSO_CFG_INSERT_VLAN_1_ADDR, value);
			break;
		}
		break;
	}
}

void apm_gport_reset(struct apm_data_priv *priv, u32 mii_mode)
{
	if (mii_mode == PHY_MODE_RGMII) {
		apm_menet_clk_rst_cfg(priv);
		apm_menet_config_qmi_assoc(priv);
	} else if (mii_mode == PHY_MODE_SGMII) {
		enet_reset(priv);
		apm_enet_config_qmi_assoc(priv);
	}

	priv->phy_mode = mii_mode;
	if (mii_mode == PHY_MODE_RGMII) {
		u32 val;
		/* Enable auto-incr for scanning */
		apm_enet_read(priv, BLOCK_MCX_MAC, MII_MGMT_CONFIG_ADDR, &val);
		val |= SCAN_AUTO_INCR_MASK;
		val = MGMT_CLOCK_SEL_SET(val, 1);
		apm_enet_write(priv, BLOCK_MCX_MAC, MII_MGMT_CONFIG_ADDR, val);
		apm_gmac_phy_enable_scan_cycle(priv, 1);
	}
}

void apm_gport_shutdown(struct apm_data_priv *priv)
{
	u32 mii_mode = priv->phy_mode;
	u32 port = priv->port;
	u32 clk, rst;

	if ((port != MENET) && (port < ENET_0 || port > ENET_3)) {
		PHY_PRINT("Error: invalid port number: %d\n", port);
		goto _ret;
	}

	if (mii_mode != PHY_MODE_RGMII && mii_mode != PHY_MODE_SGMII) {
		PHY_PRINT("Error: invalid mii_mode: %d\n", mii_mode);
		goto _ret;
	}

	// FIXME: check whether port reset will reset mac as well ?
	/* reset mac */
	//apm_enet_write(priv, BLOCK_MCX_MAC, MAC_CONFIG_1_ADDR, SOFT_RESET1_WR(1));

	/* common register for 2 ports */
	if ((port%2) == 0) {
		rst = CSR0_RESET_WR(1) | ENET0_RESET_WR(1);
		clk = CSR0_CLKEN_WR(0) | ENET0_CLKEN_WR(0);
	}
	else {
		rst = CSR1_RESET_WR(1) | ENET1_RESET_WR(1);
		clk = CSR1_CLKEN_WR(0) | ENET1_CLKEN_WR(0);
	}

	/* reset ethernet csr, core and disable clock */
	apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_SRST_ADDR, rst);
	apm_enet_write(priv, BLOCK_ETH_CLKRST_CSR, ENET_CLKEN_ADDR, clk);

_ret:
	return;
}

void apm_enet_init_priv(struct apm_data_priv *priv, void *port_vaddr, void *gbl_vaddr, void *mii_vaddr)
{
	/* Setup the ethernet base address and mac address */
	priv->vaddr_base = gbl_vaddr;
	priv->vpaddr_base = port_vaddr;

	/* Initialize base addresses for direct access */
	priv->eth_csr_addr_v        = gbl_vaddr + BLOCK_ETH_CSR_OFFSET;
	priv->eth_cle_addr_v        = gbl_vaddr + BLOCK_ETH_CLE_OFFSET;
	priv->eth_qmi_addr_v        = gbl_vaddr + BLOCK_ETH_QMI_OFFSET;
	priv->eth_sds_csr_addr_v    = gbl_vaddr + BLOCK_ETH_SDS_CSR_OFFSET;
	priv->eth_clkrst_csr_addr_v = gbl_vaddr + BLOCK_ETH_CLKRST_CSR_OFFSET;
	priv->eth_diag_csr_addr_v   = gbl_vaddr + BLOCK_ETH_DIAG_CSR_OFFSET;

	/* Initialize base addresses for Per Port MII Indirect access */
	priv->mac_mii_addr_v = mii_vaddr;

	/* Initialize base addresses for Per Port indirect & direct MCX MAC access */
	priv->mcx_mac_addr_v       = port_vaddr + BLOCK_ETH_MAC_OFFSET;
	priv->mcx_stats_addr_v     = port_vaddr + BLOCK_ETH_STATS_OFFSET;
	priv->mcx_mac_csr_addr_v   = gbl_vaddr + BLOCK_ETH_MAC_CSR_OFFSET;
	priv->sata_enet_csr_addr_v = gbl_vaddr + BLOCK_SATA_ENET_CSR_OFFSET;

	/* Enable autonegotiation by default */
	priv->autoneg_set = 1;

	ENET_DEBUG("          ETH%d VADDR: 0x%p\n", priv->port, priv->vpaddr_base);
	ENET_DEBUG("           ETH VADDR: 0x%p\n", priv->vaddr_base);
	ENET_DEBUG("       ETH CSR VADDR: 0x%p\n", priv->eth_csr_addr_v);
	ENET_DEBUG("       ETH CLE VADDR: 0x%p\n", priv->eth_cle_addr_v);
	ENET_DEBUG("       ETH QMI VADDR: 0x%p\n", priv->eth_qmi_addr_v);
	ENET_DEBUG("   ETH SDS CSR VADDR: 0x%p\n", priv->eth_sds_csr_addr_v);
	ENET_DEBUG("ETH CLKRST CSR VADDR: 0x%p\n", priv->eth_clkrst_csr_addr_v);
	ENET_DEBUG("      ETH DIAG VADDR: 0x%p\n", priv->eth_diag_csr_addr_v);
	ENET_DEBUG("       MAC MII VADDR: 0x%p\n", priv->mac_mii_addr_v);
	ENET_DEBUG("       MCX MAC VADDR: 0x%p\n", priv->mcx_mac_addr_v);
	ENET_DEBUG("      MCX STAT VADDR: 0x%p\n", priv->mcx_stats_addr_v);
	ENET_DEBUG("   MCX MAC CSR VADDR: 0x%p\n", priv->mcx_mac_csr_addr_v);
	ENET_DEBUG(" SATA ENET CSR VADDR: 0x%p\n", priv->sata_enet_csr_addr_v);

	/* Initialize priv handlers */
	priv->port_reset = apm_gport_reset;
	priv->mac_reset = apm_gmac_reset;
	priv->serdes_reset = serdes_reset_rxd_rxa;
	priv->get_link_status = apm_gmac_link_status;
	priv->autoneg = module_enet_config_sgmii_autoneg;
	priv->mac_init = apm_gmac_init;
	priv->mac_rx_state = apm_gmac_rx_state;
	priv->mac_tx_state = apm_gmac_tx_state;
	priv->mac_change_mtu = apm_gmac_change_mtu;
	priv->mac_set_ipg = apm_gmac_set_ipg;
	priv->get_stats = apm_gmac_get_detailed_stats;
	priv->set_mac_addr = apm_gmac_set_mac_addr;
	priv->cle_bypass = apm_enet_cle_bypass_mode_cfg;
	priv->tx_offload = apm_gmac_tx_offload;
	priv->port_shutdown = apm_gport_shutdown;
}
