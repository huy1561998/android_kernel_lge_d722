/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/qpnp/pin.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/qpnp/pwm.h>
#include <linux/err.h>
#include <mach/board_lge.h>


#include "mdss.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"
#define DEBUG

#define DT_CMD_HDR 6
#ifdef CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL
extern int mdss_dsi_lane_config(struct mdss_panel_data *pdata, int enable);
#endif

DEFINE_LED_TRIGGER(bl_led_trigger);

#if defined(CONFIG_BACKLIGHT_LM3630)
extern void lm3630_lcd_backlight_set_level(int level);
#elif defined(CONFIG_BACKLIGHT_LM3530)
extern void lm3530_lcd_backlight_set_level(int level);
#elif defined(CONFIG_BACKLIGHT_RT8542)
extern void rt8542_lcd_backlight_set_level(int level);
#elif defined(CONFIG_BACKLIGHT_RT8555)
extern void rt8555_lcd_backlight_set_level(int level);
#elif defined(CONFIG_BACKLIGHT_LM3697)
extern void lm3697_lcd_backlight_set_level(int level);
#endif

#if defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) || defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL)
static struct dsi_panel_cmds lge_display_on_cmds;
static struct dsi_panel_cmds lge_sleep_in_cmds;
extern int is_dsv_cont_splash_screening_f;
extern int has_dsv_f;
#endif

#if defined (CONFIG_MACH_MSM8X10_W5) || defined (CONFIG_MACH_MSM8X10_W6)
/* At booting up, Between LG Logo and Operation Animation showing, abnormal LG Logo is appearing one time.
Because LG Logo image format is RGB888, Android side image format is RGBA8888, both Image formats are mismatched.
So, We add the code to change MDP_RGBA_8888 to MDP_RGB_888 at mdp3_overlay_set when is_done_drawing_logo is not "1".
is_done_drawing_logo is set to 1 at mdss_dsi_panel_off.
*/
char is_done_drawing_logo = 0;
#endif
#if defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL) || defined(CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL)
static struct dsi_panel_cmds lge_display_on_cmds;
static struct dsi_panel_cmds lge_display_off_cmds;
extern int is_dsv_cont_splash_screening_f;
extern int has_dsv_f;
#endif

#if defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
static struct dsi_panel_cmds lge_display_power_setting;
#endif

#if defined(CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL)
static struct dsi_panel_cmds lge_sleep_out_cmds;
static struct dsi_panel_cmds lge_sleep_in_cmds;
static struct dsi_panel_cmds lge_color_cmds;
#endif

#if defined(CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL)
extern int is_dsv_cont_splash_screening_f;
extern int has_dsv_f;
static struct dsi_panel_cmds lge_sleep_in_cmds;
static struct dsi_panel_cmds lge_display_off_cmds;
static struct dsi_panel_cmds lge_display_on_cmds;
static struct dsi_panel_cmds lge_display_on_cmds_2;
static struct dsi_panel_cmds lge_sleep_out_cmds;
#endif
void mdss_dsi_panel_pwm_cfg(struct mdss_dsi_ctrl_pdata *ctrl)
{
	ctrl->pwm_bl = pwm_request(ctrl->pwm_lpg_chan, "lcd-bklt");
	if (ctrl->pwm_bl == NULL || IS_ERR(ctrl->pwm_bl)) {
		pr_err("%s: Error: lpg_chan=%d pwm request failed",
				__func__, ctrl->pwm_lpg_chan);
	}
}

static void mdss_dsi_panel_bklt_pwm(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	int ret;
	u32 duty;

	if (ctrl->pwm_bl == NULL) {
		pr_err("%s: no PWM\n", __func__);
		return;
	}

	if (level == 0) {
		if (ctrl->pwm_enabled)
			pwm_disable(ctrl->pwm_bl);
		ctrl->pwm_enabled = 0;
		return;
	}

	duty = level * ctrl->pwm_period;
	duty /= ctrl->bklt_max;

	pr_debug("%s: bklt_ctrl=%d pwm_period=%d pwm_gpio=%d pwm_lpg_chan=%d\n",
			__func__, ctrl->bklt_ctrl, ctrl->pwm_period,
				ctrl->pwm_pmic_gpio, ctrl->pwm_lpg_chan);

	pr_debug("%s: ndx=%d level=%d duty=%d\n", __func__,
					ctrl->ndx, level, duty);

	if (ctrl->pwm_enabled) {
		pwm_disable(ctrl->pwm_bl);
		ctrl->pwm_enabled = 0;
	}

	ret = pwm_config_us(ctrl->pwm_bl, duty, ctrl->pwm_period);
	if (ret) {
		pr_err("%s: pwm_config_us() failed err=%d.\n", __func__, ret);
		return;
	}

	ret = pwm_enable(ctrl->pwm_bl);
	if (ret)
		pr_err("%s: pwm_enable() failed err=%d\n", __func__, ret);
	ctrl->pwm_enabled = 1;
}

static char dcs_cmd[2] = {0x54, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc dcs_read_cmd = {
	{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(dcs_cmd)},
	dcs_cmd
};

u32 mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len)
{
	struct dcs_cmd_req cmdreq;

	dcs_cmd[0] = cmd0;
	dcs_cmd[1] = cmd1;
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &dcs_read_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
	cmdreq.rlen = len;
	cmdreq.rbuf = rbuf;
	cmdreq.cb = fxn; /* call back */
	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
	/*
	 * blocked here, until call back called
	 */

	return 0;
}

static void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
			struct dsi_panel_cmds *pcmds)
{
	struct dcs_cmd_req cmdreq;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = pcmds->cmds;
	cmdreq.cmds_cnt = pcmds->cmd_cnt;
	cmdreq.flags = CMD_REQ_COMMIT;

	/*Panel ON/Off commands should be sent in DSI Low Power Mode*/
	if (pcmds->link_state == DSI_LP_MODE)
		cmdreq.flags  |= CMD_REQ_LP_MODE;

	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

static char led_pwm1[2] = {0x51, 0x0};	/* DTYPE_DCS_WRITE1 */
static struct dsi_cmd_desc backlight_cmd = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1, sizeof(led_pwm1)},
	led_pwm1
};

