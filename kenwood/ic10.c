/*
 *  Hamlib Kenwood backend - IC-10 interface for:
 *  			TS-940, TS-811, TS-711, TS-440, and R-5000
 *
 *  Copyright (c) 2000-2004 by Stephane Fillod and others
 *
 *	$Id: ic10.c,v 1.3 2004-06-13 12:38:41 fillods Exp $
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <math.h>

#include "hamlib/rig.h"
#include "serial.h"
#include "misc.h"
#include "register.h"

#include "kenwood.h"
#include "ic10.h"

/*
 * modes in use by the "MD" command
 */
#define MD_NONE	'0'
#define MD_LSB	'1'
#define MD_USB	'2'
#define MD_CW	'3'
#define MD_FM	'4'
#define MD_AM	'5'
#define MD_FSK	'6'


/**
 * ic10_transaction
 * Assumes rig!=NULL rig->state!=NULL rig->caps!=NULL
**/ 
int ic10_transaction (RIG *rig, const char *cmd, int cmd_len, char *data, int *data_len)
{
	int retval;
	struct rig_state *rs;

	rs = &rig->state;

	serial_flush(&rs->rigport);

	retval = write_block(&rs->rigport, cmd, cmd_len);
	if (retval != RIG_OK)
		return retval;

	if (!data || !data_len)
		return 0;

	*data_len = read_string(&rs->rigport, data, 50, EOM_KEN, 1);
    
    return RIG_OK;
}

/* 
 * Get the anwser of IF command, with retry handling
 */
static int get_ic10_if (RIG *rig, char *data)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	int i, data_len, retval=!RIG_OK;

	for (i=0; retval!=RIG_OK && i < rig->caps->retry; i++) {
		data_len = 38;
		retval = ic10_transaction (rig, "IF;", 3, data, &data_len);
		if (retval != RIG_OK)
			continue;

		if (retval == RIG_OK && 
				(data_len < priv->if_len ||
				 data[0] != 'I' || data[1] != 'F')) {
			rig_debug(RIG_DEBUG_WARN,"%s: unexpected answer %s, len=%d\n",
				__FUNCTION__, data, data_len);		
			retval = -RIG_ERJCTED;
		}
	}

	return retval;
}


/*
 * ic10_set_vfo
 * Assumes rig!=NULL
 */
int ic10_set_vfo(RIG *rig, vfo_t vfo)
{
	unsigned char cmdbuf[6], ackbuf[16];
	int cmd_len, retval, ack_len;
	char vfo_function;

	switch (vfo) {
	case RIG_VFO_VFO:
	case RIG_VFO_A: vfo_function = '0'; break;
	case RIG_VFO_B: vfo_function = '1'; break;
	case RIG_VFO_MEM: vfo_function = '2'; break;
	case RIG_VFO_CURR: return RIG_OK;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported VFO %d\n",
						__FUNCTION__, vfo);
		return -RIG_EINVAL;
	}

	cmd_len = sprintf(cmdbuf, "FN%c;", vfo_function);

	retval = ic10_transaction (rig, cmdbuf, cmd_len, ackbuf, &ack_len);
	return retval;
}


/*
 * ic10_get_vfo
 * Assumes rig!=NULL, !vfo
 */
int ic10_get_vfo(RIG *rig, vfo_t *vfo)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	unsigned char vfobuf[50], c;
	int retval;


	/* query RX VFO */
	retval = get_ic10_if(rig, vfobuf);
	if (retval != RIG_OK)
		return retval;


	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	c = vfobuf[priv->if_len - 3];
	switch (c) {
	case '0': *vfo = RIG_VFO_A; break;
	case '1': *vfo = RIG_VFO_B; break;
	case '2': *vfo = RIG_VFO_MEM; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported VFO %c\n",
					__FUNCTION__, c);
		return -RIG_EPROTO;
	}
	return RIG_OK;
}


