/*
 *  Copyright (C) 2003-2004 Pontus Fuchs, Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <net/iw_handler.h>
#include <linux/rtnetlink.h>

#include "iw_ndis.h"
#include "wpa_ndiswrapper.h"

static int freq_chan[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
			   2447, 2452, 2457, 2462, 2467, 2472, 2484 };

int ndis_set_essid(struct net_device *dev, struct iw_request_info *info,
		   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned int res, written, needed;
	struct essid_req req;

	memset(&req.essid, 0, sizeof(req.essid));
	
	if (wrqu->essid.flags == 0)
		req.len = 0;
	else
	{
		if (wrqu->essid.length > (IW_ESSID_MAX_SIZE + 1))
			return -EINVAL;

		req.len = wrqu->essid.length - 1;
		memcpy(&req.essid, extra, req.len);
	}
	
	handle->essid.flags = wrqu->essid.flags;
	handle->essid.length = req.len;
	memcpy(handle->essid.name, req.essid, req.len);
	res = dosetinfo(handle, NDIS_OID_ESSID, (char*)&req, sizeof(req),
			&written, &needed);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/500);
	res = dosetinfo(handle, NDIS_OID_ESSID, (char*)&req, sizeof(req),
			&written, &needed);
	if(res)
	{
		printk(KERN_ERR "%s: setting essid failed (%08X)\n",
		       dev->name, res); 
		return -EINVAL;
	}

	return 0;
}

static int ndis_get_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned int res, written, needed;
	struct essid_req req;

	TRACEENTER1();
	res = doquery(handle, NDIS_OID_ESSID, (char*)&req, sizeof(req),
		      &written, &needed);
	if(res)
	{
		printk(KERN_ERR "%s: getting essid failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	memcpy(extra, &req.essid, req.len);	
	extra[req.len] = 0;
	wrqu->essid.flags  = 1;
	wrqu->essid.length = req.len + 1;
	TRACEEXIT1(return 0);
}


int ndis_set_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	enum op_mode ndis_mode;
	int res;

	switch(wrqu->mode)
	{
	case IW_MODE_ADHOC:
		ndis_mode = NDIS_MODE_ADHOC;
		break;	
	case IW_MODE_INFRA:
		ndis_mode = NDIS_MODE_INFRA;
		break;	
	case IW_MODE_AUTO:
		ndis_mode = NDIS_MODE_AUTO;
		break;	
	default:
		return -EINVAL;
	}
	
	res = set_int(handle, NDIS_OID_MODE, ndis_mode);
	if(res)
	{
		printk(KERN_ERR "%s: setting operating mode failed (%08X)\n",
		       dev->name, res); 
		return -EINVAL;
	}

	return 0;
}

static int ndis_get_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	int ndis_mode, iw_mode;

	int res = query_int(handle, NDIS_OID_MODE, &ndis_mode);
	if(res)
	{
		printk(KERN_ERR "%s: getting operating mode failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	switch(ndis_mode)
	{
	case NDIS_MODE_ADHOC:
		iw_mode = IW_MODE_ADHOC;
		break;
	case NDIS_MODE_INFRA:
		iw_mode = IW_MODE_INFRA;
		break;
	case NDIS_MODE_AUTO:
		iw_mode = IW_MODE_AUTO;
		break;
	default:
		printk(KERN_ERR "%s: invalid operating mode (%u)\n",
		       dev->name, ndis_mode);
		return -1;
		break;
	}
	wrqu->mode = iw_mode;
	return 0;
}

const char *net_type_to_name(int net_type)
{
	static const char *net_names[] = {"IEEE 802.11FH", "IEEE 802.11b",
	              "IEEE 802.11a", "IEEE 802.11g"};
	static const char *unknown = "Unknown";

	if (net_type >= 0 &&
	    net_type < (sizeof(net_names)/sizeof(net_names[0])))
		return net_names[net_type];
	else
		return unknown;
}

static int ndis_get_name(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int network_type, res;
	
	res = query_int(handle, NDIS_OID_NETWORK_TYPE_IN_USE, &network_type);
	if (res)
		network_type = -1;

	strncpy(wrqu->name, net_type_to_name(network_type),
	        sizeof(wrqu->name) - 1);
	wrqu->name[sizeof(wrqu->name)-1] = 0;
	return 0;
}

static int ndis_get_freq(struct net_device *dev, struct iw_request_info *info,
                         union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res, written, needed;
	struct ndis_configuration req;

	res = doquery(handle, NDIS_OID_CONFIGURATION, (char*)&req, sizeof(req), &written, &needed);
	if(res)
	{
		printk(KERN_ERR "%s: getting configuration failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	memset(&(wrqu->freq), 0, sizeof(struct iw_freq));

	/* see comment in wireless.h above the "struct iw_freq"
	   definition for an explanation of this if
	   NOTE: 1000000 is due to the kHz
	*/
	if (req.ds_config > 1000000)
	{
		wrqu->freq.m = req.ds_config / 10;
		wrqu->freq.e = 1;
	}
	else
		wrqu->freq.m = req.ds_config;

	/* convert from kHz to Hz */
	wrqu->freq.e += 3;

	return 0;
}

