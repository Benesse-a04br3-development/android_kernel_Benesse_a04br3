#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#include "bq25896.h"
#include <mt-plat/charging.h>
#include <linux/delay.h>

/**********************************************************
  *
  *   [I2C Slave Setting]
  *
  *********************************************************/
#define bq25896_SLAVE_ADDR_WRITE   0xD6
#define bq25896_SLAVE_ADDR_READ    0xD7

#ifdef CONFIG_OF
static const struct of_device_id bq25896_id[] = {
		{ .compatible = "ti,bq25896" },
		{},
};
MODULE_DEVICE_TABLE(of, bq25896_id);
#endif

static struct i2c_client *new_client;
static const struct i2c_device_id bq25896_i2c_id[] = { {"bq25896", 0}, {} };

kal_bool chargin_hw_init_done = KAL_FALSE;
static int bq25896_driver_probe(struct i2c_client *client, const struct i2c_device_id *id);

static void bq25896_shutdown(struct i2c_client *client)
{
	battery_log(BAT_LOG_FULL, "[bq25896_shutdown] driver shutdown\n");
	bq25896_set_chg_en(0x0);
}
static struct i2c_driver bq25896_driver = {
		.driver = {
				.name    = "bq25896",
#ifdef CONFIG_OF
				.of_match_table = of_match_ptr(bq25896_id),
#endif
		},
		.probe       = bq25896_driver_probe,
		.id_table    = bq25896_i2c_id,
		.shutdown    = bq25896_shutdown,
};
/**********************************************************
  *
  *   [Global Variable]
  *
  *********************************************************/
unsigned char bq25896_reg[bq25896_REG_NUM] = { 0 };

static DEFINE_MUTEX(bq25896_i2c_access);

kal_bool g_bq25896_hw_exist = KAL_FALSE;

/**********************************************************
  *
  *   [I2C Function For Read/Write bq25896]
  *
  *********************************************************/
unsigned int bq25896_read_byte(unsigned char cmd, unsigned char *returnData)
{
	char     readData = 0;
	int      ret = 0;
	struct i2c_msg msg[2];
	struct i2c_adapter *adap = new_client->adapter;

	mutex_lock(&bq25896_i2c_access);
	msg[0].addr = new_client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &cmd;

	msg[1].addr = new_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &readData;

	ret = i2c_transfer(adap, msg, 2);
	if (ret < 0) {
		mutex_unlock(&bq25896_i2c_access);
		return 0;
	}
	*returnData = readData;

	mutex_unlock(&bq25896_i2c_access);
	return 1;
}

unsigned int bq25896_write_byte(unsigned char cmd, unsigned char writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;
	struct i2c_msg msg;
	struct i2c_adapter *adap = new_client->adapter;

	mutex_lock(&bq25896_i2c_access);
	write_data[0] = cmd;
	write_data[1] = writeData;
	msg.addr = new_client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = (char *)write_data;

	ret = i2c_transfer(adap, &msg, 1);
	if (ret < 0) {
		mutex_unlock(&bq25896_i2c_access);
		return 0;
	}

	mutex_unlock(&bq25896_i2c_access);
	return 1;
}
/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
unsigned int bq25896_read_interface(unsigned char RegNum, unsigned char *val, unsigned char MASK,
				  unsigned char SHIFT)
{
	unsigned char bq25896_reg = 0;
	unsigned int ret = 0;

	ret = bq25896_read_byte(RegNum, &bq25896_reg);

	battery_log(BAT_LOG_FULL, "[bq25896_read_interface] Reg[%x]=0x%x\n", RegNum, bq25896_reg);

	bq25896_reg &= (MASK << SHIFT);
	*val = (bq25896_reg >> SHIFT);

	battery_log(BAT_LOG_FULL, "[bq25896_read_interface] val=0x%x\n", *val);

	return ret;
}