static void mdss_dsi_panel_bklt_dcs(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	struct dcs_cmd_req cmdreq;

	pr_debug("%s: level=%d\n", __func__, level);

	led_pwm1[1] = (unsigned char)level;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &backlight_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

void mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	int i;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		pr_debug("%s:%d, disp_en line not configured\n",
			   __func__, __LINE__);
	}

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);
	pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (enable) {
#if !defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) && !defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL) && \
	!defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL) && \
	!defined(CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL) && \
	!defined(CONFIG_LGE_MIPI_DSI_LGD_LVDS_WXGA) && !defined(CONFIG_LGE_MIPI_DSI_LGD_NT35521_WXGA) && \
	!defined(CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL)

		if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
			gpio_set_value((ctrl_pdata->disp_en_gpio), 1);
#endif
#if defined(CONFIG_LGE_MIPI_NT35521_VIDEO_720P_PANEL)
		usleep(50000);
#endif

		for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
			gpio_set_value((ctrl_pdata->rst_gpio),
				pdata->panel_info.rst_seq[i]);
			if (pdata->panel_info.rst_seq[++i])
				usleep(pdata->panel_info.rst_seq[i] * 1000);
		}

		if (gpio_is_valid(ctrl_pdata->mode_gpio)) {
			if (pinfo->mode_gpio_state == MODE_GPIO_HIGH)
				gpio_set_value((ctrl_pdata->mode_gpio), 1);
			else if (pinfo->mode_gpio_state == MODE_GPIO_LOW)
				gpio_set_value((ctrl_pdata->mode_gpio), 0);
		}
		if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
			pr_debug("%s: Panel Not properly turned OFF\n",
						__func__);
			ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
			pr_debug("%s: Reset panel done\n", __func__);
		}
	} else {
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
#if !defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) && !defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL) && \
	!defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL) && \
	!defined(CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL) && \
	!defined(CONFIG_LGE_MIPI_DSI_LGD_LVDS_WXGA) && !defined(CONFIG_LGE_MIPI_DSI_LGD_NT35521_WXGA) &&\
	!defined(CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL)

		if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
			gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
#endif
	}
}

#if defined(CONFIG_LGE_MIPI_DSI_LGD_LVDS_WXGA)
int lge_lvds_panel_power(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if(enable){
		pr_info("%s: LGE LVDS Panel Power On!\n", __func__);
		gpio_set_value((ctrl_pdata->disp_en_gpio), 1);
		gpio_set_value((ctrl_pdata->lcd_stby_gpio), 1);
		mdelay(10);
	}
	else{
		pr_info("%s: LGE LVDS Panel Power Off!\n", __func__);
		mdelay(1);
		gpio_set_value((ctrl_pdata->lcd_stby_gpio), 0);
		gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
		mdelay(210);
	}

	return 0;
}
#endif

#if defined(CONFIG_LGE_MIPI_DSI_LGD_NT35521_WXGA)
int nt35521_panel_power(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (enable) {
		pr_info("%s: LGE E7 Panel Power On!\n", __func__);
		gpio_set_value((ctrl_pdata->lcd_dsv_enp_gpio), 1); //DSV ENP
		mdelay(1);
		gpio_set_value(ctrl_pdata->lcd_dsv_enn_gpio, 1); //DSV ENN
		mdelay(50);
	} else {
		pr_info("%s: LGE E7 Panel Power Off!\n", __func__);
		gpio_set_value(ctrl_pdata->lcd_dsv_enn_gpio, 0);
		mdelay(1);
		gpio_set_value((ctrl_pdata->lcd_dsv_enp_gpio), 0);
	}

	return 0;
}

#endif


static char caset[] = {0x2a, 0x00, 0x00, 0x03, 0x00};	/* DTYPE_DCS_LWRITE */
static char paset[] = {0x2b, 0x00, 0x00, 0x05, 0x00};	/* DTYPE_DCS_LWRITE */

static struct dsi_cmd_desc partial_update_enable_cmd[] = {
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(caset)}, caset},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(paset)}, paset},
};

static int mdss_dsi_panel_partial_update(struct mdss_panel_data *pdata)
{
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct dcs_cmd_req cmdreq;
	int rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi  = &pdata->panel_info.mipi;

	pr_debug("%s: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);

	caset[1] = (((pdata->panel_info.roi_x) & 0xFF00) >> 8);
	caset[2] = (((pdata->panel_info.roi_x) & 0xFF));
	caset[3] = (((pdata->panel_info.roi_x - 1 + pdata->panel_info.roi_w)
								& 0xFF00) >> 8);
	caset[4] = (((pdata->panel_info.roi_x - 1 + pdata->panel_info.roi_w)
								& 0xFF));
	partial_update_enable_cmd[0].payload = caset;

	paset[1] = (((pdata->panel_info.roi_y) & 0xFF00) >> 8);
	paset[2] = (((pdata->panel_info.roi_y) & 0xFF));
	paset[3] = (((pdata->panel_info.roi_y - 1 + pdata->panel_info.roi_h)
								& 0xFF00) >> 8);
	paset[4] = (((pdata->panel_info.roi_y - 1 + pdata->panel_info.roi_h)
								& 0xFF));
	partial_update_enable_cmd[1].payload = paset;

	pr_debug("%s: enabling partial update\n", __func__);
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = partial_update_enable_cmd;
	cmdreq.cmds_cnt = 2;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);

	return rc;
}

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
#include <mach/board_lge.h>

void qpnp_goto_suspend_for_chg_logo(void);
#endif