int ic10_set_split_vfo(RIG *rig, vfo_t vfo, split_t split, vfo_t txvfo)
{
	unsigned char ackbuf[16];
	int ack_len;

	return ic10_transaction (rig, split==RIG_SPLIT_ON? "SP1;":"SP0;", 4, 
					ackbuf, &ack_len);
}


int ic10_get_split_vfo(RIG *rig, vfo_t vfo, split_t *split, vfo_t *txvfo)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	unsigned char infobuf[50];
	int retval;

	retval = get_ic10_if (rig, infobuf);
	if (retval != RIG_OK)
		return retval;

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	*split = infobuf[priv->if_len-1] == '0' ? RIG_SPLIT_OFF : RIG_SPLIT_ON;

	return RIG_OK;
}


/*
 * ic10_get_mode
 * Assumes rig!=NULL, !vfo
 */
int ic10_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	unsigned char modebuf[50],c;
	int retval;

	/* query RX VFO */
	retval = get_ic10_if (rig, modebuf);
	if (retval != RIG_OK)
		return retval;

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	c = modebuf[priv->if_len-4];
	switch (c) {
	case MD_CW  :	*mode = RIG_MODE_CW; break;
	case MD_USB :	*mode = RIG_MODE_USB; break;
	case MD_LSB :	*mode = RIG_MODE_LSB; break;
	case MD_FM  :	*mode = RIG_MODE_FM; break;
	case MD_AM  :	*mode = RIG_MODE_AM; break;
	case MD_FSK :	*mode = RIG_MODE_RTTY; break;
	case MD_NONE:	*mode = RIG_MODE_NONE; break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode '%c'\n",
				__FUNCTION__, c);
		return -RIG_EINVAL;
	}

	*width = rig_passband_normal(rig, *mode);

	return RIG_OK;
}


/*
 * ic10_set_mode
 * Assumes rig!=NULL
 */
int ic10_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
	unsigned char modebuf[6], ackbuf[16];
	int mode_len, ack_len, retval;
	char mode_letter;

	switch (mode) {
	case RIG_MODE_LSB  :	mode_letter = MD_LSB; break;
	case RIG_MODE_USB  :	mode_letter = MD_USB; break;
	case RIG_MODE_CW   :	mode_letter = MD_CW; break;
	case RIG_MODE_FM   :	mode_letter = MD_FM; break;
	case RIG_MODE_AM   :	mode_letter = MD_AM; break;
	case RIG_MODE_RTTY :	mode_letter = MD_FSK; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode %d\n", 
				__FUNCTION__,mode);
		return -RIG_EINVAL;
	}
	
	mode_len = sprintf(modebuf,"MD%c;", mode_letter);
	retval = ic10_transaction (rig, modebuf, mode_len, ackbuf, &ack_len);

	return retval;
}


/*
 * ic10_get_freq
 * Assumes rig!=NULL, freq!=NULL
 */
int ic10_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
	unsigned char infobuf[50];
	int retval;
	long long f;

       	if (vfo != RIG_VFO_CURR) {
		/* targeted freq retrieval */
		return kenwood_get_freq(rig, vfo, freq);
	}

	retval = get_ic10_if (rig, infobuf);
	if (retval != RIG_OK)
		return retval;

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	infobuf[13] = '\0';
	sscanf(infobuf+2, "%011lld", &f);
	*freq = (freq_t)f;

	return RIG_OK;
}


/*
 * ic10_set_freq
 * Assumes rig!=NULL
 */
int ic10_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
	unsigned char freqbuf[16], ackbuf[16];
	int freq_len, ack_len, retval;
	char vfo_letter;
	vfo_t	tvfo;

   	if(vfo==RIG_VFO_CURR)
		tvfo=rig->state.current_vfo;
   	else
		tvfo=vfo;
                                                                                         
	switch (tvfo) {
	case RIG_VFO_A: vfo_letter = 'A'; break;
	case RIG_VFO_B: vfo_letter = 'B'; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported VFO %d\n",
				__FUNCTION__,vfo);
		return -RIG_EINVAL;
	}
	
	freq_len = sprintf(freqbuf,"F%c%011Ld;", vfo_letter, (long long)freq);
	retval = ic10_transaction (rig, freqbuf, freq_len, ackbuf, &ack_len);

	return retval;
}