static int ndis_set_freq(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res, written, needed;
	struct ndis_configuration req;

	res = doquery(handle, NDIS_OID_CONFIGURATION, (char *)&req,
				  sizeof(req), &written, &needed);
	if (res)
	{
		printk(KERN_ERR "%s: getting configuration failed (%08X)\n",
			   dev->name, res);
		return -EINVAL;
	}
	if (wrqu->freq.m < 1000 && wrqu->freq.e == 0)
	{
		if (wrqu->freq.m >= 1 &&
		    wrqu->freq.m <= (sizeof(freq_chan)/sizeof(freq_chan[0])))
			req.ds_config = freq_chan[wrqu->freq.m - 1] * 1000;
		else
			return -1;
	}
	else
	{
		int i;
		for (req.ds_config = wrqu->freq.m, i = wrqu->freq.e ;
		     i > 0 ; i--)
			req.ds_config *= 10;
		req.ds_config /= 1000;
		
	}
	res = dosetinfo(handle, NDIS_OID_CONFIGURATION, (char*)&req,
			sizeof(req), &written, &needed);
	if(res)
	{
		printk(KERN_ERR "%s: setting configuration failed (%08X)\n",
		       dev->name, res);
		return -EINVAL;
	}
	return 0;
}

static int ndis_get_tx_power(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned long ndis_power;
	unsigned int written, needed, res;

	res = doquery(handle, NDIS_OID_TX_POWER_LEVEL, (char*)&ndis_power,
		      sizeof(ndis_power), &written, &needed);
	if(res)
		return -EOPNOTSUPP;

	wrqu->txpower.flags = IW_TXPOW_MWATT;
	wrqu->txpower.disabled = 0;
	wrqu->txpower.fixed = 0;
	wrqu->txpower.value = ndis_power;
	return 0;
}

static int ndis_set_tx_power(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned long ndis_power;
	unsigned int written, needed, res;

	if (wrqu->txpower.disabled)
	{
		ndis_power = 0;
		res = dosetinfo(handle, NDIS_OID_TX_POWER_LEVEL, (char *)&ndis_power,
				sizeof(ndis_power), &written, &needed);
		res |= set_int(handle, NDIS_OID_DISASSOCIATE, 0);
		if (res)
			return -EINVAL;
		return 0;
	}
	else 
	{
		if (wrqu->txpower.flags == IW_TXPOW_MWATT)
			ndis_power = wrqu->txpower.value;
		else // wrqu->txpower.flags == IW_TXPOW_DBM
		{
			if (wrqu->txpower.value > 20)
				ndis_power = 128;
			else if (wrqu->txpower.value < -43)
				ndis_power = 127;
			else
			{
				signed char tmp;
				tmp = wrqu->txpower.value;
				tmp = -12 - tmp;
				tmp <<= 2;
				ndis_power = (unsigned char)tmp;
			}
		}
	}
	res = dosetinfo(handle, NDIS_OID_TX_POWER_LEVEL, (char*)&ndis_power,
		      sizeof(ndis_power), &written, &needed);
	if(res)
	{
		printk(KERN_ERR "%s: setting tx_power failed (%08X)\n",
		       dev->name, res);
		return -EINVAL;
	}

	return 0;
}