static void mdss_dsi_panel_bl_ctrl(struct mdss_panel_data *pdata,
							u32 bl_level)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/*
	 * Some backlight controllers specify a minimum duty cycle
	 * for the backlight brightness. If the brightness is less
	 * than it, the controller can malfunction.
	 */

	if ((bl_level < pdata->panel_info.bl_min) && (bl_level != 0))
		bl_level = pdata->panel_info.bl_min;

	switch (ctrl_pdata->bklt_ctrl) {
	case BL_WLED:

#if defined(CONFIG_LGE_MIPI_DSI_LGD_LVDS_WXGA)
		if(system_rev==0){
			pr_err("%s:bl_level=%d\n", __func__, bl_level);
			if (bl_level==0) {
			gpio_set_value((ctrl_pdata->bl_en_gpio), 0);
			gpio_set_value((ctrl_pdata->bl_vled_gpio), 0);
			gpio_set_value((ctrl_pdata->bl_pwm_gpio), 0);
			}
			else {
			gpio_set_value((ctrl_pdata->bl_en_gpio), 1);
			gpio_set_value((ctrl_pdata->bl_vled_gpio), 1);
			gpio_set_value((ctrl_pdata->bl_pwm_gpio), 1);
			}
		}
		else
			rt8555_lcd_backlight_set_level(bl_level);
#else
#if defined(CONFIG_BACKLIGHT_LM3630)
	#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
			if(bl_level > 16)
				bl_level = bl_level/16;
	#endif
		lm3630_lcd_backlight_set_level(bl_level);
#elif defined(CONFIG_BACKLIGHT_LM3530)
		lm3530_lcd_backlight_set_level(bl_level);
#elif defined(CONFIG_BACKLIGHT_RT8542)
		rt8542_lcd_backlight_set_level(bl_level);
#elif defined(CONFIG_BACKLIGHT_LM3697)
		lm3697_lcd_backlight_set_level(bl_level);
#else
		led_trigger_event(bl_led_trigger, bl_level);

#endif //CONFIG_BACKLIGHT_LM3630
#endif //                                 
		break;
	case BL_PWM:
		mdss_dsi_panel_bklt_pwm(ctrl_pdata, bl_level);
		break;
	case BL_DCS_CMD:
		mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
		break;
	default:
		pr_err("%s: Unknown bl_ctrl configuration\n",
			__func__);
		break;
	}

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
    if(lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO && bl_level == 0)
    {
        qpnp_goto_suspend_for_chg_logo();
    }
#endif
}

#ifdef CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL
static int mdss_dsi_panel_on(struct mdss_panel_data *pdata)
{
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;

	hw_rev_type hw_rev;
	hw_rev = lge_get_board_revno();

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	mipi  = &pdata->panel_info.mipi;

	pr_info("%s+: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);
	pr_info("[LCD] %s+: ctrl->on_cmds.cmd_cnt : %d\n", __func__, ctrl->on_cmds.cmd_cnt);
	pr_info("[LCD] %s: defined CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL", __func__);
	if (!is_dsv_cont_splash_screening_f && ctrl->on_cmds.cmd_cnt) //          
	{
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->on_cmds);
	}

	if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio))
	{
//		pr_info("[LCD] %s[%d]: set disp_en_gpio... ", __func__, __LINE__);
//		gpio_set_value((ctrl->disp_en_gpio), 1);
	}

	if(is_dsv_cont_splash_screening_f)
	{
		pr_info("[LCD] %s[%d]: is_dsv_cont_splash_screening_f == TRUE... delay 130 ", __func__, __LINE__);
		msleep(130);
	}
	else
	{
		pr_info("[LCD] %s[%d]: is_dsv_cont_splash_screening_f == FALSE... delay 5 ", __func__, __LINE__);
		msleep(5);
	}

	if (lge_sleep_out_cmds.cmd_cnt) {
		pr_info("sending lge_sleep_out_cmds code\n");
		mdss_dsi_panel_cmds_send(ctrl, &lge_sleep_out_cmds);
	}
	mdelay(200);

	if (lge_color_cmds.cmd_cnt) {
		pr_info("sending lge_color_cmds code\n");
		mdss_dsi_panel_cmds_send(ctrl, &lge_color_cmds);
	}

	if (lge_display_on_cmds.cmd_cnt) {
		pr_info("sending lge_display_on_cmds code\n");
		mdss_dsi_panel_cmds_send(ctrl, &lge_display_on_cmds);
	}

	pr_info("%s:-\n", __func__);
	return 0;
}
#else
static int mdss_dsi_panel_on(struct mdss_panel_data *pdata)
{
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
#if defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL)
	int gpio_get;
#endif

	hw_rev_type hw_rev;
	hw_rev = lge_get_board_revno();

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	mipi  = &pdata->panel_info.mipi;

#ifdef CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL
	printk("[LCD] %s+: is_dsv_cont_splash_screening_f : %d\n", __func__, is_dsv_cont_splash_screening_f);
#endif

#if defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) || defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL) || defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
	pr_info("[LCD] %s: defined CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL", __func__);
	if (!is_dsv_cont_splash_screening_f && ctrl->on_cmds.cmd_cnt) //          
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->on_cmds);
#elif defined(CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL)
	if (!is_dsv_cont_splash_screening_f && ctrl->on_cmds.cmd_cnt) //          
	{
		mdss_dsi_lane_config(&ctrl->panel_data, 1);
		printk("on_cmds_sending=%d\n",ctrl->on_cmds.cmd_cnt);
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->on_cmds);
		printk("on_cmds_sent=%d\n",ctrl->on_cmds.cmd_cnt);
//		if(lge_sleep_out_cmds.cmd_cnt) {
//			mdss_dsi_panel_cmds_send(ctrl, &lge_sleep_out_cmds);
//        printk("sleep_out_cmds sent\n");
//		}
		gpio_set_value((ctrl->disp_en_gpio), 1);
		mdelay(10);
//		if(lge_display_on_cmds.cmd_cnt) {
//			mdss_dsi_panel_cmds_send(ctrl, &lge_display_on_cmds);
//			printk("lge_display_on_cmds sent \n");
//		}
		if(lge_display_on_cmds_2.cmd_cnt) {
			mdss_dsi_panel_cmds_send(ctrl, &lge_display_on_cmds_2);
			printk("on_cmds_2 sent \n");
		}
		mdss_dsi_lane_config(&ctrl->panel_data, 0);
		mdelay(50);
	}
#else
	if (ctrl->on_cmds.cmd_cnt)												  //qct original
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->on_cmds);
#endif


#if defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 == hw_rev)
#endif
    {
    	pr_info("%s HW_REV_0 \n", __func__);
		if(!is_dsv_cont_splash_screening_f && lge_display_power_setting.cmd_cnt) {
			mdelay(20);
			mdss_dsi_panel_cmds_send(ctrl, &lge_display_power_setting);
		}
	}
#endif

#if defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) || defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL) || defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 == hw_rev)
#endif
    {
		if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio))
			gpio_set_value((ctrl->disp_en_gpio), 1);