unsigned int bq25896_config_interface(unsigned char RegNum, unsigned char val, unsigned char MASK,
				    unsigned char SHIFT)
{
	unsigned char bq25896_reg = 0;
	unsigned int ret = 0;

	ret = bq25896_read_byte(RegNum, &bq25896_reg);
	battery_log(BAT_LOG_FULL, "[bq25896_config_interface] Reg[%x]=0x%x\n", RegNum, bq25896_reg);

	bq25896_reg &= ~(MASK << SHIFT);
	bq25896_reg |= (val << SHIFT);

	ret = bq25896_write_byte(RegNum, bq25896_reg);
	battery_log(BAT_LOG_FULL, "[bq25896_config_interface] write Reg[%x]=0x%x\n", RegNum,
		    bq25896_reg);

	/* Check */
	/* bq25896_read_byte(RegNum, &bq25896_reg); */
	/* printk("[bq25896_config_interface] Check Reg[%x]=0x%x\n", RegNum, bq25896_reg); */

	return ret;
}

/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
/* CON0---------------------------------------------------- */

void bq25896_set_en_hiz(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_EN_HIZ_MASK),
				       (unsigned char) (CON0_EN_HIZ_SHIFT)
	    );
}

void bq25896_set_en_ilim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON0),
				       (unsigned char) (val),
				       (unsigned char) (CON0_EN_ILIM_MASK),
				       (unsigned char) (CON0_EN_ILIM_SHIFT)
	    );
}

void bq25896_set_iinlim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON0),
				       (val),
				       (unsigned char) (CON0_IINLIM_MASK),
				       (unsigned char) (CON0_IINLIM_SHIFT)
	    );
}

unsigned int bq25896_get_iinlim(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON0),
				     (&val),
				     (unsigned char) (CON0_IINLIM_MASK), (unsigned char) (CON0_IINLIM_SHIFT)
	    );
	return val;
}



/* CON1---------------------------------------------------- */

void bq25896_ADC_start(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_CONV_START_MASK),
				       (unsigned char) (CON2_CONV_START_SHIFT)
	    );
}

void bq25896_set_ADC_rate(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_CONV_RATE_MASK),
				       (unsigned char) (CON2_CONV_RATE_SHIFT)
	    );
}

void bq25896_set_vindpm_os(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON1),
				       (unsigned char) (val),
				       (unsigned char) (CON1_VINDPM_OS_MASK),
				       (unsigned char) (CON1_VINDPM_OS_SHIFT)
	    );
}

/* CON2---------------------------------------------------- */

void bq25896_set_ico_en_start(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON2),
				       (unsigned char) (val),
				       (unsigned char) (CON2_ICO_EN_MASK),
				       (unsigned char) (CON2_ICO_EN_RATE_SHIFT)
	    );
}



/* CON3---------------------------------------------------- */

void bq25896_set_wd_rst(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON3),
				       (val),
				       (unsigned char) (CON3_WD_MASK), (unsigned char) (CON3_WD_SHIFT)
	    );

}

void bq25896_set_otg_en(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON3),
				       (val),
				       (unsigned char) (CON3_OTG_CONFIG_MASK),
				       (unsigned char) (CON3_OTG_CONFIG_SHIFT)
	    );

}

void bq25896_set_chg_en(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON3),
				       (val),
				       (unsigned char) (CON3_CHG_CONFIG_MASK),
				       (unsigned char) (CON3_CHG_CONFIG_SHIFT)
	    );

}

unsigned int bq25896_get_chg_en(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON3),
				     (&val),
				     (unsigned char) (CON3_CHG_CONFIG_MASK),
				     (unsigned char) (CON3_CHG_CONFIG_SHIFT)
	    );
	return val;
}


void bq25896_set_sys_min(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON3),
				       (val),
				       (unsigned char) (CON3_SYS_V_LIMIT_MASK),
				       (unsigned char) (CON3_SYS_V_LIMIT_SHIFT)
	    );

}

/* CON4---------------------------------------------------- */

void bq25896_en_pumpx(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_EN_PUMPX_MASK),
				       (unsigned char) (CON4_EN_PUMPX_SHIFT)
	    );
}