/*
 * ic10_set_ant
 * Assumes rig!=NULL
 */
int ic10_set_ant(RIG *rig, vfo_t vfo, ant_t ant)
{
	unsigned char buf[6], ackbuf[16];
	int len, ack_len, retval;

	len = sprintf(buf,"AN%c;", ant==RIG_ANT_1?'1':'2');

	retval = ic10_transaction(rig, buf, len, ackbuf, &ack_len);

	return retval;
}


/*
 * ic10_get_ant
 * Assumes rig!=NULL, ptt!=NULL
 */
int ic10_get_ant(RIG *rig, vfo_t vfo, ant_t *ant)
{
	unsigned char infobuf[50];
	int info_len, retval;

	info_len = 4;
	retval = ic10_transaction (rig, "AN;", 3, infobuf, &info_len);
	if (retval != RIG_OK)
		return retval;

	if (info_len < 4 || infobuf[0] != 'A' || infobuf[1] != 'N') {
		rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
				__FUNCTION__,info_len);
		return -RIG_ERJCTED;
	}

	*ant = infobuf[2] == '1' ? RIG_ANT_1 : RIG_ANT_2;

	return RIG_OK;
}


/*
 * ic10_get_ptt
 * Assumes rig!=NULL, ptt!=NULL
 */
int ic10_get_ptt(RIG *rig, vfo_t vfo, ptt_t *ptt)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	unsigned char infobuf[50];
	int retval;

	retval = get_ic10_if (rig, infobuf);
	if (retval != RIG_OK)
		return retval;

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	*ptt = infobuf[priv->if_len-5] == '0' ? RIG_PTT_OFF : RIG_PTT_ON;

	return RIG_OK;
}


/*
 * ic10_set_ptt
 * Assumes rig!=NULL
 */
int ic10_set_ptt(RIG *rig, vfo_t vfo, ptt_t ptt)
{
	unsigned char pttbuf[4], ackbuf[16];
	int ptt_len, ack_len, retval;
	char ptt_letter;

	switch (ptt) {
	case RIG_PTT_OFF: ptt_letter = 'R'; break;
	case RIG_PTT_ON : ptt_letter = 'T'; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported PTT %d\n",
				__FUNCTION__,ptt);
		return -RIG_EINVAL;
	}
				
	ptt_len = sprintf(pttbuf,"%cX;", ptt_letter);
	retval = ic10_transaction (rig, pttbuf, ptt_len, ackbuf, &ack_len);

	return retval;
}


/*
 * ic10_get_mem
 * Assumes rig!=NULL
 */
int ic10_get_mem(RIG *rig, vfo_t vfo, int *ch)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	unsigned char membuf[50];
	int retval;

	retval = get_ic10_if (rig, membuf);
	if (retval != RIG_OK)
		return retval;

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */
	membuf[priv->if_len-5] = '\0';
	*ch = atoi(membuf+priv->if_len-7);

	return RIG_OK;
}


/*
 * ic10_set_mem
 * Assumes rig!=NULL
 */
int ic10_set_mem(RIG *rig, vfo_t vfo, int ch)
{
	unsigned char membuf[4], ackbuf[16];
	int mem_len, ack_len, retval;

	mem_len = sprintf(membuf, "MC %02d;", ch);
	
	retval = ic10_transaction (rig, membuf, mem_len, ackbuf, &ack_len);

	return retval;
}