#if defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL)
		gpio_get = gpio_get_value(23);

		if(gpio_get){
			msleep(5);
		} else {
			if(is_dsv_cont_splash_screening_f){
				msleep(130);
			} else {
				msleep(80);
			}
		}
#elif defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL)
		if(is_dsv_cont_splash_screening_f)
			msleep(130);
		else
			msleep(5);
#else
		if(is_dsv_cont_splash_screening_f)
			msleep(130);
		else
			msleep(80);
#endif
		if (lge_display_on_cmds.cmd_cnt) {
			pr_info("sending diplay on code\n");
			mdss_dsi_panel_cmds_send(ctrl, &lge_display_on_cmds);
		}
	}
#endif

	pr_info("%s:-\n", __func__);
	return 0;
}

#endif


#ifdef CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL
static int mdss_dsi_panel_off(struct mdss_panel_data *pdata)
{
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;

	hw_rev_type hw_rev;
	hw_rev = lge_get_board_revno();

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_info("%s+: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);

	mipi  = &pdata->panel_info.mipi;


	if (ctrl->off_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds);

	mdelay(40);
	if(lge_display_off_cmds.cmd_cnt) {
		mdss_dsi_panel_cmds_send(ctrl, &lge_display_off_cmds);
	}
	mdelay(40);
	if(lge_sleep_in_cmds.cmd_cnt) {
		mdss_dsi_panel_cmds_send(ctrl, &lge_display_off_cmds);
	}
	mdelay(40);
	if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio)){
		gpio_set_value((ctrl->disp_en_gpio), 0);
		if (has_dsv_f)
		{
			pr_info("%s: panel reset\n", __func__);
			mdss_dsi_panel_reset(pdata, 0);
		}
	}

	if (ctrl->off_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds);

	pr_info("%s:-\n", __func__);
	return 0;
}
#else
static int mdss_dsi_panel_off(struct mdss_panel_data *pdata)
{
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;

	hw_rev_type hw_rev;
	hw_rev = lge_get_board_revno();

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_info("%s+: ctrl=%p ndx=%d\n", __func__, ctrl, ctrl->ndx);

	mipi  = &pdata->panel_info.mipi;

#ifdef CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL
	if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio)){
		if(lge_display_off_cmds.cmd_cnt) {
			mdss_dsi_panel_cmds_send(ctrl, &lge_display_off_cmds);
		}
		printk("ctrl->disp_en_gpio = %x \n",ctrl->disp_en_gpio);
		gpio_set_value((ctrl->disp_en_gpio), 0);
		if (has_dsv_f){
			printk("DSV_EN LOW \n");
			mdelay(10);
			if(lge_sleep_in_cmds.cmd_cnt) {
				mdss_dsi_panel_cmds_send(ctrl, &lge_sleep_in_cmds);
			}
			mdelay(50);
			mdss_dsi_panel_reset(pdata, 0);
		}
	}
#else

#if defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 == hw_rev)
#endif
    {
		if (ctrl->off_cmds.cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds);

		mdelay(20);
		if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio)){
			gpio_set_value((ctrl->disp_en_gpio), 0);
			if (has_dsv_f)
				mdss_dsi_panel_reset(pdata, 0);
		}
		mdelay(20);
		if(lge_display_off_cmds.cmd_cnt) {
			mdss_dsi_panel_cmds_send(ctrl, &lge_display_off_cmds);
		}
		mdelay(20);
	}
#endif

#if !defined(CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL)
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 != hw_rev)
#endif
    {
		if (ctrl->off_cmds.cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds);
	}
#endif

#if defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) || defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL)
	if (!is_dsv_cont_splash_screening_f && gpio_is_valid(ctrl->disp_en_gpio)){
		gpio_set_value((ctrl->disp_en_gpio), 0);
		if (has_dsv_f)
			mdss_dsi_panel_reset(pdata, 0);
	}
#endif

#endif

#if defined (CONFIG_MACH_MSM8X10_W5) || defined (CONFIG_MACH_MSM8X10_W6)
/* At booting up, Between LG Logo and Operation Animation showing, abnormal LG Logo is appearing one time.
Because LG Logo image format is RGB888, Android side image format is RGBA8888, both Image formats are mismatched.
So, We add the code to change MDP_RGBA_8888 to MDP_RGB_888 at mdp3_overlay_set when is_done_drawing_logo is not "1".
is_done_drawing_logo is set to 1 at mdss_dsi_panel_off.
*/
	is_done_drawing_logo = 1;
#endif

	pr_info("%s:-\n", __func__);
	return 0;
}
#endif

static void mdss_dsi_parse_lane_swap(struct device_node *np, char *dlane_swap)
{
	const char *data;

	*dlane_swap = DSI_LANE_MAP_0123;
	data = of_get_property(np, "qcom,mdss-dsi-lane-map", NULL);
	if (data) {
		if (!strcmp(data, "lane_map_3012"))
			*dlane_swap = DSI_LANE_MAP_3012;
		else if (!strcmp(data, "lane_map_2301"))
			*dlane_swap = DSI_LANE_MAP_2301;
		else if (!strcmp(data, "lane_map_1230"))
			*dlane_swap = DSI_LANE_MAP_1230;
		else if (!strcmp(data, "lane_map_0321"))
			*dlane_swap = DSI_LANE_MAP_0321;
		else if (!strcmp(data, "lane_map_1032"))
			*dlane_swap = DSI_LANE_MAP_1032;
		else if (!strcmp(data, "lane_map_2103"))
			*dlane_swap = DSI_LANE_MAP_2103;
		else if (!strcmp(data, "lane_map_3210"))
			*dlane_swap = DSI_LANE_MAP_3210;
	}
}

static void mdss_dsi_parse_trigger(struct device_node *np, char *trigger,
		char *trigger_key)
{
	const char *data;

	*trigger = DSI_CMD_TRIGGER_SW;
	data = of_get_property(np, trigger_key, NULL);
	if (data) {
		if (!strcmp(data, "none"))
			*trigger = DSI_CMD_TRIGGER_NONE;
		else if (!strcmp(data, "trigger_te"))
			*trigger = DSI_CMD_TRIGGER_TE;
		else if (!strcmp(data, "trigger_sw_seof"))
			*trigger = DSI_CMD_TRIGGER_SW_SEOF;
		else if (!strcmp(data, "trigger_sw_te"))
			*trigger = DSI_CMD_TRIGGER_SW_TE;
	}
}