static int ndis_get_bitrate(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	int ndis_rate;

	int res = query_int(handle, NDIS_OID_GEN_SPEED, &ndis_rate);
	if(res)
	{
		printk(KERN_ERR "%s: getting bitrate failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	wrqu->bitrate.value = ndis_rate * 100;
	return 0;
}

static int ndis_set_dummy(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	/* Do nothing. Used for set_encode and set_rate. Having a dummy
	 * function like this surpresses errors the user will get when
	 * running ifup.
	  */
	return 0;
}

static int ndis_get_rts_threshold(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	int ndis_rts_threshold;

	int res = query_int(handle, NDIS_OID_RTS_THRESH, &ndis_rts_threshold);
	if(res)
	{
		printk(KERN_ERR "%s: getting RTS threshold failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	wrqu->rts.value = ndis_rts_threshold;
	return 0;
}

static int ndis_get_frag_threshold(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	int ndis_frag_threshold;

	int res = query_int(handle, NDIS_OID_FRAG_THRESH, &ndis_frag_threshold);
	if(res)
	{
		printk(KERN_ERR "%s: getting fragmentation threshold failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	wrqu->frag.value = ndis_frag_threshold;
	return 0;
}

int ndis_get_ap_address(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned int res, written, needed;
	__u8 mac_address[ETH_ALEN];

	memset(mac_address, 0, ETH_ALEN);
	res = doquery(handle, NDIS_OID_BSSID, (char*)&mac_address, ETH_ALEN,
		      &written, &needed);
	if(res)
		memset(mac_address, 0, ETH_ALEN);

        memcpy(wrqu->ap_addr.sa_data, mac_address, ETH_ALEN);
        wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	DBGTRACE("%02X:%02X:%02X:%02X:%02X:%02X", mac_address[0],
		 mac_address[1], mac_address[2], mac_address[3],
		 mac_address[4], mac_address[5]);
        return 0;
}

static int ndis_set_ap_address(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv; 
	unsigned int res, written, needed;
	__u8 mac_address[ETH_ALEN];

        memcpy(mac_address, wrqu->ap_addr.sa_data, ETH_ALEN);
	res = dosetinfo(handle, NDIS_OID_BSSID, (char*)&(mac_address[0]), ETH_ALEN, &written, &needed);

	if(res)
	{
		printk(KERN_ERR "%s: setting AP mac address failed (%08X)\n",
		       dev->name, res);
		return -EINVAL;
	}

        return 0;
}

static int ndis_get_wep(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	int status, res, index;
	struct wep_info *wep_info = &handle->wep_info;

	wrqu->data.length = 0;
	extra[0] = 0;

	index = (wrqu->data.flags & IW_ENCODE_INDEX);
	DBGTRACE2("index = %u", index);
	if (index && (index <= 0 || index > MAX_WEP_KEYS))
	{
		printk(KERN_ERR "%s: wep index out of range (%u)\n",
		       dev->name, index);
		return -EINVAL;
	}

	if (index == 0)
		index = wep_info->active;
	else	
		index--;

	if (index != wep_info->active)
	{
		if (wep_info->keys[index].length > 0)
		{
			wrqu->data.flags |= IW_ENCODE_ENABLED;
			wrqu->data.length = wep_info->keys[index].length;
			memcpy(extra, wep_info->keys[index].key,
			       wep_info->keys[index].length);
		}
		else
			wrqu->data.flags |= IW_ENCODE_DISABLED;

		return 0;
	}
	
	/* active key */
	res = query_int(handle, NDIS_OID_WEP_STATUS, &status);
	if (res)
	{
		printk(KERN_ERR "%s: getting wep status failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	if (status == WEP_ENABLED)
	{
		wrqu->data.flags |= IW_ENCODE_ENABLED | (index+1);
		wrqu->data.length = wep_info->keys[index].length;
		memcpy(extra, wep_info->keys[index].key,
		       wep_info->keys[index].length);
	}
	else if (status == WEP_DISABLED)
		wrqu->data.flags |= IW_ENCODE_DISABLED;
	else if (status == WEP_NOKEY)
		wrqu->data.flags |= IW_ENCODE_NOKEY;

	res = query_int(handle, NDIS_OID_AUTH_MODE, &status);
	if (res)
	{
		printk(KERN_ERR "%s: getting authentication mode failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}

	if (status == AUTHMODE_OPEN)
		wrqu->data.flags |= IW_ENCODE_OPEN;
	else if (status == AUTHMODE_RESTRICTED)
		wrqu->data.flags |= IW_ENCODE_RESTRICTED;
	
	return 0;
}
	
static int ndis_set_wep(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res, written, needed, index;
	union iwreq_data essid_wrqu;
	struct wep_info *wep_info = &handle->wep_info;
	struct wep_req wep_req;
	
	index = (wrqu->data.flags & IW_ENCODE_INDEX);
	DBGTRACE2("index = %u", index);
	if (index > MAX_WEP_KEYS)
	{
		printk(KERN_ERR "%s: wep index out of range (%u)\n",
		       dev->name, index);
		return -EINVAL;
	}

	if (index <= 0)
		index = wep_info->active;
	else	
		index--;

	/* store the key if given */
	if(wrqu->data.length > 0)
	{
		wep_info->keys[index].length = wrqu->data.length;
		memcpy(&wep_info->keys[index].key, extra,
		       wrqu->data.length);		
	}

	/* remove key if disabled */
	if (wrqu->data.flags & IW_ENCODE_DISABLED)
	{
		unsigned long keyindex = index;
		res = dosetinfo(handle, NDIS_OID_REMOVE_WEP,
				(char *)&keyindex, sizeof(keyindex),
				&written, &needed);
		if (res)
		{
			printk(KERN_ERR "%s: removing wep key %d failed (%08X)\n",
			       dev->name, index, res);
			return -EINVAL;
		}
		wep_info->keys[index].length = 0;
		
		/* if it is active key, disable wep */
		if (index == wep_info->active)
		{
			res = set_int(handle, NDIS_OID_WEP_STATUS,
				      WEP_DISABLED);
			if (res)
				printk(KERN_WARNING "%s: changing wep status failed (%08X)\n",
				       dev->name, res);
		}
		return 0;
	}

	if (wep_info->keys[index].length > 0)
	{
		wep_req.len = sizeof(wep_req);

		if (index == wep_info->active || (wrqu->data.length == 0))
		{
			DBGTRACE2("setting key %d as tx key", index);
			wep_req.keyindex = (index) | (1 << 31);
			wep_info->active = index;
		}
		else
		{
			DBGTRACE2("setting key %d as rx key", index);
			wep_req.keyindex = (index);
		}
		wep_req.keylength = wep_info->keys[index].length;
		memcpy(&wep_req.key, wep_info->keys[index].key,
		       wep_info->keys[index].length);

		res = set_int(handle, NDIS_OID_WEP_STATUS, WEP_ENABLED);
		if (res)
			printk(KERN_WARNING "%s: changing wep status failed (%08X)\n",
			       dev->name, res);

		res = dosetinfo(handle, NDIS_OID_ADD_WEP, (char *)&wep_req,
				sizeof(wep_req), &written, &needed);
		if (res)
		{
			printk(KERN_ERR "%s: adding wep key %d failed (%08X)\n",
			       dev->name, index, res);
			return -EINVAL;
		}

		/* ndis drivers want essid to be set after setting wep */
		if (handle->essid.length > 0)
		{
			memset(&essid_wrqu, 0, sizeof(essid_wrqu));
			essid_wrqu.essid.length = handle->essid.length + 1;
			essid_wrqu.essid.flags = handle->essid.flags;
			essid_wrqu.essid.pointer = handle->essid.name;
			ndis_set_essid(dev, NULL, &essid_wrqu, handle->essid.name);
		}

	}

	/* global wep state (for all keys) */
	if (wrqu->data.flags & IW_ENCODE_OPEN)
		res = set_int(handle, NDIS_OID_AUTH_MODE, AUTHMODE_OPEN);
	else // if (wrqu->data.flags & IW_ENCODE_RESTRICTED)
		res = set_int(handle, NDIS_OID_AUTH_MODE,
			      AUTHMODE_RESTRICTED);
	if (res)
	{
		printk(KERN_ERR "%s: setting wep mode failed (%08X)\n",
		       dev->name, res);
		return -EINVAL;
	}

	return 0;
}
	
static int ndis_set_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	
	if (wrqu->data.length > IW_ESSID_MAX_SIZE)
		return -EINVAL;
	memcpy(handle->nick, extra, wrqu->data.length);
	handle->nick[IW_ESSID_MAX_SIZE] = 0;
	return 0;
}

static int ndis_get_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	
	memcpy(extra, handle->nick, IW_ESSID_MAX_SIZE+1);
	wrqu->data.length = strlen(handle->nick);
	return 0;
}

char *ndis_translate_scan(struct net_device *dev, char *event, char *end_buf,
			  struct ssid_item *item)
{
	struct iw_event iwe;
	char *current_val;
	int i;
	char buf[MAX_WPA_IE_LEN * 2 + 30];

	/* add mac address */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	iwe.len = IW_EV_ADDR_LEN;
	memcpy(iwe.u.ap_addr.sa_data, item->mac, ETH_ALEN);
	event = iwe_stream_add_event(event, end_buf, &iwe, IW_EV_ADDR_LEN);

	/* add essid */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.length = item->ssid.len;
	if (iwe.u.data.length > IW_ESSID_MAX_SIZE)
		iwe.u.data.length = IW_ESSID_MAX_SIZE;
	iwe.u.data.flags = 1;
	iwe.len = IW_EV_POINT_LEN + iwe.u.data.length;
	event = iwe_stream_add_point(event, end_buf, &iwe, item->ssid.essid);

	/* add protocol name */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWNAME;
	strncpy(iwe.u.name, net_type_to_name(item->net_type), IFNAMSIZ);
	event = iwe_stream_add_event(event, end_buf, &iwe, IW_EV_CHAR_LEN);

	/* add mode */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWMODE;
	if (item->mode == NDIS_MODE_ADHOC)
		iwe.u.mode = IW_MODE_ADHOC;
	else if (item->mode == NDIS_MODE_INFRA)
		iwe.u.mode = IW_MODE_INFRA;
	else // if (item->mode == NDIS_MODE_AUTO)
		iwe.u.mode = IW_MODE_AUTO;
	event = iwe_stream_add_event(event, end_buf, &iwe, IW_EV_UINT_LEN);
	
	/* add freq */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = item->config.ds_config;
	if (item->config.ds_config > 1000000)
	{
		iwe.u.freq.m = item->config.ds_config / 10;
		iwe.u.freq.e = 1;
	}
	else
		iwe.u.freq.m = item->config.ds_config;
	/* convert from kHz to Hz */
	iwe.u.freq.e += 3;
	iwe.len = IW_EV_FREQ_LEN;
	event = iwe_stream_add_event(event, end_buf, &iwe, IW_EV_FREQ_LEN);

	/* add qual */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.level = item->rssi;
	iwe.u.qual.noise = 0;
	iwe.u.qual.qual = 0;
	iwe.len = IW_EV_QUAL_LEN;
	event = iwe_stream_add_event(event, end_buf, &iwe, IW_EV_QUAL_LEN);

	/* add key info */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (item->privacy == NDIS_PRIV_ACCEPT_ALL)
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	else
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	iwe.u.data.length = 0;
	iwe.len = IW_EV_POINT_LEN;
	event = iwe_stream_add_point(event, end_buf, &iwe, item->ssid.essid);

	/* add rate */
	memset(&iwe, 0, sizeof(iwe));
	current_val = event + IW_EV_LCP_LEN;
	iwe.cmd = SIOCGIWRATE;
	for (i = 0 ; i < NDIS_MAX_RATES ; i++)
	{
		if (item->rates[i] == 0)
			break;
		iwe.u.bitrate.value = ((item->rates[i] & 0x7f) * 500000);
		current_val = iwe_stream_add_value(event, current_val, end_buf, &iwe, IW_EV_PARAM_LEN);
	}

	if ((current_val - event) > IW_EV_LCP_LEN)
		event = current_val;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	sprintf(buf, "bcn_int=%d", item->config.beacon_period);
	iwe.u.data.length = strlen(buf);
	event = iwe_stream_add_point(event, end_buf, &iwe, buf);
	
	if (item->ie_length >= (sizeof(struct fixed_ies) + 2))
	{
		struct fixed_ies *fixed_ies = (struct fixed_ies *)item->ies;
		unsigned char *iep = (unsigned char *)(fixed_ies + 1);
		int iel = item->ie_length - sizeof(*fixed_ies);
		
		DBGTRACE("%s: adding atim\n", __FUNCTION__);
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "atim=%u", item->config.atim_window);
		iwe.u.data.length = strlen(buf);
		event = iwe_stream_add_point(event, end_buf, &iwe, buf);
		
		while(iel > 0) {
			if(iep[0] == WLAN_EID_GENERIC && iep[1] >= 4 &&
			   iep[2] == 0x00 && iep[3] == 0x50 &&
			   iep[4] == 0xf2 && iep[5] == 1)
			{
				char *p = buf;
				
				i = iep[1] + 2;
				if(i < iel)
					iel = i;
				
				p += sprintf(p, "wpa_ie=");
				for (i = 0; i < iel; i++)
					p += sprintf(p, "%02x", iep[i]);
				
				DBGTRACE("adding wpa_ie :%d\n", strlen(buf));
				memset(&iwe, 0, sizeof(iwe));
				iwe.cmd = IWEVCUSTOM;
				iwe.u.data.length = strlen(buf);
				event = iwe_stream_add_point(event, end_buf,
							     &iwe, buf);
				break;
			}

			iel -= iep[1];
			iel -= 2;
			iep += iep[1];
			iep += 2;
		}
	}

	DBGTRACE("event = %p, current_val = %p", event, current_val);

	return event;
}

static int ndis_set_scan(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res = 0;

	res = set_int(handle, NDIS_OID_BSSID_LIST_SCAN, 0);
	if (res)
	{
		printk(KERN_ERR "%s: scanning failed (%08X)\n", dev->name, res);
		handle->scan_timestamp = 0;
		return -EOPNOTSUPP;
	}
	else
	{
		handle->scan_timestamp = jiffies;
		return 0;
	}
}

static int ndis_get_scan(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
 	struct ndis_handle *handle = dev->priv;
	unsigned int i, res, written, needed, list_len;
	struct bssid_list *bssid_list;
	char *event = extra;
	struct ssid_item *cur_item ;

	if (!handle->scan_timestamp)
		return -EOPNOTSUPP;

	if (time_before(jiffies, handle->scan_timestamp + 3 * HZ))
		return -EAGAIN;
	
	/* Try with space for 15 scan items */
	list_len = sizeof(unsigned long) + sizeof(struct ssid_item) * 15;
	bssid_list = kmalloc(list_len, GFP_KERNEL);

	written = needed = 0;
	res = doquery(handle, NDIS_OID_BSSID_LIST, (char *)bssid_list,
		      list_len, &written, &needed);
	if (res == NDIS_STATUS_INVALID_LENGTH)
	{
		/* 15 items not enough; allocate required space */
		kfree(bssid_list);
		list_len = needed;
		bssid_list = kmalloc(list_len, GFP_KERNEL);
	
		res = doquery(handle, NDIS_OID_BSSID_LIST, (char*)bssid_list,
			      list_len, &written, &needed);
	}
	if (res)
	{
		printk(KERN_ERR "%s: getting BSSID list failed (%08X)\n",
		       dev->name, res);
		kfree(bssid_list);
		return -EOPNOTSUPP;
	}

	for (i = 0, cur_item = &bssid_list->items[0] ;
	     i < bssid_list->num_items ; i++)
	{
		event = ndis_translate_scan(dev, event,
					    extra + IW_SCAN_MAX_DATA,
					    cur_item);
		cur_item = (struct ssid_item *)((char *)cur_item +
						cur_item->length);
	}
	wrqu->data.length = event - extra;
	wrqu->data.flags = 0;

	kfree(bssid_list);
	return 0;
}

static int ndis_set_power_mode(struct net_device *dev,
		struct iw_request_info *info, union iwreq_data *wrqu,
		char *extra)
{
	struct ndis_handle *handle = dev->priv;
	int res, power_mode;

	if (wrqu->power.disabled == 1)
		power_mode = NDIS_POWER_OFF;
	else if (wrqu->power.flags & IW_POWER_MIN)
		power_mode = NDIS_POWER_MIN;
	else // if (wrqu->power.flags & IW_POWER_MAX)
		power_mode = NDIS_POWER_MAX;

	res = set_int(handle, NDIS_OID_POWER_MODE, power_mode);
	if (res)
	{
		printk(KERN_ERR "%s: setting power mode failed (%08X)\n",
		       dev->name, res);
		return -EINVAL;
	}

	return 0;
}

static int ndis_get_power_mode(struct net_device *dev,
		struct iw_request_info *info, union iwreq_data *wrqu,
		char *extra)
{
	struct ndis_handle *handle = dev->priv;
	int res, power_mode;

	res = query_int(handle, NDIS_OID_POWER_MODE, &power_mode);
	if (res)
	{
		printk(KERN_ERR "%s: getting power mode failed (%08X)\n",
		       dev->name, res);
		return -EOPNOTSUPP;
	}
	if (power_mode == NDIS_POWER_OFF)
		wrqu->power.disabled = 1;
	else
	{
		if (wrqu->power.flags != 0)
			return 0;
		wrqu->power.flags |= IW_POWER_ALL_R;
		wrqu->power.flags |= IW_POWER_TIMEOUT;
		wrqu->power.value = 0;
		wrqu->power.disabled = 0;

		if (power_mode == NDIS_POWER_MIN)
			wrqu->power.flags |= IW_POWER_MIN;
		else // if (power_mode == NDIS_POWER_MAX)
			wrqu->power.flags |= IW_POWER_MAX;
	}
	return 0;
}

static int ndis_get_sensitivity(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu,	char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res, written, needed;
	unsigned long rssi_trigger;

	res = doquery(handle, NDIS_OID_RSSI_TRIGGER, (char *)&rssi_trigger,
		      sizeof(rssi_trigger), &written, &needed);
	if (res)
		return -EOPNOTSUPP;
	wrqu->param.value = rssi_trigger;
	wrqu->param.disabled = (rssi_trigger == 0);
	wrqu->param.fixed = 1;
	return 0;
}

static int ndis_set_sensitivity(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu,	char *extra)
{
	struct ndis_handle *handle = dev->priv;
	unsigned int res, written, needed;
	unsigned long rssi_trigger;

	if (wrqu->param.disabled)
		rssi_trigger = 0;
	else
		rssi_trigger = wrqu->param.value;
	res = dosetinfo(handle, NDIS_OID_RSSI_TRIGGER, (char *)&rssi_trigger,
			sizeof(rssi_trigger), &written, &needed);
	if (res)
		return -EINVAL;
	return 0;
}

struct iw_statistics *ndis_get_wireless_stats(struct net_device *dev)
{
	struct ndis_handle *handle = dev->priv;
	return &handle->wireless_stats;
}


static int ndis_get_ndis_stats(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)

{
	struct iw_statistics *stats = ndis_get_wireless_stats(dev);
	memcpy(&wrqu->qual, &stats->qual, sizeof(stats->qual));
	return 0;
}

static int ndis_get_range(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct iw_point *data = &wrqu->data;
	struct ndis_handle *handle = (struct ndis_handle *)dev->priv;
	unsigned int i, written, needed;
	unsigned char rates[NDIS_MAX_RATES];
	unsigned long tx_power;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));
	
	range->txpower_capa = IW_TXPOW_MWATT;
	range->num_txpower = 0;

	if (!doquery(handle, NDIS_OID_TX_POWER_LEVEL, (char*)&tx_power,
		     sizeof(tx_power), &written, &needed))
	{
		range->num_txpower = 1;
		range->txpower[0] = tx_power;
	}


	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 0;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	range->num_channels = 1;
	
	range->max_qual.qual = 100;
	range->max_qual.level = 154;
	range->max_qual.noise = 154;
	range->sensitivity = 3;

	range->max_encoding_tokens = 4;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

	range->num_bitrates = 0;
	if (!doquery(handle, NDIS_OID_SUPPORTED_RATES, (char *)&rates,
		    sizeof(rates), &written, &needed))
	{
		for (i = 0 ; i < NDIS_MAX_RATES && rates[i] ; i++)
			if (range->num_bitrates < IW_MAX_BITRATES &&
			    rates[i] & 0x80)
			{
				range->bitrate[range->num_bitrates] =
					(rates[i] & 0x7f) * 500000;
				range->num_bitrates++;
			}
	}

	range->num_channels = (sizeof(freq_chan)/sizeof(freq_chan[0]));

	for(i = 0; i < (sizeof(freq_chan)/sizeof(freq_chan[0])) &&
		    i < IW_MAX_FREQUENCIES; i++)
	{
		range->freq[i].i = i + 1;
		range->freq[i].m = freq_chan[i] * 100000;
		range->freq[i].e = 1;
	}
	range->num_frequency = i;


	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	return 0;
}

static int ndis_reset(struct net_device *dev, struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	int res;
	res = doreset(dev->priv);
	if (res)
	{
		printk(KERN_ERR "%s: reset returns %08X\n", __FUNCTION__, res);
		return -EINVAL;
	}
	return 0;
}

static const iw_handler	ndis_handler[] = {
	//[SIOCGIWSENS    - SIOCIWFIRST] = ndis_get_sens,
	[SIOCGIWNAME	- SIOCIWFIRST] = ndis_get_name,
	[SIOCSIWESSID	- SIOCIWFIRST] = ndis_set_essid,
	[SIOCGIWESSID	- SIOCIWFIRST] = ndis_get_essid,
	[SIOCSIWMODE	- SIOCIWFIRST] = ndis_set_mode,
	[SIOCGIWMODE	- SIOCIWFIRST] = ndis_get_mode,
	[SIOCGIWFREQ	- SIOCIWFIRST] = ndis_get_freq,
	[SIOCSIWFREQ	- SIOCIWFIRST] = ndis_set_freq,
	[SIOCGIWTXPOW	- SIOCIWFIRST] = ndis_get_tx_power,
	[SIOCSIWTXPOW	- SIOCIWFIRST] = ndis_set_tx_power,
	[SIOCGIWRATE	- SIOCIWFIRST] = ndis_get_bitrate,
	[SIOCSIWRATE	- SIOCIWFIRST] = ndis_set_dummy,
	[SIOCGIWRTS	- SIOCIWFIRST] = ndis_get_rts_threshold,
	[SIOCGIWFRAG	- SIOCIWFIRST] = ndis_get_frag_threshold,
	//[SIOCSIWRETRY	- SIOCIWFIRST] = ndis_get_rety_limit,
	[SIOCGIWAP	- SIOCIWFIRST] = ndis_get_ap_address,
	[SIOCSIWAP	- SIOCIWFIRST] = ndis_set_ap_address,
	[SIOCSIWENCODE	- SIOCIWFIRST] = ndis_set_wep,
	[SIOCGIWENCODE	- SIOCIWFIRST] = ndis_get_wep,
	[SIOCSIWSCAN	- SIOCIWFIRST] = ndis_set_scan,
	[SIOCGIWSCAN	- SIOCIWFIRST] = ndis_get_scan,
	[SIOCGIWPOWER	- SIOCIWFIRST] = ndis_get_power_mode,
	[SIOCSIWPOWER	- SIOCIWFIRST] = ndis_set_power_mode,
	[SIOCGIWRANGE	- SIOCIWFIRST] = ndis_get_range,
	[SIOCGIWSTATS	- SIOCIWFIRST] = ndis_get_ndis_stats,
	[SIOCGIWSENS	- SIOCIWFIRST] = ndis_get_sensitivity,
	[SIOCSIWSENS	- SIOCIWFIRST] = ndis_set_sensitivity,
	[SIOCGIWNICKN	- SIOCIWFIRST] = ndis_get_nick,
	[SIOCSIWNICKN	- SIOCIWFIRST] = ndis_set_nick,
	[SIOCSIWCOMMIT	- SIOCIWFIRST] = ndis_set_dummy,
};

/* WPA support */

static int ndis_set_wpa(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = (struct ndis_handle *)dev->priv;
	unsigned int res;
	
	TRACEENTER1();
	DBGTRACE("flags = %d, encr_alg = %d, handle->capa = %d, "
		 "handle->encr_alg = %d", wrqu->data.flags, handle->encr_alg,
	       handle->wpa_capa, handle->encr_alg);
	
	if (!wrqu->data.flags)
	{

		/* FIXME : what should the authmode be? */
		res = set_int(handle, NDIS_OID_AUTH_MODE, AUTHMODE_RESTRICTED);
		if (res)
			return -1;
		res = set_int(handle, NDIS_OID_WEP_STATUS, WEP_ENABLED);
		if (res)
			return -1;
		return 0;
	}

	if (!handle->wpa_capa)
		return -1;

	DBGTRACE("authmode = %d, wepmode = %d", handle->auth_mode,
		 handle->wep_mode);
	handle->auth_mode = AUTHMODE_WPAPSK;
	handle->wep_mode = WEP_ENCR2_ENABLED;

	if (handle->auth_mode == AUTHMODE_WPA ||
	    handle->auth_mode == AUTHMODE_WPAPSK)
	{
		res = set_int(handle, NDIS_OID_AUTH_MODE,
			      handle->auth_mode);
		if (res)
			return -1;
	}
	else
		return -1;

	if (handle->wep_mode == WEP_ENCR3_ENABLED ||
	    handle->wep_mode == WEP_ENCR2_ENABLED)
	{
		res = set_int(handle, NDIS_OID_WEP_STATUS, handle->wep_mode);
		if (res)
			return -1;
	}
	else
		return -1;
	
	DBGTRACE("%s", "wpa enabled");
	TRACEEXIT1(return 0);
}

static int ndis_set_key(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = (struct ndis_handle *)dev->priv;
	struct ndis_key ndis_key;
	struct wpa_key *wpa_key = (struct wpa_key *)wrqu->data.pointer;
	int i, res, written, needed;
	
	TRACEENTER1("alg = %d", wpa_key->alg);
	
	if (wpa_key->alg == WPA_ALG_NONE)
	{
		struct ndis_remove_key ndis_remove_key;
		ndis_remove_key.length = sizeof(ndis_remove_key);
		ndis_remove_key.key_index = wpa_key->key_index;
		if (wpa_key->addr)
			memcpy(&ndis_remove_key.bssid, wpa_key->addr, ETH_ALEN);
		else
			memset(&ndis_remove_key.bssid, 0xff, ETH_ALEN);
		res = dosetinfo(handle, NDIS_OID_REMOVE_KEY,
				(char *)&ndis_remove_key,
				sizeof(ndis_remove_key),
				&written, &needed);
		if (res)
		{
			printk(KERN_ERR "%s: removing key failed with %08X, %d, %d\n",
			       __FUNCTION__, res, needed, sizeof(ndis_remove_key));
			return 0;
		}
		return 0;
	}

	if (wpa_key->alg == WPA_ALG_WEP)
	{
		struct wep_req wep;

		wep.len = sizeof(wep);
		wep.keyindex = wpa_key->key_index;
		wep.keyindex |= (1 << 31);
		wep.keylength = wpa_key->key_len;
		memcpy(&wep.key, wpa_key->key, wpa_key->key_len);

		handle->auth_mode = AUTHMODE_RESTRICTED;
		handle->wep_mode = WEP_ENCR1_ENABLED;

		res = set_int(handle, NDIS_OID_AUTH_MODE, handle->auth_mode);
		if (res)
			return -1;
		res = dosetinfo(handle, NDIS_OID_ADD_WEP, (char *)&wep,
				sizeof(wep), &written, &needed);
		if (res)
			return -1;
		res = set_int(handle, NDIS_OID_WEP_STATUS, handle->wep_mode);
		if (res)
			return -1;
		return 0;
	}

	/* alg is either WPA_ALG_TKIP or WPA_ALG_CCMP */

	if (wpa_key->key_len > sizeof(ndis_key.key))
	{
		printk(KERN_ERR "%s: incorrect key length (%d)\n",
		       dev->name, wpa_key->key_len);
		TRACEEXIT1(return -EINVAL);
	}
	
	DBGTRACE("adding key %d, %d", wpa_key->key_index, wpa_key->key_len);
	ndis_key.key_index = wpa_key->key_index;
	if (wpa_key->set_tx)
		ndis_key.key_index |= (1 << 31);
	else
		ndis_key.key_index |= (1 << 30);
	ndis_key.key_len = wpa_key->key_len;
	memcpy(&ndis_key.key, wpa_key->key, wpa_key->key_len);
	
	memcpy(&ndis_key.bssid, wpa_key->addr, ETH_ALEN);
	
	for (i = 0, ndis_key.key_rsc = 0 ; i < wpa_key->seq_len ; i++)
		ndis_key.key_rsc |= (wpa_key->seq[i] << (i * 8));
	
	res = dosetinfo(handle, NDIS_OID_ADD_KEY, (char *)&ndis_key,
			sizeof(ndis_key) - 
			sizeof(ndis_key.key) + ndis_key.key_len,
			&written, &needed);
	if (res)
	{
		printk(KERN_ERR "%s: adding key failed (%08X)\n",
		       dev->name, res);
		TRACEEXIT1(return -EINVAL);
	}
	
	/* FIXME: check for TKIP -> encr mode */
	handle->encr_alg = wpa_key->alg;
	DBGTRACE("encr_alg = %d", handle->encr_alg);
	TRACEEXIT1(return 0);
}

static int ndis_set_associate(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	printk(KERN_INFO "%s: Entry\n", __FUNCTION__);
	/* FIXME: for now we simply set the essid */
	wrqu->essid.flags = 1;
	return ndis_set_essid(dev, info, wrqu, extra);
}

static int ndis_set_disassociate(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = (struct ndis_handle *)dev->priv;
	
	printk(KERN_INFO "%s: Entry\n", __FUNCTION__);
	if (set_int(handle, NDIS_OID_DISASSOCIATE, 0))
		return -1;
	return 0;
}

int ndis_set_priv_filter(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ndis_handle *handle = (struct ndis_handle *)dev->priv;
	int res;

	printk(KERN_INFO "%s: Entry (%d)\n", __FUNCTION__, wrqu->param.value);
	if (wrqu->param.value == NDIS_PRIV_WEP)
		res = set_int(handle, NDIS_OID_PRIVACY_FILTER, NDIS_PRIV_WEP);
	else
		res = set_int(handle, NDIS_OID_PRIVACY_FILTER, NDIS_PRIV_ACCEPT_ALL);
	return 0;
}

static int ndis_set_generic_element(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	int i;
	printk(KERN_INFO "%s: Entry\n", __FUNCTION__);
	printk(KERN_INFO "generic_element (%d):", wrqu->data.length);
	for (i = 0 ; i < wrqu->data.length; i ++)
		printk(KERN_INFO "%02X ", ((char *)wrqu->data.pointer)[i]);
	printk(KERN_INFO "\n");

	return 0;
}

static int ndis_dummy(struct net_device *dev, struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	printk(KERN_INFO "%s: called with mode = %d\n",
	       __FUNCTION__, wrqu->mode);
	return 0;
}

static const struct iw_priv_args priv_args[] = {
	{PRIV_RESET, 0, 0, "ndis_reset"},
	{WPA_SET_WPA, 0, IW_PRIV_TYPE_ADDR | 16, "setwpa"},
	{WPA_SET_KEY, 0, IW_PRIV_TYPE_ADDR | 16, "setkey"},
	{WPA_ASSOCIATE, 0, IW_PRIV_TYPE_ADDR | 16, "associate"},
	{WPA_DISASSOCIATE, 0, IW_PRIV_TYPE_ADDR | 16, "disassociate"},
	{WPA_SET_PRIV_FILTER, 0, IW_PRIV_TYPE_ADDR | 16, "privfilter"},
	{WPA_SET_GENERIC_ELEMENT, 0, IW_PRIV_TYPE_ADDR | 16, "genelem"},
};

static const iw_handler priv_handler[] = {
	[PRIV_RESET - SIOCIWFIRSTPRIV] = ndis_reset,
	[WPA_SET_WPA - SIOCIWFIRSTPRIV] = ndis_set_wpa,
	[WPA_SET_KEY - SIOCIWFIRSTPRIV] = ndis_set_key,
	[WPA_ASSOCIATE - SIOCIWFIRSTPRIV] = ndis_set_associate,
	[WPA_DISASSOCIATE - SIOCIWFIRSTPRIV] = ndis_set_disassociate,
	[WPA_SET_PRIV_FILTER - SIOCIWFIRSTPRIV] = ndis_set_priv_filter,
	[WPA_SET_GENERIC_ELEMENT - SIOCIWFIRSTPRIV] = ndis_set_generic_element,
};

const struct iw_handler_def ndis_handler_def = {
	.num_standard	= sizeof(ndis_handler) / sizeof(ndis_handler[0]),
	.num_private	= sizeof(priv_handler) / sizeof(priv_handler[0]),
	.num_private_args = sizeof(priv_args) / sizeof(priv_args[0]),

	.standard	= (iw_handler *)ndis_handler,
	.private	= (iw_handler *)priv_handler,
	.private_args	= (struct iw_priv_args *)priv_args,
};