int ic10_get_channel(RIG *rig, channel_t *chan)
{
	char membuf[16],infobuf[32];
	int retval,info_len,len;
	long long freq;

	len = sprintf(membuf,"MR0 %02d;",chan->channel_num);
	info_len = 24;
	retval = ic10_transaction(rig, membuf, len, infobuf, &info_len);
	if (retval != RIG_OK && info_len > 17)
		return retval;

	/* MRn rrggmmmkkkhhhdz    ; */
	switch (infobuf[17]) {
	case MD_CW  :	chan->mode = RIG_MODE_CW; break;
	case MD_USB :	chan->mode = RIG_MODE_USB; break;
	case MD_LSB :	chan->mode = RIG_MODE_LSB; break;
	case MD_FM  :	chan->mode = RIG_MODE_FM; break;
	case MD_AM  :	chan->mode = RIG_MODE_AM; break;
	case MD_FSK :	chan->mode = RIG_MODE_RTTY; break;
	case MD_NONE:	chan->mode = RIG_MODE_NONE; break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode '%c'\n",
				__FUNCTION__,infobuf[17]);
		return -RIG_EINVAL;
	}
	chan->width = rig_passband_normal(rig, chan->mode);

	/*  infobuf[17] = ' '; */
	infobuf[17] = '\0';
	sscanf(infobuf+6, "%011lld", &freq);
	chan->freq = (freq_t)freq;
	chan->vfo=RIG_VFO_MEM;

	/* TX VFO (Split channel only) */
	len = sprintf(membuf,"MR1 %02d;",chan->channel_num);
	info_len = 24;
	retval = ic10_transaction(rig, membuf, len, infobuf, &info_len);
	if (retval == RIG_OK && info_len > 17) {

		/* MRn rrggmmmkkkhhhdz    ; */
		switch (infobuf[17]) {
		case MD_CW  :	chan->tx_mode = RIG_MODE_CW; break;
		case MD_USB :	chan->tx_mode = RIG_MODE_USB; break;
		case MD_LSB :	chan->tx_mode = RIG_MODE_LSB; break;
		case MD_FM  :	chan->tx_mode = RIG_MODE_FM; break;
		case MD_AM  :	chan->tx_mode = RIG_MODE_AM; break;
		case MD_FSK :	chan->tx_mode = RIG_MODE_RTTY; break;
		case MD_NONE:	chan->tx_mode = RIG_MODE_NONE; break;
		default:
			rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode '%c'\n",
					__FUNCTION__,infobuf[17]);
			return -RIG_EINVAL;
		}
		chan->tx_width = rig_passband_normal(rig, chan->tx_mode);

		/*  infobuf[17] = ' '; */
		infobuf[17] = '\0';

		sscanf(infobuf+6, "%011lld", &freq);
		chan->tx_freq = (freq_t)freq;
	}

	return RIG_OK;
}


int ic10_set_channel(RIG *rig, const channel_t *chan)
{
	char membuf[32],ackbuf[32];
	int retval,ack_len,len,md;
	long long freq;

	freq = chan->freq;
	switch (chan->mode) {
	case RIG_MODE_CW  :	md = MD_CW; break;
	case RIG_MODE_USB :	md = MD_USB; break;
	case RIG_MODE_LSB :	md = MD_LSB; break;
	case RIG_MODE_FM  :	md = MD_FM; break;
	case RIG_MODE_AM  :	md = MD_AM; break;
	case RIG_MODE_RTTY:	md = MD_FSK; break;
	case RIG_MODE_NONE:	md = MD_NONE; break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode %d\n",
				__FUNCTION__,chan->mode);
		return -RIG_EINVAL;
	}

	/* MWnxrrggmmmkkkhhhdzxxxx; */
	len = sprintf(membuf,"MW0 %02d%011lld%c0    ;",
			chan->channel_num,
			freq,
			md
			);
	retval = ic10_transaction(rig, membuf, len, ackbuf, &ack_len);
	if (retval != RIG_OK)
		return retval;

	/* TX VFO (Split channel only) */
	freq = chan->tx_freq;
	switch (chan->tx_mode) {
	case RIG_MODE_CW:	md = MD_CW; break;
	case RIG_MODE_USB:	md = MD_USB; break;
	case RIG_MODE_LSB:	md = MD_LSB; break;
	case RIG_MODE_FM:	md = MD_FM; break;
	case RIG_MODE_AM:	md = MD_AM; break;
	case RIG_MODE_RTTY:	md = MD_FSK; break;
	case RIG_MODE_NONE:	md = MD_NONE; break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode %d\n",
				__FUNCTION__,chan->tx_mode);
		return -RIG_EINVAL;
	}

	/* MWnxrrggmmmkkkhhhdzxxxx; */
	len = sprintf(membuf,"MW1 %02d%011lld%c0    ;",
			chan->channel_num,
			freq,
			md
			);
	retval = ic10_transaction(rig, membuf, len, ackbuf, &ack_len);

	return RIG_OK;
}