static int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key)
{
	const char *data;
	int blen = 0, len;
	char *buf, *bp;
	struct dsi_ctrl_hdr *dchdr;
	int i, cnt;

	data = of_get_property(np, cmd_key, &blen);
	if (!data) {
		pr_err("%s: failed, key=%s\n", __func__, cmd_key);
		return -ENOMEM;
	}

	buf = kzalloc(sizeof(char) * blen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, blen);

	/* scan dcs commands */
	bp = buf;
	len = blen;
	cnt = 0;
	while (len > sizeof(*dchdr)) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		dchdr->dlen = ntohs(dchdr->dlen);
		if (dchdr->dlen > len) {
			pr_err("%s: dtsi cmd=%x error, len=%d",
				__func__, dchdr->dtype, dchdr->dlen);
			goto exit_free;
		}
		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		pr_err("%s: dcs_cmd=%x len=%d error!",
				__func__, buf[0], blen);
		goto exit_free;
	}

	pcmds->cmds = kzalloc(cnt * sizeof(struct dsi_cmd_desc),
						GFP_KERNEL);
	if (!pcmds->cmds)
		goto exit_free;

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	bp = buf;
	len = blen;
	for (i = 0; i < cnt; i++) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	data = of_get_property(np, link_key, NULL);
	if (data && !strcmp(data, "dsi_hs_mode"))
		pcmds->link_state = DSI_HS_MODE;
	else
		pcmds->link_state = DSI_LP_MODE;

	pr_debug("%s: dcs_cmd=%x len=%d, cmd_cnt=%d link_state=%d\n", __func__,
		pcmds->buf[0], pcmds->blen, pcmds->cmd_cnt, pcmds->link_state);

	return 0;

exit_free:
	kfree(buf);
	return -ENOMEM;
}


static int mdss_panel_dt_get_dst_fmt(u32 bpp, char mipi_mode, u32 pixel_packing,
				char *dst_format)
{
	int rc = 0;
	switch (bpp) {
	case 3:
		*dst_format = DSI_CMD_DST_FORMAT_RGB111;
		break;
	case 8:
		*dst_format = DSI_CMD_DST_FORMAT_RGB332;
		break;
	case 12:
		*dst_format = DSI_CMD_DST_FORMAT_RGB444;
		break;
	case 16:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB565;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		}
		break;
	case 18:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB666;
			break;
		default:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		}
		break;
	case 24:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB888;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}


static int mdss_dsi_parse_fbc_params(struct device_node *np,
				struct mdss_panel_info *panel_info)
{
	int rc, fbc_enabled = 0;
	u32 tmp;

	fbc_enabled = of_property_read_bool(np,	"qcom,mdss-dsi-fbc-enable");
	if (fbc_enabled) {
		pr_debug("%s:%d FBC panel enabled.\n", __func__, __LINE__);
		panel_info->fbc.enabled = 1;
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bpp", &tmp);
		panel_info->fbc.target_bpp =	(!rc ? tmp : panel_info->bpp);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-packing",
				&tmp);
		panel_info->fbc.comp_mode = (!rc ? tmp : 0);
		panel_info->fbc.qerr_enable = of_property_read_bool(np,
			"qcom,mdss-dsi-fbc-quant-error");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bias", &tmp);
		panel_info->fbc.cd_bias = (!rc ? tmp : 0);
		panel_info->fbc.pat_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-pat-mode");
		panel_info->fbc.vlc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-vlc-mode");
		panel_info->fbc.bflc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-bflc-mode");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-h-line-budget",
				&tmp);
		panel_info->fbc.line_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-budget-ctrl",
				&tmp);
		panel_info->fbc.block_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-block-budget",
				&tmp);
		panel_info->fbc.block_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossless-threshold", &tmp);
		panel_info->fbc.lossless_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-threshold", &tmp);
		panel_info->fbc.lossy_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-rgb-threshold",
				&tmp);
		panel_info->fbc.lossy_rgb_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-mode-idx", &tmp);
		panel_info->fbc.lossy_mode_idx = (!rc ? tmp : 0);
	} else {
		pr_debug("%s:%d Panel does not support FBC.\n",
				__func__, __LINE__);
		panel_info->fbc.enabled = 0;
		panel_info->fbc.target_bpp =
			panel_info->bpp;
	}
	return 0;
}


static int mdss_dsi_parse_reset_seq(struct device_node *np,
		u32 rst_seq[MDSS_DSI_RST_SEQ_LEN], u32 *rst_len,
		const char *name)
{
	int num = 0, i;
	int rc;
	struct property *data;
	u32 tmp[MDSS_DSI_RST_SEQ_LEN];
	*rst_len = 0;
	data = of_find_property(np, name, &num);
	num /= sizeof(u32);
	if (!data || !num || num > MDSS_DSI_RST_SEQ_LEN || num % 2) {
		pr_debug("%s:%d, error reading %s, length found = %d\n",
			__func__, __LINE__, name, num);
	} else {
		rc = of_property_read_u32_array(np, name, tmp, num);
		if (rc)
			pr_debug("%s:%d, error reading %s, rc = %d\n",
				__func__, __LINE__, name, rc);
		else {
			for (i = 0; i < num; ++i)
				rst_seq[i] = tmp[i];
			*rst_len = num;
		}
	}
	return 0;
}