void bq25896_set_ichg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON4),
				       (unsigned char) (val),
				       (unsigned char) (CON4_ICHG_MASK), (unsigned char) (CON4_ICHG_SHIFT)
	    );
}

unsigned int bq25896_get_reg_ichg(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON4),
				     (&val),
				     (unsigned char) (CON4_ICHG_MASK), (unsigned char) (CON4_ICHG_SHIFT)
	    );
	return val;
}

/* CON5---------------------------------------------------- */

void bq25896_set_iprechg(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON5),
				       (val),
				       (unsigned char) (CON5_IPRECHG_MASK),
				       (unsigned char) (CON5_IPRECHG_SHIFT)
	    );

}

void bq25896_set_iterml(unsigned int val)
{
	unsigned int ret = 0;


	ret = bq25896_config_interface((unsigned char) (bq25896_CON5),
				       (val),
				       (unsigned char) (CON5_ITERM_MASK), (unsigned char) (CON5_ITERM_SHIFT)
	    );

}



/* CON6---------------------------------------------------- */

void bq25896_set_vreg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_2XTMR_EN_MASK),
				       (unsigned char) (CON6_2XTMR_EN_SHIFT)
	    );
}

void bq25896_set_batlowv(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_BATLOWV_MASK),
				       (unsigned char) (CON6_BATLOWV_SHIFT)
	    );
}

void bq25896_set_vrechg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON6),
				       (unsigned char) (val),
				       (unsigned char) (CON6_VRECHG_MASK),
				       (unsigned char) (CON6_VRECHG_SHIFT)
	    );
}

/* CON7---------------------------------------------------- */


void bq25896_en_term_chg(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_EN_TERM_CHG_MASK),
				       (unsigned char) (CON7_EN_TERM_CHG_SHIFT)
	    );
}

void bq25896_en_state_dis(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_STAT_DIS_MASK),
				       (unsigned char) (CON7_STAT_DIS_SHIFT)
	    );
}

void bq25896_set_wd_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_WTG_TIM_SET_MASK),
				       (unsigned char) (CON7_WTG_TIM_SET_SHIFT)
	    );
}

void bq25896_en_chg_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_EN_TIMER_MASK),
				       (unsigned char) (CON7_EN_TIMER_SHIFT)
	    );
}

void bq25896_set_chg_timer(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON7),
				       (unsigned char) (val),
				       (unsigned char) (CON7_SET_CHG_TIM_MASK),
				       (unsigned char) (CON7_SET_CHG_TIM_SHIFT)
	    );
}

/* CON8--------------------------------------------------- */
void bq25896_set_thermal_regulation(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON8),
				       (unsigned char) (val),
				       (unsigned char) (CON8_TREG_MASK), (unsigned char) (CON8_TREG_SHIFT)
	    );
}

void bq25896_set_VBAT_clamp(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON8),
				       (unsigned char) (val),
				       (unsigned char) (CON8_VCLAMP_MASK),
				       (unsigned char) (CON8_VCLAMP_SHIFT)
	    );
}

void bq25896_set_VBAT_IR_compensation(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CON8),
				       (unsigned char) (val),
				       (unsigned char) (CON8_BAT_COMP_MASK),
				       (unsigned char) (CON8_BAT_COMP_SHIFT)
	    );
}

/* CON9---------------------------------------------------- */
void bq25896_set_pumpx_up(unsigned int val)
{
	unsigned int ret = 0;

	bq25896_en_pumpx(1);
	if (val == 1) {
		ret = bq25896_config_interface((unsigned char) (bq25896_CON9),
					       (unsigned char) (1),
					       (unsigned char) (CON9_PUMPX_UP),
					       (unsigned char) (CON9_PUMPX_UP_SHIFT)
		    );
	} else {
		ret = bq25896_config_interface((unsigned char) (bq25896_CON9),
					       (unsigned char) (1),
					       (unsigned char) (CON9_PUMPX_DN),
					       (unsigned char) (CON9_PUMPX_DN_SHIFT)
		    );
	}
/* Input current limit = 800 mA, changes after port detection*/
	bq25896_set_iinlim(0x14);
/* CC mode current = 2048 mA*/
	bq25896_set_ichg(0x20);
	msleep(3000);
}