/*
 * ic10_get_func
 * Assumes rig!=NULL, val!=NULL
 */
int ic10_get_func(RIG *rig, vfo_t vfo, setting_t func, int *status)
{
	unsigned char cmdbuf[6],fctbuf[50];
	int cmdlen, fct_len, retval;

	fct_len = 4;
	switch (func) {
	case RIG_FUNC_LOCK: cmdlen = sprintf(cmdbuf,"LK;"); break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: Unsupported get_func %#x",
				__FUNCTION__,func);
		return -RIG_EINVAL;
	}

	retval = ic10_transaction (rig, cmdbuf, cmdlen, fctbuf, &fct_len);
	if (retval != RIG_OK)
		return retval;

	if (fct_len != 4) {
		rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
				__FUNCTION__,fct_len);
		return -RIG_ERJCTED;
	}

	*status = fctbuf[2] == '0' ? 0 : 1;

	return RIG_OK;
}


/*
 * ic10_set_func
 * Assumes rig!=NULL, val!=NULL
 */
int ic10_set_func(RIG *rig, vfo_t vfo, setting_t func, int status)
{
	unsigned char cmdbuf[4], fctbuf[16], ackbuf[16];
	int cmdlen, fct_len, ack_len;

	switch (func) {
	case RIG_FUNC_LOCK: cmdlen = sprintf(cmdbuf,"LK"); break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: Unsupported set_func %#x",
				__FUNCTION__,func);
		return -RIG_EINVAL;
	}

	fct_len = sprintf(fctbuf,"%s%c;", cmdbuf, status==0?'0':'1');
	return ic10_transaction (rig, fctbuf, fct_len, ackbuf, &ack_len);

	return RIG_OK;
}


/*
 * ic10_set_parm
 * Assumes rig!=NULL
 */
int ic10_set_parm(RIG *rig, setting_t parm, value_t val)
{
	char cmdbuf[50];
	int cmd_len;
	int hours;
	int minutes;
	int seconds;

	switch (parm) {
	case RIG_PARM_TIME:
		minutes = val.i/60;
		hours = minutes/60;
		seconds = val.i-(minutes*60);
		minutes = minutes%60;
		cmd_len = sprintf(cmdbuf, "CK1%02d%02d%02d;", hours, minutes, seconds);
		return ic10_transaction (rig, cmdbuf, cmd_len, NULL, NULL);
		break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: Unsupported set_parm %d\n", 
				__FUNCTION__,parm);
		return -RIG_EINVAL;
	}
		
	return RIG_OK;
}


/*
 * ic10_get_parm
 * Assumes rig!=NULL, val!=NULL
 */
int ic10_get_parm(RIG *rig, setting_t parm, value_t *val)
{
	int retval, lvl_len, i;
	char lvlbuf[50];

	switch (parm) {
	case RIG_PARM_TIME:
		lvl_len = 10;
		retval = ic10_transaction (rig, "CK1;", 4, lvlbuf, &lvl_len);
		if (retval != RIG_OK)
			return retval;
	  
		/* "CK1hhmmss;"*/
		if (lvl_len != 10) {
			rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
					__FUNCTION__,lvl_len);
			return -RIG_ERJCTED;
		}

		/* convert ASCII to numeric 0..9 */
		for (i=3; i<9; i++) {
			lvlbuf[i] -= '0';
		}
		val->i = ((10*lvlbuf[3] + lvlbuf[4])*60 +	/* hours */
					10*lvlbuf[5] + lvlbuf[6])*60 +	/* minutes */
					10*lvlbuf[7] + lvlbuf[8];	/* seconds */
		break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: Unsupported get_parm %d\n", 
				__FUNCTION__,parm);
		return -RIG_EINVAL;
	}

	return RIG_OK;
}