static int mdss_panel_parse_dt(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	u32 tmp;
	int rc, i, len;
	const char *data;
	static const char *pdest;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	hw_rev_type hw_rev;
	hw_rev = lge_get_board_revno();

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-width", &tmp);
	if (rc) {
		pr_err("%s:%d, panel width not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pinfo->xres = (!rc ? tmp : 640);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-height", &tmp);
	if (rc) {
		pr_err("%s:%d, panel height not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pinfo->yres = (!rc ? tmp : 480);

	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-width-dimension", &tmp);
	pinfo->physical_width = (!rc ? tmp : 0);
	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-height-dimension", &tmp);
	pinfo->physical_height = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-left-border", &tmp);
	pinfo->lcdc.xres_pad = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-right-border", &tmp);
	if (!rc)
		pinfo->lcdc.xres_pad += tmp;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-top-border", &tmp);
	pinfo->lcdc.yres_pad = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-bottom-border", &tmp);
	if (!rc)
		pinfo->lcdc.yres_pad += tmp;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bpp", &tmp);
	if (rc) {
		pr_err("%s:%d, bpp not specified\n", __func__, __LINE__);
		return -EINVAL;
	}
	pinfo->bpp = (!rc ? tmp : 24);
	pinfo->mipi.mode = DSI_VIDEO_MODE;
	data = of_get_property(np, "qcom,mdss-dsi-panel-type", NULL);
	if (data && !strncmp(data, "dsi_cmd_mode", 12))
		pinfo->mipi.mode = DSI_CMD_MODE;
	tmp = 0;
	data = of_get_property(np, "qcom,mdss-dsi-pixel-packing", NULL);
	if (data && !strcmp(data, "loose"))
		tmp = 1;
	rc = mdss_panel_dt_get_dst_fmt(pinfo->bpp,
		pinfo->mipi.mode, tmp,
		&(pinfo->mipi.dst_format));
	if (rc) {
		pr_debug("%s: problem determining dst format. Set Default\n",
			__func__);
		pinfo->mipi.dst_format =
			DSI_VIDEO_DST_FORMAT_RGB888;
	}
	pdest = of_get_property(np,
		"qcom,mdss-dsi-panel-destination", NULL);

	if (pdest) {
		if (strlen(pdest) != 9) {
			pr_err("%s: Unknown pdest specified\n", __func__);
			return -EINVAL;
		}
		if (!strcmp(pdest, "display_1"))
			pinfo->pdest = DISPLAY_1;
		else if (!strcmp(pdest, "display_2"))
			pinfo->pdest = DISPLAY_2;
		else {
			pr_debug("%s: pdest not specified. Set Default\n",
								__func__);
			pinfo->pdest = DISPLAY_1;
		}
	} else {
		pr_err("%s: pdest not specified\n", __func__);
		return -EINVAL;
	}
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-front-porch", &tmp);
	pinfo->lcdc.h_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-back-porch", &tmp);
	pinfo->lcdc.h_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-pulse-width", &tmp);
	pinfo->lcdc.h_pulse_width = (!rc ? tmp : 2);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-sync-skew", &tmp);
	pinfo->lcdc.hsync_skew = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-back-porch", &tmp);
	pinfo->lcdc.v_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-front-porch", &tmp);
	pinfo->lcdc.v_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-pulse-width", &tmp);
	pinfo->lcdc.v_pulse_width = (!rc ? tmp : 2);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-underflow-color", &tmp);
	pinfo->lcdc.underflow_clr = (!rc ? tmp : 0xff);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-border-color", &tmp);
	pinfo->lcdc.border_clr = (!rc ? tmp : 0);
	pinfo->bklt_ctrl = UNKNOWN_CTRL;
	data = of_get_property(np, "qcom,mdss-dsi-bl-pmic-control-type", NULL);
	if (data) {
		if (!strncmp(data, "bl_ctrl_wled", 12)) {
			led_trigger_register_simple("bkl-trigger",
				&bl_led_trigger);
			pr_debug("%s: SUCCESS-> WLED TRIGGER register\n",
				__func__);
			ctrl_pdata->bklt_ctrl = BL_WLED;
		} else if (!strncmp(data, "bl_ctrl_pwm", 11)) {
			ctrl_pdata->bklt_ctrl = BL_PWM;
			rc = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-pwm-frequency", &tmp);
			if (rc) {
				pr_err("%s:%d, Error, panel pwm_period\n",
						__func__, __LINE__);
				return -EINVAL;
			}
			ctrl_pdata->pwm_period = tmp;
			rc = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-bank-select", &tmp);
			if (rc) {
				pr_err("%s:%d, Error, dsi lpg channel\n",
						__func__, __LINE__);
				return -EINVAL;
			}
			ctrl_pdata->pwm_lpg_chan = tmp;
			tmp = of_get_named_gpio(np,
				"qcom,mdss-dsi-pwm-gpio", 0);
			ctrl_pdata->pwm_pmic_gpio = tmp;
		} else if (!strncmp(data, "bl_ctrl_dcs", 11)) {
			ctrl_pdata->bklt_ctrl = BL_DCS_CMD;
		}
	}
	rc = of_property_read_u32(np, "qcom,mdss-brightness-max-level", &tmp);
	pinfo->brightness_max = (!rc ? tmp : MDSS_MAX_BL_BRIGHTNESS);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-min-level", &tmp);
	pinfo->bl_min = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-max-level", &tmp);
	pinfo->bl_max = (!rc ? tmp : 255);
	ctrl_pdata->bklt_max = pinfo->bl_max;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-interleave-mode", &tmp);
	pinfo->mipi.interleave_mode = (!rc ? tmp : 0);

	pinfo->mipi.vsync_enable = of_property_read_bool(np,
		"qcom,mdss-dsi-te-check-enable");
	pinfo->mipi.hw_vsync_mode = of_property_read_bool(np,
		"qcom,mdss-dsi-te-using-te-pin");

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-h-sync-pulse", &tmp);
	pinfo->mipi.pulse_mode_hsa_he = (!rc ? tmp : false);

	pinfo->mipi.hfp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hfp-power-mode");
	pinfo->mipi.hsa_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hsa-power-mode");
	pinfo->mipi.hbp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hbp-power-mode");
	pinfo->mipi.bllp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-bllp-power-mode");
	pinfo->mipi.eof_bllp_power_stop = of_property_read_bool(
		np, "qcom,mdss-dsi-bllp-eof-power-mode");
	pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_PULSE;
	data = of_get_property(np, "qcom,mdss-dsi-traffic-mode", NULL);
	if (data) {
		if (!strcmp(data, "non_burst_sync_event"))
			pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_EVENT;
		else if (!strcmp(data, "burst_mode"))
			pinfo->mipi.traffic_mode = DSI_BURST_MODE;
	}
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-dcs-command", &tmp);
	pinfo->mipi.insert_dcs_cmd =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-v-sync-continue-lines", &tmp);
	pinfo->mipi.wr_mem_continue =
			(!rc ? tmp : 0x3c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-v-sync-rd-ptr-irq-line", &tmp);
	pinfo->mipi.wr_mem_start =
			(!rc ? tmp : 0x2c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-pin-select", &tmp);
	pinfo->mipi.te_sel =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-virtual-channel-id", &tmp);
	pinfo->mipi.vc = (!rc ? tmp : 0);
	pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RGB;
	data = of_get_property(np, "mdss-dsi-color-order", NULL);
	if (data) {
		if (!strcmp(data, "rgb_swap_rbg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RBG;
		else if (!strcmp(data, "rgb_swap_bgr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BGR;
		else if (!strcmp(data, "rgb_swap_brg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BRG;
		else if (!strcmp(data, "rgb_swap_grb"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GRB;
		else if (!strcmp(data, "rgb_swap_gbr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GBR;
	}
	pinfo->mipi.data_lane0 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-0-state");
	pinfo->mipi.data_lane1 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-1-state");
	pinfo->mipi.data_lane2 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-2-state");
	pinfo->mipi.data_lane3 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-3-state");

	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-pre", &tmp);
	pinfo->mipi.t_clk_pre = (!rc ? tmp : 0x24);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-post", &tmp);
	pinfo->mipi.t_clk_post = (!rc ? tmp : 0x03);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-stream", &tmp);
	pinfo->mipi.stream = (!rc ? tmp : 0);

	data = of_get_property(np, "qcom,mdss-dsi-panel-mode-gpio-state", NULL);
	if (data) {
		if (!strcmp(data, "high"))
			pinfo->mode_gpio_state = MODE_GPIO_HIGH;
		else if (!strcmp(data, "low"))
			pinfo->mode_gpio_state = MODE_GPIO_LOW;
	} else {
		pinfo->mode_gpio_state = MODE_GPIO_NOT_VALID;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-framerate", &tmp);
	pinfo->mipi.frame_rate = (!rc ? tmp : 60);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-clockrate", &tmp);
	pinfo->clk_rate = (!rc ? tmp : 0);
	data = of_get_property(np, "qcom,mdss-dsi-panel-timings", &len);
	if ((!data) || (len != 12)) {
		pr_err("%s:%d, Unable to read Phy timing settings",
		       __func__, __LINE__);
		goto error;
	}
	for (i = 0; i < len; i++)
		pinfo->mipi.dsi_phy_db.timing[i] = data[i];

	pinfo->mipi.lp11_init = of_property_read_bool(np,
					"qcom,mdss-dsi-lp11-init");
	rc = of_property_read_u32(np, "qcom,mdss-dsi-init-delay-us", &tmp);
	pinfo->mipi.init_delay = (!rc ? tmp : 0);

	mdss_dsi_parse_fbc_params(np, pinfo);

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.mdp_trigger),
		"qcom,mdss-dsi-mdp-trigger");

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.dma_trigger),
		"qcom,mdss-dsi-dma-trigger");

	mdss_dsi_parse_lane_swap(np, &(pinfo->mipi.dlane_swap));

	mdss_dsi_parse_reset_seq(np, pinfo->rst_seq, &(pinfo->rst_seq_len),
		"qcom,mdss-dsi-reset-sequence");

	mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->on_cmds,
		"qcom,mdss-dsi-on-command", "qcom,mdss-dsi-on-command-state");

	mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->off_cmds,
		"qcom,mdss-dsi-off-command", "qcom,mdss-dsi-off-command-state");

	#if defined(CONFIG_LGE_MIPI_TOVIS_VIDEO_540P_PANEL) || defined(CONFIG_FB_MSM_MIPI_TIANMA_VIDEO_QHD_PT_PANEL)
	mdss_dsi_parse_dcs_cmds(np, &lge_display_on_cmds,
		"lge,display-on-cmds", "qcom,mdss-dsi-on-command-state");

	mdss_dsi_parse_dcs_cmds(np, &lge_sleep_in_cmds,
		"lge,sleep-in-cmds", "qcom,mdss-dsi-off-command-state");
	#endif

#ifdef CONFIG_FB_MSM_MIPI_LGIT_LH470WX1_VIDEO_HD_PT_PANEL
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 == hw_rev)
#endif
    {
		pr_debug("%s:%d   lge_display_on_cmds \n", __func__, __LINE__);
		mdss_dsi_parse_dcs_cmds(np, &lge_display_on_cmds,
			"lge,display-on-cmds", "qcom,mdss-dsi-on-command-state");

	mdss_dsi_parse_dcs_cmds(np, &lge_display_power_setting,
		"lge,display-power-setting", "qcom,mdss-dsi-on-command-state");

		mdss_dsi_parse_dcs_cmds(np, &lge_display_off_cmds,
			"lge,display-off-cmds", "qcom,mdss-dsi-off-command-state");
	}
#endif

#if defined(CONFIG_FB_MSM_MIPI_TOVIS_LM570HN1A_VIDEO_HD_PT_PANEL)
#if defined(CONFIG_MACH_MSM8926_X10_VZW) || defined(CONFIG_MACH_MSM8926_B2L_ATT) || defined(CONFIG_MACH_MSM8926_B2LN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_ATT) || defined(CONFIG_MACH_MSM8926_JAGN_KR) || defined(CONFIG_MACH_MSM8926_JAGNM_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GSS_GLOBAL_COM) || defined(CONFIG_MACH_MSM8226_JAG3GDS_GLOBAL_COM)
	if(HW_REV_0 != hw_rev)
#endif
    {
		mdss_dsi_parse_dcs_cmds(np, &lge_display_on_cmds,
			"lge,display-on-cmds", "qcom,mdss-dsi-on-command-state");

		mdss_dsi_parse_dcs_cmds(np, &lge_sleep_out_cmds,
			"lge,sleep-out-cmds", "qcom,mdss-dsi-on-command-state");

		mdss_dsi_parse_dcs_cmds(np, &lge_color_cmds,
			"lge,color-cmds", "qcom,mdss-dsi-on-command-state");

		mdss_dsi_parse_dcs_cmds(np, &lge_display_off_cmds,
			"lge,display-off-cmds", "qcom,mdss-dsi-off-command-state");

		mdss_dsi_parse_dcs_cmds(np, &lge_sleep_in_cmds,
			"lge,sleep-in-cmds", "qcom,mdss-dsi-off-command-state");
	}
#endif

#if defined(CONFIG_LGE_MIPI_DSI_LGD_LVDS_WXGA)

	//Already Done qcom,lcd_en-gpio setting

	ctrl_pdata->lcd_stby_gpio = of_get_named_gpio(np, "qcom,lcd_stby-gpio", 0);
	if(!gpio_is_valid(ctrl_pdata->lcd_stby_gpio)) {
		pr_err("%s: lcd_stby_gpio not specified\n" , __func__);
		goto error;
	}

	// Already Done lcd_rst-gpio setting

	if( hw_rev == HW_REV_0 ){		//need to check it
		ctrl_pdata->bl_en_gpio = of_get_named_gpio(np, "qcom,bl_en-gpio", 0);
		if(!gpio_is_valid(ctrl_pdata->bl_en_gpio)) {
			pr_err("%s: bl_en_gpio not specified\n" , __func__);
			goto error;
		}

		ctrl_pdata->bl_pwm_gpio = of_get_named_gpio(np, "qcom,bl_pwm-gpio", 0);
		if(!gpio_is_valid(ctrl_pdata->bl_pwm_gpio)) {
			pr_err("%s: bl_pwm_gpio not specified\n" , __func__);
			goto error;
		}

		ctrl_pdata->bl_vled_gpio = of_get_named_gpio(np, "qcom,bl_vled-gpio", 0);
		if(!gpio_is_valid(ctrl_pdata->bl_vled_gpio)) {
			pr_err("%s: bl_vled_gpio not specified\n" , __func__);
			goto error;
	}
}
#elif defined (CONFIG_LGE_MIPI_DSI_LGD_NT35521_WXGA)
	ctrl_pdata->lcd_dsv_enp_gpio = of_get_named_gpio(np, "qcom,lcd_dsv_enp-gpio", 0);
	if(!gpio_is_valid(ctrl_pdata->lcd_dsv_enp_gpio)) {
		pr_err("%s: lcd_dsv_enp_gpio not specified\n" , __func__);
		goto error;
	}

	ctrl_pdata->lcd_dsv_enn_gpio = of_get_named_gpio(np, "qcom,lcd_dsv_enn-gpio", 0);
	if(!gpio_is_valid(ctrl_pdata->lcd_dsv_enn_gpio)) {
		pr_err("%s: lcd_dsv_enn_gpio not specified\n" , __func__);
		goto error;
	}
#endif
#ifdef CONFIG_FB_MSM_MIPI_LGD_LH500WX9_VIDEO_HD_PT_PANEL
	printk("cmd_parsing\n");

	mdss_dsi_parse_dcs_cmds(np, &lge_sleep_out_cmds,
		"lge,sleep-out-cmds", "qcom,mdss-dsi-on-command-state");
	printk("lge_sleep_out_cmds.cmd_cnt = %d\n",lge_sleep_out_cmds.cmd_cnt);

	mdss_dsi_parse_dcs_cmds(np, &lge_display_on_cmds,
		"lge,display-on-cmds", "qcom,mdss-dsi-on-command-state");
	printk("lge_display_on_cmds.cmd_cnt = %d\n",lge_display_on_cmds.cmd_cnt);

	mdss_dsi_parse_dcs_cmds(np, &lge_display_off_cmds,
		"lge,display-off-cmds", "qcom,mdss-dsi-off-command-state");
	printk("lge_display_off_cmds.cmd_cnt = %d\n",lge_display_off_cmds.cmd_cnt);

	mdss_dsi_parse_dcs_cmds(np, &lge_sleep_in_cmds,
		"lge,sleep-in-cmds", "qcom,mdss-dsi-off-command-state");
	printk("lge_sleep_in_cmds.cmd_cnt = %d\n",lge_sleep_in_cmds.cmd_cnt);

	mdss_dsi_parse_dcs_cmds(np, &lge_display_on_cmds_2,
		"lge,VSS-HSS-PPS-cmds", "qcom,mdss-dsi-on-command-state");
	printk("lge_display_on_cmds_2.cmd_cnt = %d\n",lge_display_on_cmds_2.cmd_cnt);
#endif

	return 0;

error:
	return -EINVAL;
}

int mdss_dsi_panel_init(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	bool cmd_cfg_cont_splash)
{
	int rc = 0;
	static const char *panel_name;
	bool cont_splash_enabled;
	bool partial_update_enabled;

	if (!node) {
		pr_err("%s: no panel node\n", __func__);
		return -ENODEV;
	}

	pr_debug("%s:%d\n", __func__, __LINE__);
	panel_name = of_get_property(node, "qcom,mdss-dsi-panel-name", NULL);
	if (!panel_name)
		pr_info("%s:%d, Panel name not specified\n",
						__func__, __LINE__);
	else
		pr_info("%s: Panel Name = %s\n", __func__, panel_name);

	rc = mdss_panel_parse_dt(node, ctrl_pdata);
	if (rc) {
		pr_err("%s:%d panel dt parse failed\n", __func__, __LINE__);
		return rc;
	}

	if (cmd_cfg_cont_splash)
		cont_splash_enabled = of_property_read_bool(node,
				"qcom,cont-splash-enabled");
	else
		cont_splash_enabled = false;
	if (!cont_splash_enabled) {
		pr_info("%s:%d Continuous splash flag not found.\n",
				__func__, __LINE__);
		ctrl_pdata->panel_data.panel_info.cont_splash_enabled = 0;
	} else {
		pr_info("%s:%d Continuous splash flag enabled.\n",
				__func__, __LINE__);

		ctrl_pdata->panel_data.panel_info.cont_splash_enabled = 1;
	}

	partial_update_enabled = of_property_read_bool(node,
						"qcom,partial-update-enabled");
	if (partial_update_enabled) {
		pr_info("%s:%d Partial update enabled.\n", __func__, __LINE__);
		ctrl_pdata->panel_data.panel_info.partial_update_enabled = 1;
		ctrl_pdata->partial_update_fnc = mdss_dsi_panel_partial_update;
	} else {
		pr_info("%s:%d Partial update disabled.\n", __func__, __LINE__);
		ctrl_pdata->panel_data.panel_info.partial_update_enabled = 0;
		ctrl_pdata->partial_update_fnc = NULL;
	}

	ctrl_pdata->on = mdss_dsi_panel_on;
	ctrl_pdata->off = mdss_dsi_panel_off;
	ctrl_pdata->panel_data.set_backlight = mdss_dsi_panel_bl_ctrl;

	return 0;
}