/* CONA---------------------------------------------------- */
void bq25896_set_boost_ilim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CONA),
				       (unsigned char) (val),
				       (unsigned char) (CONA_BOOST_ILIM_MASK),
				       (unsigned char) (CONA_BOOST_ILIM_SHIFT)
	    );
}

void bq25896_set_boost_vlim(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_CONA),
				       (unsigned char) (val),
				       (unsigned char) (CONA_BOOST_VLIM_MASK),
				       (unsigned char) (CONA_BOOST_VLIM_SHIFT)
	    );
}

/* CONB---------------------------------------------------- */


unsigned int bq25896_get_vbus_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONB),
				     (&val),
				     (unsigned char) (CONB_VBUS_STAT_MASK),
				     (unsigned char) (CONB_VBUS_STAT_SHIFT)
	    );
	return val;
}


unsigned int bq25896_get_chrg_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONB),
				     (&val),
				     (unsigned char) (CONB_CHRG_STAT_MASK),
				     (unsigned char) (CONB_CHRG_STAT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_pg_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONB),
				     (&val),
				     (unsigned char) (CONB_PG_STAT_MASK),
				     (unsigned char) (CONB_PG_STAT_SHIFT)
	    );
	return val;
}



unsigned int bq25896_get_sdp_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONB),
				     (&val),
				     (unsigned char) (CONB_SDP_STAT_MASK),
				     (unsigned char) (CONB_SDP_STAT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_vsys_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONB),
				     (&val),
				     (unsigned char) (CONB_VSYS_STAT_MASK),
				     (unsigned char) (CONB_VSYS_STAT_SHIFT)
	    );
	return val;
}

/* CON0C---------------------------------------------------- */
unsigned int bq25896_get_wdt_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONC),
				     (&val),
				     (unsigned char) (CONB_WATG_STAT_MASK),
				     (unsigned char) (CONB_WATG_STAT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_boost_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONC),
				     (&val),
				     (unsigned char) (CONB_BOOST_STAT_MASK),
				     (unsigned char) (CONB_BOOST_STAT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_chrg_fault_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONC),
				     (&val),
				     (unsigned char) (CONC_CHRG_FAULT_MASK),
				     (unsigned char) (CONC_CHRG_FAULT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_bat_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONC),
				     (&val),
				     (unsigned char) (CONB_BAT_STAT_MASK),
				     (unsigned char) (CONB_BAT_STAT_SHIFT)
	    );
	return val;
}


/* COND */
void bq25896_set_FORCE_VINDPM(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_COND),
				       (unsigned char) (val),
				       (unsigned char) (COND_FORCE_VINDPM_MASK),
				       (unsigned char) (COND_FORCE_VINDPM_SHIFT)
	    );
}

void bq25896_set_VINDPM(unsigned int val)
{
	unsigned int ret = 0;

	ret = bq25896_config_interface((unsigned char) (bq25896_COND),
				       (unsigned char) (val),
				       (unsigned char) (COND_VINDPM_MASK),
				       (unsigned char) (COND_VINDPM_SHIFT)
	    );
}

/* CONDE */
unsigned int bq25896_get_vbat(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CONE),
				     (&val),
				     (unsigned char) (CONE_VBAT_MASK), (unsigned char) (CONE_VBAT_SHIFT)
	    );
	return val;
}

/* CON11 */
unsigned int bq25896_get_vbus(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON11),
				     (&val),
				     (unsigned char) (CON11_VBUS_MASK), (unsigned char) (CON11_VBUS_SHIFT)
	    );
	return val;
}