/*
 * ic10_set_powerstat
 * Assumes rig!=NULL
 */
int ic10_set_powerstat(RIG *rig, powerstat_t status)
{
	unsigned char pwrbuf[16], ackbuf[16];
	int pwr_len, ack_len;

	pwr_len = sprintf(pwrbuf,"PS%c;", status==RIG_POWER_ON?'1':'0');

	return ic10_transaction (rig, pwrbuf, pwr_len, ackbuf, &ack_len);
}


/*
 * ic10_get_powerstat
 * Assumes rig!=NULL, trn!=NULL
 */
int ic10_get_powerstat(RIG *rig, powerstat_t *status)
{
	unsigned char pwrbuf[50];
	int pwr_len, retval;

	pwr_len = 4;
	retval = ic10_transaction (rig, "PS;", 3, pwrbuf, &pwr_len);
	if (retval != RIG_OK)
		return retval;

	if (pwr_len != 4) {
		rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
				__FUNCTION__,pwr_len);
		return -RIG_ERJCTED;
	}
	*status = pwrbuf[2] == '0' ? RIG_POWER_OFF : RIG_POWER_ON;

	return RIG_OK;
}


/*
 * ic10_set_trn
 * Assumes rig!=NULL
 */
int ic10_set_trn(RIG *rig, int trn)
{
	unsigned char trnbuf[16], ackbuf[16];
	int trn_len, ack_len;

	trn_len = sprintf(trnbuf,"AI%c;", trn==RIG_TRN_RIG?'1':'0');

	return ic10_transaction (rig, trnbuf, trn_len, ackbuf, &ack_len);
}


/*
 * ic10_get_trn
 * Assumes rig!=NULL, trn!=NULL
 */
int ic10_get_trn(RIG *rig, int *trn)
{
	unsigned char trnbuf[50];
	int trn_len, retval;

	trn_len = 38;
	retval = ic10_transaction (rig, "AI;", 3, trnbuf, &trn_len);
	if (retval != RIG_OK)
		return retval;

	if (trn_len != 38) {
		rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
				__FUNCTION__,trn_len);
		return -RIG_ERJCTED;
	}
	*trn = trnbuf[2] != '0' ? RIG_TRN_RIG : RIG_TRN_OFF;

	return RIG_OK;
}


/*
 * ic10_vfo_op
 * Assumes rig!=NULL
 */
int ic10_vfo_op(RIG *rig, vfo_t vfo, vfo_op_t op)
{
	unsigned char *cmd, ackbuf[16];
	int ack_len;

	switch(op) {
	case RIG_OP_UP    :	cmd = "UP;"; break;
	case RIG_OP_DOWN  :	cmd = "DN;"; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported op %#x\n",
						__FUNCTION__,op);
		return -RIG_EINVAL;
	}

	return ic10_transaction (rig, cmd, 3, ackbuf, &ack_len);
}


/*
 * ic10_scan
 * Assumes rig!=NULL, val!=NULL
 */
int ic10_scan(RIG * rig, vfo_t vfo, scan_t scan, int ch)
{
	unsigned char ackbuf[16];
	int ack_len;

	return ic10_transaction (rig, scan==RIG_SCAN_STOP? "SC0;":"SC1;", 4, 
						ackbuf, &ack_len);
}


/*
 * ic10_get_info
 * Assumes rig!=NULL
 */