/* CON12 */
unsigned int bq25896_get_ichg(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON12),
				     (&val),
				     (unsigned char) (CONB_ICHG_STAT_MASK),
				     (unsigned char) (CONB_ICHG_STAT_SHIFT)
	    );
	return val;
}



/* CON13 /// */


unsigned int bq25896_get_idpm_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON13),
				     (&val),
				     (unsigned char) (CON13_IDPM_STAT_MASK),
				     (unsigned char) (CON13_IDPM_STAT_SHIFT)
	    );
	return val;
}

unsigned int bq25896_get_vdpm_state(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface((unsigned char) (bq25896_CON13),
				     (&val),
				     (unsigned char) (CON13_VDPM_STAT_MASK),
				     (unsigned char) (CON13_VDPM_STAT_SHIFT)
	    );
	return val;
}




/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
void bq25896_hw_component_detect(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = bq25896_read_interface(0x03, &val, 0xFF, 0x0);

	if (val == 0)
		g_bq25896_hw_exist = 0;
	else
		g_bq25896_hw_exist = 1;

	pr_debug("[bq25896_hw_component_detect] exist=%d, Reg[0x03]=0x%x\n",
		 g_bq25896_hw_exist, val);
}

int is_bq25896_exist(void)
{
	pr_debug("[is_bq25896_exist] g_bq25896_hw_exist=%d\n", g_bq25896_hw_exist);

	return g_bq25896_hw_exist;
}

void bq25896_dump_register(void)
{
	unsigned char i = 0;
	unsigned char ichg = 0;
	unsigned char ichg_reg = 0;
	unsigned char iinlim = 0;
	unsigned char vbat = 0;
	unsigned char chrg_state = 0;
	unsigned char chr_en = 0;
	unsigned char vbus = 0;
	unsigned char vdpm = 0;
	unsigned char fault = 0;

	bq25896_ADC_start(1);
	for (i = 0; i < bq25896_REG_NUM; i++) {
		bq25896_read_byte(i, &bq25896_reg[i]);
		battery_log(BAT_LOG_CRTI, "[bq25896 reg@][0x%x]=0x%x ", i, bq25896_reg[i]);
	}
	bq25896_ADC_start(1);
	iinlim = bq25896_get_iinlim();
	chrg_state = bq25896_get_chrg_state();
	chr_en = bq25896_get_chg_en();
	ichg_reg = bq25896_get_reg_ichg();
	ichg = bq25896_get_ichg();
	vbat = bq25896_get_vbat();
	vbus = bq25896_get_vbus();
	vdpm = bq25896_get_vdpm_state();
	fault = bq25896_get_chrg_fault_state();
	battery_log(BAT_LOG_CRTI,
	"[PE+]Ibat=%d, Ilim=%d, Vbus=%d, err=%d, Ichg=%d, Vbat=%d, ChrStat=%d, CHGEN=%d, VDPM=%d\n",
	ichg_reg * 64, iinlim * 50 + 100, vbus * 100 + 2600, fault,
	ichg * 50, vbat * 20 + 2304, chrg_state, chr_en, vdpm);

}

void bq25896_hw_init(void)
{
	/*battery_log(BAT_LOG_CRTI, "[bq25896_hw_init] After HW init\n");*/
	bq25896_dump_register();
}

static int bq25896_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	battery_log(BAT_LOG_CRTI, "[bq25896_driver_probe]\n");

	new_client = client;

	/* --------------------- */
	bq25896_hw_component_detect();
	bq25896_dump_register();
	/* bq25896_hw_init(); //move to charging_hw_xxx.c */
	chargin_hw_init_done = true;

	return 0;


}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
unsigned char g_reg_value_bq25896 = KAL_FALSE;
static ssize_t show_bq25896_access(struct device *dev, struct device_attribute *attr, char *buf)
{
	battery_log(BAT_LOG_CRTI, "[show_bq25896_access] 0x%x\n", g_reg_value_bq25896);
	return sprintf(buf, "%u\n", g_reg_value_bq25896);
}

static ssize_t store_bq25896_access(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret = 0;
	/*char *pvalue = NULL;*/
	unsigned int reg_value = 0;
	unsigned long int reg_address = 0;
	int rv;

	battery_log(BAT_LOG_CRTI, "[store_bq25896_access]\n");

	if (buf != NULL && size != 0) {
		battery_log(BAT_LOG_CRTI, "[store_bq25896_access] buf is %s and size is %zu\n", buf,
			    size);
		/*reg_address = simple_strtoul(buf, &pvalue, 16);*/
		rv = kstrtoul(buf, 0, &reg_address);
			if (rv != 0)
				return -EINVAL;
		/*ret = kstrtoul(buf, 16, reg_address); *//* This must be a null terminated string */
		if (size > 3) {
			/*NEED to check kstr*/
			/*reg_value = simple_strtoul((pvalue + 1), NULL, 16);*/
			/*ret = kstrtoul(buf + 3, 16, reg_value); */
			battery_log(BAT_LOG_CRTI,
				    "[store_bq25896_access] write bq25896 reg 0x%x with value 0x%x !\n",
				    (unsigned int) reg_address, reg_value);
			ret = bq25896_config_interface(reg_address, reg_value, 0xFF, 0x0);
		} else {
			ret = bq25896_read_interface(reg_address, &g_reg_value_bq25896, 0xFF, 0x0);
			battery_log(BAT_LOG_CRTI,
				    "[store_bq25896_access] read bq25896 reg 0x%x with value 0x%x !\n",
				    (unsigned int) reg_address, g_reg_value_bq25896);
			battery_log(BAT_LOG_CRTI,
				    "[store_bq25896_access] Please use \"cat bq25896_access\" to get value\r\n");
		}
	}
	return size;
}

static DEVICE_ATTR(bq25896_access, 0664, show_bq25896_access, store_bq25896_access);	/* 664 */

static int bq25896_user_space_probe(struct platform_device *dev)
{
	int ret_device_file = 0;

	battery_log(BAT_LOG_CRTI, "******** bq25896_user_space_probe!! ********\n");

	ret_device_file = device_create_file(&(dev->dev), &dev_attr_bq25896_access);

	return 0;
}

struct platform_device bq25896_user_space_device = {
	.name = "bq25896-user",
	.id = -1,
};

static struct platform_driver bq25896_user_space_driver = {
	.probe = bq25896_user_space_probe,
	.driver = {
		   .name = "bq25896-user",
		   },
};

static int __init bq25896_init(void)
{
	int ret = 0;

	/* i2c registeration using DTS instead of boardinfo*/

	battery_log(BAT_LOG_CRTI, "[bq25896_init] init start. ch=%d\n", BQ25896_BUSNUM);
#ifndef CONFIG_OF
	i2c_register_board_info(BQ25896_BUSNUM, &i2c_bq25896, 1);
#endif

	if (i2c_add_driver(&bq25896_driver) != 0) {
		battery_log(BAT_LOG_CRTI,
			    "[bq25896_init] failed to register bq25896 i2c driver.\n");
	} else {
		battery_log(BAT_LOG_CRTI,
			    "[bq25896_init] Success to register bq25896 i2c driver.\n");
	}

	/* bq25896 user space access interface */
	ret = platform_device_register(&bq25896_user_space_device);
	if (ret) {
		battery_log(BAT_LOG_CRTI, "****[bq25896_init] Unable to device register(%d)\n",
			    ret);
		return ret;
	}
	ret = platform_driver_register(&bq25896_user_space_driver);
	if (ret) {
		battery_log(BAT_LOG_CRTI, "****[bq25896_init] Unable to register driver (%d)\n",
			    ret);
		return ret;
	}

	return 0;
}

static void __exit bq25896_exit(void)
{
	i2c_del_driver(&bq25896_driver);
}
module_init(bq25896_init);
module_exit(bq25896_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C bq25896 Driver");
MODULE_AUTHOR("will cai <will.cai@mediatek.com>");