const char* ic10_get_info(RIG *rig)
{
	unsigned char firmbuf[50];
	int firm_len, retval;
								 
	firm_len = 6;
	retval = ic10_transaction (rig, "ID;", 3, firmbuf, &firm_len);
	if (retval != RIG_OK)
		return NULL;

	if (firm_len != 6) {
		rig_debug(RIG_DEBUG_ERR,"%s: wrong answer len=%d\n",
						__FUNCTION__,firm_len);
		return NULL;
	}

	switch (firmbuf[4]) {
	case '4': return "ID: TS-440S";
	case '5': return "ID: R-5000";
	default: return "ID: unknown";
	}
}


/*
 * ic10_decode_event is called by sa_sigio, when some asynchronous
 * data has been received from the rig.
 */
int ic10_decode_event (RIG *rig)
{
	struct kenwood_priv_caps *priv = (struct kenwood_priv_caps *)rig->caps->priv;
	char asyncbuf[128],c;
	int retval,async_len=128;
	vfo_t vfo;
	long long freq;
	rmode_t mode;
	ptt_t ptt;

	rig_debug(RIG_DEBUG_TRACE, "%s: called\n", __FUNCTION__);

	retval = ic10_transaction(rig, NULL, 0, asyncbuf, &async_len);
	if (retval != RIG_OK)
        	return retval;

	rig_debug(RIG_DEBUG_TRACE, "%s: Decoding message\n", __FUNCTION__);



    /* --------------------------------------------------------------------- */
	if (async_len<priv->if_len || asyncbuf[0] != 'I' || asyncbuf[1] != 'F') {

        	rig_debug(RIG_DEBUG_ERR, "%s: Unsupported transceive cmd '%s'\n",
			__FUNCTION__, asyncbuf);
	        return -RIG_ENIMPL;
	}

	/* IFggmmmkkkhhh snnnzrx yytdfcp */
	/* IFggmmmkkkhhhxxxxxrrrrrssxcctmfcp */

	c = asyncbuf[priv->if_len-3];
	switch (c) {
	case '0': vfo = RIG_VFO_A; break;
	case '1': vfo = RIG_VFO_B; break;
	case '2': vfo = RIG_VFO_MEM; break;
	default: 
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported VFO %c\n",
					__FUNCTION__, c);
		return -RIG_EPROTO;
	}

	c = asyncbuf[priv->if_len-4];
	switch (c) {
	case MD_CW  :	mode = RIG_MODE_CW; break;
	case MD_USB :	mode = RIG_MODE_USB; break;
	case MD_LSB :	mode = RIG_MODE_LSB; break;
	case MD_FM  :	mode = RIG_MODE_FM; break;
	case MD_AM  :	mode = RIG_MODE_AM; break;
	case MD_FSK :	mode = RIG_MODE_RTTY; break;
	case MD_NONE:	mode = RIG_MODE_NONE; break;
	default:
		rig_debug(RIG_DEBUG_ERR,"%s: unsupported mode '%c'\n",
				__FUNCTION__,c);
		return -RIG_EINVAL;
	}

	ptt = asyncbuf[priv->if_len-5] == '0' ? RIG_PTT_OFF : RIG_PTT_ON;

	asyncbuf[13] = '\0';
	sscanf(asyncbuf+2, "%011lld", &freq);
	
	/* Callback execution */
	if (rig->callbacks.vfo_event) {
    	rig->callbacks.vfo_event(rig, vfo, rig->callbacks.vfo_arg);
	}
	if (rig->callbacks.freq_event) {
		rig->callbacks.freq_event(rig, vfo, (freq_t)freq, rig->callbacks.freq_arg);
	}
	if (rig->callbacks.mode_event) {
		rig->callbacks.mode_event(rig, vfo, mode, RIG_PASSBAND_NORMAL, 
		rig->callbacks.mode_arg);
	}
	if (rig->callbacks.ptt_event) {
		rig->callbacks.ptt_event(rig, vfo, ptt, rig->callbacks.ptt_arg);
	}

	return RIG_OK;
}
