
/*
 rtsp.c

Copyright (C) 2012  Elecard Devices

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Elecard Devices nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ELECARD DEVICES BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/***********************************************
* INCLUDE FILES                                *
************************************************/

#include "rtsp.h"

#include "debug.h"
#include "app_info.h"
#include "sem.h"
#include "gfx.h"
#include "interface.h"
#include "StbMainApp.h"
#include "rtp_func.h"
#include "rtp.h"
#include "media.h"
#include "dlna.h"
#include "off_air.h"
#include "list.h"
#include "rtp_common.h"
#include "l10n.h"
#include "playlist.h"
#include "output_network.h"
#ifdef ENABLE_VIDIMAX
#include "vidimax.h"
#endif

#include <sdp.h>
#include <tools.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>

/***********************************************
* LOCAL TYPEDEFS                               *
************************************************/

typedef struct stream_files
{
	unsigned int index;
	char *stream;
	char *name;
	char *description;
	char *poster;
	char *thumb;
	unsigned int pidv;
	unsigned int pida;
	unsigned int pidp;
	unsigned int vformat;
	unsigned int aformat;
	struct stream_files *next;
} streams_struct;

/******************************************************************
* STATIC FUNCTION PROTOTYPES                  <Module>_<Word>+    *
*******************************************************************/

static void rtsp_setupPlayControl(void *pArg);
static int  rtsp_stream_change(interfaceMenu_t *pMenu, void* pArg);
static int  rtsp_stream_start(void* pArg);
static int  rtsp_setChannel(int channel, void* pArg);
static int  rtsp_play_callback(interfacePlayControlButton_t button, void *pArg);
static int  rtsp_setChannelFromURL(interfaceMenu_t *pMenu, const char *value, const char* description, const char* thumbnail, void* pArg);

static void rtsp_enableStateCheck(void);
static void rtsp_disableStateCheck(void);
static int rtsp_checkPlaybackState(void *notused);

static int  rtsp_startNextChannel(int direction, void* pArg);

static void clean_list(streams_struct *stream_ptr);
static int find_streams(streams_struct **ppstream_head);
static streams_struct *add_stream(streams_struct **ppstream_head, char *stream, int index);
static streams_struct *get_stream(unsigned int index);
static int  get_rtsp_streams(streams_struct **ppstream_head);

static int  rtsp_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg);

static void *rtsp_list_updater(void *pArg);
static int  rtsp_displayStreamMenu(void* pArg);

/******************************************************************
* STATIC DATA                  g[k|p|kp|pk|kpk]<Module>_<Word>+   *
*******************************************************************/

static int collectFlag;
static pthread_t collectThread;
static rtsp_stream_info stream_info;

//static int selected;
static streams_struct *pstream_head=NULL;
static streams_struct **ppstream_head=&pstream_head;

//static struct rtp_list *streams = NULL;
static interfaceListMenu_t rtspStreamMenu;

/* start/stop semaphore */
static pmysem_t  rtsp_semaphore;

static char rtsp_lasturl[MAX_URL] = "rtsp://";

/*********************************************************(((((((**********
* EXPORTED DATA      g[k|p|kp|pk|kpk]ph[<lnx|tm|NONE>]StbTemplate_<Word>+ *
***************************************************************************/

interfaceListMenu_t rtspMenu;

/*******************************************************************************
* FUNCTION IMPLEMENTATION  <Module>[_<Word>+] for static functions             *
*                          tm[<layer>]<Module>[_<Word>+] for exported functions*
********************************************************************************/

void clean_list(streams_struct *stream_ptr)
{
	//recursively clears linked list

	//dprintf("%s: clean list\n", __FUNCTION__);

	if ( stream_ptr->next!=NULL )
	{
		clean_list((streams_struct *) stream_ptr->next);
	}

	dfree(stream_ptr->stream);
	FREE(stream_ptr->name);
	FREE(stream_ptr->description);
	FREE(stream_ptr->thumb);
	FREE(stream_ptr->poster);
	dfree(stream_ptr);
}

static void rtsp_urlHandler(void *pArg, const char *location, const char *desc, xmlConfigHandle_t track)
{
	int            *pIndex = (int*)pArg;
	streams_struct *pstream;
	char           *ptr;

	if( location[0] )
	{
		pstream = add_stream(ppstream_head, (char *)location, *pIndex);

		helperSafeStrCpy(&pstream->name, desc);

		if( track && pstream )
		{
			ptr = (char*)xmlConfigGetText(track, "descr");
			if( ptr && *ptr )
			{
				//dprintf("%s: Adding '%s' description to %03d stream\n", __FUNCTION__, ptr, *pIndex);
				helperSafeStrCpy( &pstream->description, ptr );
			}

			ptr = (char*)xmlConfigGetText(track, "thumb");
			if( ptr && *ptr )
			{
				//dprintf("%s: Adding '%s' thumb to %03d stream\n", __FUNCTION__, ptr, *pIndex);
				helperSafeStrCpy( &pstream->thumb, ptr );
			}

			ptr = (char*)xmlConfigGetText(track, "poster");
			if( ptr && *ptr )
			{
				//dprintf("%s: Adding '%s' poster to %03d stream\n", __FUNCTION__, ptr, *pIndex);
				helperSafeStrCpy( &pstream->poster, ptr );
			}
		}

		(*pIndex)++;
	}
}

int find_streams(streams_struct **ppstream_head)
{
	//http get streams.txt
	char info_url[MAX_URL];
	int  i;
	int  index = 0;

	dprintf("%s: get semaphore\n", __FUNCTION__);

	mysem_get(rtsp_semaphore);

	if( appControlInfo.rtspInfo.usePlaylistURL && appControlInfo.rtspInfo.streamInfoUrl[0] != 0 )
	{
		eprintf("%s: trying URL '%s'\n", __FUNCTION__, appControlInfo.rtspInfo.streamInfoUrl);
		playlist_getFromURL( appControlInfo.rtspInfo.streamInfoUrl, rtsp_urlHandler, (void*)&index );
	} else
	{
		i = 0;
		while (appControlInfo.rtspInfo.streamInfoFiles[i] != NULL)
		{
			sprintf(info_url, "http://%s/%s", appControlInfo.rtspInfo.streamInfoIP, appControlInfo.rtspInfo.streamInfoFiles[i]);
			eprintf("%s: trying IP '%s'\n", __FUNCTION__, info_url);
			if( playlist_getFromURL( info_url, rtsp_urlHandler, (void*)&index ) == PLAYLIST_ERR_OK )
				break;
			i++;
		}
	}

	dprintf("%s: release semaphore\n", __FUNCTION__);
	mysem_release(rtsp_semaphore);

	return index == 0 ? -1 : 0;
}

int rtsp_checkPlaybackState(void *notused)
{
	DFBVideoProviderStatus status = gfx_getVideoProviderStatus(screenMain);
	switch (status)
	{
		case DVSTATE_FINISHED:
		case DVSTATE_STOP:
			dprintf("%s: status = DVSTATE_STOP || DVSTATE_FINISHED\n", __FUNCTION__);
			
			if ( status == DVSTATE_FINISHED )
				interface_showMessageBox(_T("ERR_STREAM_NOT_SUPPORTED"), thumbnail_error, 0);
			interface_playControlSlider(0,0,0);
#ifdef ENABLE_VIDIMAX
			if (appControlInfo.vidimaxInfo.active){
				vidimax_stopVideoCallback();
			}
#endif
			rtsp_stopVideo(screenMain);
			interface_showMenu(1, 1);

			return 0;

#ifdef STBPNX
		case DVSTATE_STOP_REWIND:
			appControlInfo.playbackInfo.scale = 1.0;
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			interface_playControlSelect(interfacePlayControlPlay);
			interface_notifyText(NULL, 1);
			// fall through
#endif
		default:
			if (!gfx_videoProviderIsPaused(screenMain))
			{
				double length_stream   = 0.0;
				double position_stream = 0.0;
				int ret_val = gfx_getPosition(&length_stream,&position_stream);
				if (ret_val == 0) {
					//dprintf("%s: got position %f, set it\n", __FUNCTION__, ret_val, position_stream);
					if (position_stream <= length_stream)
						interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position_stream);
					else {
						eprintf("%s: RTSP played 'till the end\n", __FUNCTION__);
						rtsp_stopVideo(screenMain);

						return 0;
					}
				}
			}
	}
	rtsp_enableStateCheck();

	return 0;
}

void rtsp_enableStateCheck(void)
{
	interface_addEvent(rtsp_checkPlaybackState, NULL, 1000, 1);
}

void rtsp_disableStateCheck(void)
{
	// At the end make focused play controls but not slider 
	interface_playControlSlider(0, 0, 0);
	interface_playControlSetInputFocus(inputFocusPlayControl);
	interface_playControlSelect(interfacePlayControlStop);

	interface_removeEvent(rtsp_checkPlaybackState, NULL);
	interface_notifyText(NULL, 1);
}

void rtsp_stopVideo(int which)
{
	dprintf("%s: in %d\n", __FUNCTION__, which);

	mysem_get(rtsp_semaphore);
	dprintf("%s: got sem\n", __FUNCTION__);

	if ( appControlInfo.rtspInfo.active != 0 )
	{
		rtsp_disableStateCheck();

		interface_playControlSlider(0, 0, 0);
		interface_playControlSelect(interfacePlayControlStop);

		gfx_stopVideoProvider(which, 1, 1);
		appControlInfo.rtspInfo.active = 0;
		interface_disableBackground();
	}

	dprintf("%s: release sem\n", __FUNCTION__);
	mysem_release(rtsp_semaphore);
}

static streams_struct *add_stream(streams_struct **ppstream_head, char *stream, int index)
{
	streams_struct* stream_ptr=NULL;
	streams_struct* stream_ptr_prev=NULL;
	size_t stream_size = strlen(stream)+1;

	if ( (*ppstream_head) == NULL )
	{
		//if still no ppstream_head structure create one
		(*ppstream_head) = dmalloc(sizeof(streams_struct));
		stream_ptr_prev = NULL;
		stream_ptr = (*ppstream_head);
	} else
	{
		stream_ptr = (*ppstream_head);
		while ( stream_ptr->next != NULL )
		{
			stream_ptr = stream_ptr->next;
		}

		//otherwise point previous struct to next struct and create that
		stream_ptr->next = dmalloc(sizeof(streams_struct));
		stream_ptr_prev = stream_ptr;
		stream_ptr = (streams_struct *) stream_ptr_prev->next;
	}

	stream_ptr->index       = index;
	stream_ptr->name        = NULL;
	stream_ptr->description = NULL;
	stream_ptr->thumb       = NULL;
	stream_ptr->poster      = NULL;
	stream_ptr->next        = NULL;
	stream_ptr->stream      = dmalloc(stream_size);
	memcpy(stream_ptr->stream, stream, stream_size);
	
	return stream_ptr;
}

static void *rtsp_list_updater(void *pArg)
{
	int i;
	int which;
	int sleepTime = 1;

	which = GET_NUMBER(pArg);

	while (collectFlag)
	{
		interface_showLoadingAnimation();

		dprintf("%s: rtsp_list_updater: collecting/waiting\n", __FUNCTION__);

		i = 0;
		while (i++ < sleepTime*10)
		{
			if (collectFlag)
			{
				usleep(100000);
			} else
			{
				dprintf("%s: stop waiting\n", __FUNCTION__);
				interface_hideLoadingAnimation();
				break;
			}
		}

		if (collectFlag)
		{
			get_rtsp_streams(ppstream_head);

			if(ppstream_head == NULL)
			{
				collectFlag--;
			} else
			{
				collectFlag = 0;
			}

			interface_hideLoadingAnimation();

			rtsp_displayStreamMenu(SET_NUMBER(which));

			sleepTime = 2;
		}
	}

	collectThread = 0;

	dprintf("%s: exit normal\n", __FUNCTION__);

	return NULL;
}

static int get_rtsp_streams(streams_struct **ppstream_head)
{
	int ret;
	int i = 0;
	char buf[PATH_MAX];

	dprintf("%s: in\n", __FUNCTION__);

	if ( pstream_head != NULL && *ppstream_head != NULL )
	{
		interface_clearMenuEntries(_M &rtspStreamMenu); // don't leave thumb pointers to previous streams!
		clean_list(*ppstream_head);
		*ppstream_head = NULL;
	}

	dprintf("%s: call find_streams\n", __FUNCTION__);

	ret = find_streams(ppstream_head);

	if ( !((ret == 0)&&(pstream_head != NULL)) )
	{
		int file;

		dprintf("%s: open %s\n", __FUNCTION__, appControlInfo.rtspInfo.streamFile);

		file = open(appControlInfo.rtspInfo.streamFile, O_RDONLY);

		if ( file > 0 )
		{
			while ( helperReadLine(file, buf) == 0 )
			{
				if ( strlen(buf) > 0 )
				{
					add_stream(ppstream_head, buf, i);
				}
				i++;
			}
		}
		ret = file;
	}

	return ret;
}

int rtsp_startVideo(int which)
{
	char rtsp_url[256+32];
	char *str;

	interface_showLoadingAnimation();

	mysem_get(rtsp_semaphore);
	dprintf("%s: got sem\n", __FUNCTION__);

	media_slideshowStop(1);

	gfx_stopVideoProviders(which);

	sprintf(rtsp_url,"rtsp://%s:%d/%s",
		stream_info.ip,
		stream_info.port,
		stream_info.streamname);
	// xWorks links workaround
	if( (str = strstr(rtsp_url, "?manualDetectVideoInfo")) != NULL )
		*str = 0;

	dprintf("%s: starting video %s\n", __FUNCTION__, rtsp_url);
	if (gfx_startVideoProvider(rtsp_url, which, 1, "") != 0)
	{
		mysem_release(rtsp_semaphore);
		interface_showMessageBox(_T("ERR_STREAM_UNAVAILABLE"), thumbnail_error, 0);
		eprintf("RTSP: Failed to start video provider '%s'\n", rtsp_url);
		return -1;
	}

#ifdef ENABLE_VERIMATRIX
	if (appControlInfo.useVerimatrix != 0)
	{
		eprintf("RTSP: Enable verimatrix...\n");
		if (gfx_enableVideoProviderVerimatrix(screenMain, VERIMATRIX_INI_FILE) != 0)
		{
			interface_showMessageBox(_T("ERR_VERIMATRIX_INIT"), thumbnail_error, 0);
		}
	}
#endif
#ifdef ENABLE_SECUREMEDIA
	if (appControlInfo.useSecureMedia != 0)
	{
		eprintf("RTSP: Enable securemedia...\n");
		if (gfx_enableVideoProviderSecureMedia(screenMain) != 0)
		{
			interface_showMessageBox(_T("ERR_SECUREMEDIA_INIT"), thumbnail_error, 0);
		}
	}
#endif

	if(!gfx_getVideoProviderHasVideo(screenMain))
	{
		if(appControlInfo.slideshowInfo.state == slideshowDisabled)
		{
			interface_setBackground(0,0,0,0xFF, INTERFACE_WALLPAPER_IMAGE);
		}
	}
	else
	{
		media_slideshowStop(1);
	}
	if (gfx_isTrickModeSupported(screenMain))
	{
		interface_playControlSetButtons( interface_playControlGetButtons()|interfacePlayControlFastForward|interfacePlayControlRewind );
	}

	appControlInfo.rtspInfo.active = 1;

	interface_playControlSelect(interfacePlayControlStop);
	interface_playControlSelect(interfacePlayControlPlay);

#ifdef STBPNX
	double length_stream = 0.0;
	double position_stream = 0.0;
	/// FIXME: should be done by gfx_startVideoProvider
	if(gfx_getPosition(&length_stream,&position_stream) == 0)
	{
		//dprintf("%s: start from pos %f\n", __FUNCTION__, position_stream);
		interface_playControlSlider(0, (unsigned int)length_stream, (unsigned int)position_stream);
	}else
	{
		interface_playControlSlider(0, 0, 0);
	}
#endif
	rtsp_enableStateCheck();

	dprintf("%s: release sem\n", __FUNCTION__);
	mysem_release(rtsp_semaphore);

	interface_hideLoadingAnimation();
	interface_displayMenu(1);

	return 0;
}

int rtsp_startNextChannel(int direction, void* pArg)
{
	int which;
	streams_struct* stream_ptr=NULL;
	streams_struct* prev_ptr=NULL;
	streams_struct* new_ptr=NULL;
	char url[MAX_URL];

	streams_struct* first=NULL;

	which = CHANNEL_INFO_GET_SCREEN(pArg);

	switch( appControlInfo.playbackInfo.playlistMode )
	{
		case playlistModeFavorites:
			sprintf(url,"rtsp://%s:%d/%s",
				stream_info.ip,
				stream_info.port,
				stream_info.streamname);
			playlist_setLastUrl(url);
			return playlist_startNextChannel(direction,(void*)-1);
#ifdef ENABLE_DLNA
		case playlistModeDLNA:
			sprintf(url,"rtsp://%s:%d/%s",
				stream_info.ip,
				stream_info.port,
				stream_info.streamname);
			return dlna_startNextChannel(direction,url);
#endif
		case playlistModeIPTV:
			return rtp_startNextChannel(direction,(void*)-1);
		default:;
	}

	if ( pstream_head != NULL )
	{
		prev_ptr = NULL;
		stream_ptr = pstream_head;

		while ( stream_ptr != NULL )
		{
			if (first == NULL)
			{
				first = stream_ptr;
			}
			if ( strcmp(stream_info.ip, appControlInfo.rtspInfo.streamIP) == 0 && strcmp(stream_info.streamname, stream_ptr->stream) == 0 )
			{
				if (direction == 0)
				{
					new_ptr = stream_ptr->next;
				} else
				{
					new_ptr = prev_ptr;
				}
				if (new_ptr != NULL) // don't break if it's first/last stream in list
				{
					break;
				}
			}
			prev_ptr = stream_ptr;
			stream_ptr = (streams_struct *) stream_ptr->next;
		}
	}

	//dprintf("%s: new %08X, first %08X, last %08X\n", __FUNCTION__, new_ptr, first, prev_ptr);

	if (new_ptr == NULL && first != prev_ptr) // we have more than one stream in list
	{
		if (direction == 0)
		{
			new_ptr = first;
		} else
		{
			new_ptr = prev_ptr;
		}
	}

	if (new_ptr != NULL)
	{
		//dprintf("%s: change to stream %s, index %d\n", __FUNCTION__, new_ptr->name, new_ptr->index);
		rtsp_stream_change(interfaceInfo.currentMenu, CHANNEL_INFO_SET(which, new_ptr->index));
	}

	return 0;
}


static int rtsp_displayStreamMenu(void* pArg)
{
	char channelEntry[MENU_ENTRY_INFO_LENGTH];
	streams_struct* stream_ptr=NULL;
	int which = GET_NUMBER(pArg);
	interfaceMenu_t *rtspMenu = (interfaceMenu_t*)&rtspStreamMenu;

	interface_clearMenuEntries(rtspMenu);
	interface_showLoadingAnimation();

	int streamNumber = 0;
	for (stream_ptr = pstream_head; stream_ptr != NULL; stream_ptr = stream_ptr->next)
	{
		sprintf(channelEntry, "%d: %s", streamNumber+1, stream_ptr->name ? stream_ptr->name : stream_ptr->stream);

		int entryIndex = interface_addMenuEntry(rtspMenu, channelEntry, rtsp_stream_change, CHANNEL_INFO_SET(which, streamNumber), thumbnail_vod) - 1;
		interface_setMenuEntryImage(rtspMenu, entryIndex, stream_ptr->thumb);
		//dprintf("%s: Compare current %s\n", __FUNCTION__, channelEntry);
		if ( strcmp(stream_info.ip, appControlInfo.rtspInfo.streamIP) == 0 && 
		     strcmp(stream_info.streamname, stream_ptr->stream) == 0 )
		{
			interface_setSelectedItem(rtspMenu, streamNumber);
		}
		streamNumber++;
	}

	if (streamNumber == 0)
	{
		char *str;
		str = _T("NO_MOVIES");
		interface_addMenuEntryDisabled(rtspMenu, str, thumbnail_info);
	}

	interface_hideLoadingAnimation();
	interface_displayMenu(1);

	return 0;
}

static int rtsp_play_callback(interfacePlayControlButton_t button, void *pArg)
{
	int which = CHANNEL_INFO_GET_SCREEN(pArg);
	//int streamNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	char url[MAX_URL];

	dprintf("%s: in %d\n", __FUNCTION__, button);

	if ( button == interfacePlayControlPrevious )
	{
		rtsp_startNextChannel(1, pArg);
	} else if ( button == interfacePlayControlNext )
	{
		rtsp_startNextChannel(0, pArg);
	} else if ( button == interfacePlayControlSetPosition )
	{
		double position = 0.0;
		if ( !appControlInfo.rtspInfo.active )
		{
			position					=	interface_playControlSliderGetPosition();
			appControlInfo.playbackInfo.scale = 1.0;
			rtsp_startVideo(which);
			gfx_setVideoProviderPosition(screenMain,position);
		} else
		{
			position	=	interface_playControlSliderGetPosition();
			if (gfx_videoProviderIsPaused(screenMain))
			{
				gfx_resumeVideoProvider(screenMain);
			}
			gfx_setVideoProviderPosition(screenMain,position);
			//appControlInfo.playbackInfo.scale = 1.0
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			if( appControlInfo.playbackInfo.scale == 1.0 )
			{
				interface_notifyText(NULL, 0);
				interface_playControlSelect(interfacePlayControlPlay);
			} else
			{
				sprintf(url, "%1.0fx", appControlInfo.playbackInfo.scale);
				interface_notifyText(url, 0);
				interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
			}
		}
		return 0;
	} else if ( button == interfacePlayControlPlay )
	{
		if ( !appControlInfo.rtspInfo.active )
		{
			appControlInfo.playbackInfo.scale = 1.0;
			rtsp_startVideo(which);
		} else
		{
			appControlInfo.playbackInfo.scale = 1.0;
			if (gfx_videoProviderIsPaused(screenMain))
			{
				gfx_resumeVideoProvider(screenMain);
			}
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
		}
	} else if ( button == interfacePlayControlStop )
	{
		if ( appControlInfo.rtspInfo.active )
		{
			void *show_menu = NULL;
			switch (appControlInfo.playbackInfo.playlistMode)
			{
				case playlistModeIPTV: show_menu = (void*)&rtpStreamMenu; break;
				case playlistModeFavorites: show_menu = (void*)&playlistMenu; break;
#ifdef ENABLE_DLNA
				case playlistModeDLNA: show_menu = (void*)&BrowseServersMenu; break;
#endif
				default: show_menu = (void*)&rtspStreamMenu;
			}
			rtsp_stopVideo(which);
/*
#ifdef ENABLE_VIDIMAX
			dprintf("%s(stop): currentMenu %s\n", __FUNCTION__, interfaceInfo.currentMenu->name);
			if (interfaceInfo.currentMenu == (interfaceMenu_t*)&VidimaxMenu){
				show_menu = (void*)&VidimaxMenu;
			}
#endif
*/
			interface_menuActionShowMenu(interfaceInfo.currentMenu, show_menu);
			interface_showMenu(1, 1);
		}
	} else if ( button == interfacePlayControlPause )
	{
		if ( appControlInfo.rtspInfo.active )
		{
			if (gfx_videoProviderIsPaused(screenMain))
			{
				//appControlInfo.playbackInfo.scale = 1.0;
				gfx_resumeVideoProvider(screenMain);
				gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
				interface_playControlSelect(interfacePlayControlPlay);
			} else
			{
				eprintf("%s: gfx_stopVideoProvider...\n", __FUNCTION__);
				gfx_stopVideoProvider(screenMain, 0, 0);
				interface_playControlSelect(interfacePlayControlPause);
			}
		}
	} else if ( button == interfacePlayControlFastForward )
	{
		if ( !appControlInfo.rtspInfo.active )
		{
			rtsp_startVideo(which);
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
		} else
		{
			float newScale;

			if (gfx_videoProviderIsPaused(screenMain))
				gfx_resumeVideoProvider(screenMain);

			if( appControlInfo.playbackInfo.scale >= MAX_SCALE )
				newScale = 0.0;
			else if( appControlInfo.playbackInfo.scale > 0.0 )
				newScale = appControlInfo.playbackInfo.scale * 2;
			else if( appControlInfo.playbackInfo.scale < -2.0 )
				newScale = appControlInfo.playbackInfo.scale / 2;
			else
				newScale = 1.0;

			if( newScale != 0.0 && gfx_setSpeed(screenMain, newScale) == 0 )
				appControlInfo.playbackInfo.scale = newScale;
		}
		if( appControlInfo.playbackInfo.scale == 1.0 )
		{
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
		} else
		{
			sprintf(url, "%1.0fx", appControlInfo.playbackInfo.scale);
			interface_notifyText(url, 0);
			interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
		}
	} else if ( button == interfacePlayControlRewind )
	{
		if ( !appControlInfo.rtspInfo.active )
		{
			rtsp_startVideo(which);
			gfx_setSpeed(screenMain, appControlInfo.playbackInfo.scale);
		} else
		{
			float newScale;

			if (gfx_videoProviderIsPaused(screenMain))
				gfx_resumeVideoProvider(screenMain);

			if( appControlInfo.playbackInfo.scale >= 2.0 )
				newScale = appControlInfo.playbackInfo.scale / 2;
			else if( appControlInfo.playbackInfo.scale > 0.0 )
				newScale = -2.0;
			else if( appControlInfo.playbackInfo.scale > -MAX_SCALE )
				newScale = appControlInfo.playbackInfo.scale * 2;
			else
				newScale = 0.0;

			if( newScale != 0.0 && gfx_setSpeed(screenMain, newScale) == 0 )
				appControlInfo.playbackInfo.scale = newScale;
		}
		if( appControlInfo.playbackInfo.scale == 1.0 )
		{
			interface_notifyText(NULL, 0);
			interface_playControlSelect(interfacePlayControlPlay);
		} else
		{
			sprintf(url, "%1.0fx", appControlInfo.playbackInfo.scale);
			interface_notifyText(url, 0);
			interface_playControlSelect(appControlInfo.playbackInfo.scale > 0.0 ? interfacePlayControlFastForward : interfacePlayControlRewind);
		}
	} else if (button == interfacePlayControlInfo)
	{
		interface_playControlSliderEnable(!interface_playControlSliderIsEnabled());
		interface_displayMenu(1);
		return 0;
	} else if (button == interfacePlayControlAddToPlaylist)
	{
		sprintf(url,"rtsp://%s:%d/%s",
			stream_info.ip,
			stream_info.port,
			stream_info.streamname);
		playlist_addUrl(url, stream_info.streamname);
	} else if(button == interfacePlayControlMode && appControlInfo.playbackInfo.playlistMode == playlistModeIPTV && appControlInfo.rtpMenuInfo.epg[0] != 0 )
	{
		rtp_showEPG(which, rtsp_setupPlayControl);
	} else
	{
		// default action
		return 1;
	}

	interface_displayMenu(1);

	dprintf("%s: done\n", __FUNCTION__);

	return 0;
}

static void rtsp_setupPlayControl(void *pArg)
{
	int streamNumber = CHANNEL_INFO_GET_CHANNEL(pArg);
	int buttons;
	buttons = interfacePlayControlStop|interfacePlayControlPause|interfacePlayControlRewind|interfacePlayControlFastForward|interfacePlayControlPlay;
	buttons |= appControlInfo.playbackInfo.playlistMode != playlistModeFavorites ? interfacePlayControlAddToPlaylist : interfacePlayControlMode;

	if (stream_info.custom_url == 0 || appControlInfo.playbackInfo.playlistMode != playlistModeNone)
	{
		buttons |= interfacePlayControlPrevious|interfacePlayControlNext;
	}
	if( streamNumber != CHANNEL_CUSTOM )
	{
		sprintf(appControlInfo.playbackInfo.description, "%s: %s", _T("MOVIE"), stream_info.streamname);
		appControlInfo.playbackInfo.thumbnail[0] = 0;
		appControlInfo.playbackInfo.channel = streamNumber+1;
	}

	interface_playControlSetup(rtsp_play_callback, pArg, buttons, appControlInfo.playbackInfo.description, thumbnail_vod);

	if (stream_info.custom_url == 0 || appControlInfo.playbackInfo.playlistMode != playlistModeNone)
	{
		playControlChannelCallback set_channel;
		switch (appControlInfo.playbackInfo.playlistMode)
		{
			case playlistModeFavorites:
				set_channel = playlist_setChannel;
				saveAppSettings();
				break;
			case playlistModeIPTV:
				set_channel = rtp_setChannel;
				interface_playControlSetProcessCommand(rtp_playControlProcessCommand);
				sprintf(appControlInfo.rtpMenuInfo.lastUrl,"rtsp://%s:%d/%s",
				stream_info.ip,
				stream_info.port,
				stream_info.streamname);
				saveAppSettings();
				break;
#ifdef ENABLE_DLNA
			case playlistModeDLNA: set_channel = dlna_setChannel; break;
#endif
			default:
				set_channel = rtsp_setChannel;
		}
		interface_playControlSetChannelCallbacks(rtsp_startNextChannel, set_channel);
	}
	if( appControlInfo.playbackInfo.channel >= 0 )
		interface_channelNumberShow(appControlInfo.playbackInfo.channel);
}

static int rtsp_stream_start(void* pArg)
{
	int which = CHANNEL_INFO_GET_SCREEN(pArg);

	dprintf("%s: show menu %d, active %d\n", interfaceInfo.showMenu, appControlInfo.rtspInfo.active);
	if ( appControlInfo.rtspInfo.active == 0 )
	{
		appControlInfo.playbackInfo.scale = 1.0;
		rtsp_startVideo(which);
		rtsp_setupPlayControl(pArg);
	}

	helperFlushEvents();
	dprintf("%s: done: %d\n", __FUNCTION__, appControlInfo.rtspInfo.active);

	return appControlInfo.rtspInfo.active != 0 ? 0 : -1;
}

static streams_struct * get_stream(unsigned int index)
{
	streams_struct* stream_ptr = pstream_head;
	while ( stream_ptr && stream_ptr->index != index )
		stream_ptr = (streams_struct *) stream_ptr->next;
	return stream_ptr;
}

static int rtsp_stream_change(interfaceMenu_t *pMenu, void* pArg)
{
	int which = CHANNEL_INFO_GET_SCREEN(pArg);
	unsigned int streamNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

	dprintf("%s(%d): in %d\n", __FUNCTION__, which, streamNumber);

	interface_removeEvent(rtsp_stream_start, pArg);

	if ( appControlInfo.rtspInfo.active != 0 )
	{
		eprintf("RTSP: stop video at %d\n", which);
		// force showState to NOT be triggered
		interfacePlayControl.activeButton = interfacePlayControlStop;
		rtsp_stopVideo(which);
	}

	if (streamNumber != CHANNEL_CUSTOM)
	{
		interface_setSelectedItem(_M &rtspStreamMenu, streamNumber);

		if ( pstream_head == NULL ) {
			int ret = get_rtsp_streams(ppstream_head);
			dprintf("%s: Found streams %i\n", __FUNCTION__,ret);
			if (ret != 0 || pstream_head == NULL) {
				interface_showMessageBox(_T("ERR_DEFAULT_STREAM"), thumbnail_warning, 0);
				eprintf("RTSP: Failed to find default stream\n");
				return -1;
			}
		}
		streams_struct* stream_ptr = get_stream(streamNumber);
		if ( !stream_ptr ) {
			interface_showMessageBox(_T("ERR_STREAM_IN_LIST"), thumbnail_error, 0);
			eprintf("RTSP: Stream number %d not found in linked list\n", streamNumber);
			return -1;
		}
		//dprintf("%s: stream_ptr->stream %s\n", __FUNCTION__, stream_ptr->stream);

		stream_info.port = appControlInfo.rtspInfo.RTSPPort;
		strcpy(stream_info.ip, appControlInfo.rtspInfo.streamIP);
		strcpy(stream_info.streamname, stream_ptr->stream);
		appControlInfo.playbackInfo.playlistMode = playlistModeNone;
		stream_info.custom_url = 0;
	}

	if ( rtsp_stream_start(pArg) == 0 )
	{
		interface_showMenu(0, 1);

		return 0;
	}

	return -1;
}

static int rtsp_setChannel(int channel, void* pArg)
{
	int which = CHANNEL_INFO_GET_SCREEN(pArg);
	streams_struct* stream_ptr=NULL;

	if( channel < 1 || pstream_head == NULL )
	{
		return 1;
	}
	channel--;
	stream_ptr = get_stream( (unsigned int)channel );
	if( !stream_ptr )
	{
		return 1;
	}
	rtsp_stream_change((interfaceMenu_t*)&rtspStreamMenu, CHANNEL_INFO_SET(which, channel));

	return 0;
}

int rtsp_fillStreamMenu(interfaceMenu_t *pMenu, void* pArg)
{
	int which;
	//int position = 0;
	char *str;

	which = GET_NUMBER(pArg);

	interface_clearMenuEntries((interfaceMenu_t*)&rtspStreamMenu);

	str = _T("SEARCHING_MOVIES");
	interface_addMenuEntryDisabled((interfaceMenu_t*)&rtspStreamMenu, str, thumbnail_search);

	collectFlag++;

	if( collectThread == 0)
	{
		pthread_create(&collectThread, NULL, rtsp_list_updater, SET_NUMBER(which));
		pthread_detach(collectThread);
	}

	interface_setSelectedItem((interfaceMenu_t*)&rtspStreamMenu, MENU_ITEM_MAIN);

	interface_menuActionShowMenu(pMenu, (void*)&rtspStreamMenu);

	return 0;
}

static int rtsp_setChannelFromURL(interfaceMenu_t *pMenu, const char *value, const char* description, const char* thumbnail, void* pArg)
{
	int which = GET_NUMBER(pArg);
	int res;
	rtsp_stream_info sinfo;
	url_desc_t url;

	if( value == NULL )
		return 1;

	strcpy(rtsp_lasturl, value);

	memset(&sinfo, 0, sizeof(rtsp_stream_info));

	if ((res = parseURL(value, &url)) != 0)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_URL"), thumbnail_error, 0);
		return -1;
	}

	if (url.protocol != mediaProtoRTSP)
	{
		interface_showMessageBox(_T("ERR_INCORRECT_PROTO"), thumbnail_error, 0);
		return -1;
	}

	inet_addr_prepare(url.address);
	if (inet_addr(url.address) == INADDR_NONE || inet_addr(url.address) == INADDR_ANY)
	{
		struct hostent * h = gethostbyname(url.address);
		if (!h){
			interface_showMessageBox(_T("ERR_INCORRECT_IP"), thumbnail_error, 0);
			return -1;
		}
		strcpy(url.address, inet_ntoa(*(struct in_addr*)h->h_addr));
		//eprintf ("%s: %s\n", __FUNCTION__, url.address);
	}

	strcpy(sinfo.ip, url.address);
	sinfo.port = url.port;
	strcpy(sinfo.streamname, url.stream);
	sinfo.custom_url = 1;
	memcpy(&stream_info, &sinfo, sizeof(rtsp_stream_info));

	if( !description )
		snprintf(appControlInfo.playbackInfo.description, sizeof(appControlInfo.playbackInfo.description),
			"%s: rtsp://%s:%d/%s", _T("MOVIE"), stream_info.ip, stream_info.port, stream_info.streamname);
	else
		strcpy(appControlInfo.playbackInfo.description, description);
	if( !thumbnail )
		appControlInfo.playbackInfo.thumbnail[0] = 0;
	else
		strcpy(appControlInfo.playbackInfo.thumbnail, thumbnail);

	dprintf("%s: Change to %s\n", __FUNCTION__, sinfo.streamname);

	return rtsp_stream_change(pMenu, CHANNEL_INFO_SET(which, CHANNEL_CUSTOM));
}

int rtsp_playURL(int which, const char *URL, const char* description, const char* thumbnail)
{
	return rtsp_setChannelFromURL(interfaceInfo.currentMenu, URL, description, thumbnail, SET_NUMBER(which));
}

int rtsp_setChannelFromUserURL(interfaceMenu_t *pMenu, char *value, void* pArg)
{
	appControlInfo.playbackInfo.playlistMode = playlistModeNone;
	return rtsp_setChannelFromURL(pMenu, value, NULL, NULL, pArg);
}

static char *rtsp_urlvalue(int num, void *pArg)
{
	return num == 0 ? rtsp_lasturl : NULL;
}

static int rtsp_enterURL(interfaceMenu_t *pMenu, void* pArg)
{
	interface_getText(pMenu, _T("ENTER_CUSTOM_RTSP_URL"), "\\w+", rtsp_setChannelFromUserURL, rtsp_urlvalue, inputModeABC, pArg);
	return 0;
}

void rtsp_cleanupMenu()
{
	if ( pstream_head != NULL && *ppstream_head != NULL )
	{
		clean_list(*ppstream_head);
		*ppstream_head = NULL;
	}

	mysem_destroy(rtsp_semaphore);
}

void rtsp_buildMenu(interfaceMenu_t *pParent)
{
	int rtsp_icons[4] = { 0, 0, statusbar_f3_add, statusbar_f4_enterurl };

	createListMenu(&rtspStreamMenu, _T("MOVIE_LIST"), thumbnail_vod, rtsp_icons, pParent,
					   /* interfaceInfo.clientX, interfaceInfo.clientY,
					   interfaceInfo.clientWidth, interfaceInfo.clientHeight,*/ interfaceListMenuIconThumbnail,
					   NULL, NULL, NULL);
	interface_setCustomKeysCallback((interfaceMenu_t*)&rtspStreamMenu, rtsp_keyCallback);

	mysem_create(&rtsp_semaphore);

	memset(&stream_info, 0, sizeof(rtsp_stream_info));

#ifdef ENABLE_TEST_MODE
	{
		char desc[MENU_ENTRY_INFO_LENGTH];
		strcpy(stream_info.streamname, "a.mpg");
		strcpy(stream_info.ip, "192.168.200.1");
		stream_info.port = 554;
		stream_info.custom_url = 1;
		sprintf(desc, "%s: %s", _T("MOVIE"), "rtsp://192.168.200.1:554/a.mpg");
		interface_playControlSetup(rtsp_play_callback, CHANNEL_INFO_SET(0, CHANNEL_CUSTOM), interfacePlayControlStop|interfacePlayControlPause|interfacePlayControlRewind|interfacePlayControlFastForward|interfacePlayControlPlay|interfacePlayControlPrevious|interfacePlayControlNext, desc, thumbnail_vod);
		/*sprintf(desc, "%s: rtsp://%s:%d/%s", _T("MOVIE"), stream_info.ip, stream_info.port, stream_info.streamname);
		interface_playControlSetup(rtsp_play_callback, NULL, CHANNEL_INFO_SET(0, CHANNEL_CUSTOM), interfacePlayControlStop|interfacePlayControlPause|interfacePlayControlRewind|interfacePlayControlFastForward|interfacePlayControlPlay, desc, IMAGE_DIR "thumbnail_vod.png", 0);*/
	}
#endif // #ifdef ENABLE_TEST_MODE

	//rtsp_fillMenu();
}

static int rtsp_keyCallback(interfaceMenu_t *pMenu, pinterfaceCommandEvent_t cmd, void* pArg)
{
	unsigned int streamNumber;
	char URL[MAX_URL];

	if( cmd->command == interfaceCommandBlue )
	{
		rtsp_enterURL(pMenu, (void*)screenMain);
		return 0;
	}

	if(pMenu->selectedItem < 0)
	{
		return 1;
	}

	streamNumber = CHANNEL_INFO_GET_CHANNEL(pArg);

	if (GET_NUMBER(pArg) >= 0)
	{
		if (cmd->command == interfaceCommandYellow || cmd->command == interfaceCommandGreen)
		{
			if ( pstream_head == NULL ) {
				int ret=get_rtsp_streams(ppstream_head);
				if (ret != 0 || pstream_head == NULL) {
					eprintf("%s: Failed to find default stream\n", __FUNCTION__);
					interface_showMessageBox(_T("ERR_DEFAULT_STREAM"), thumbnail_warning, 0);
					return 0;
				}
			}
			streams_struct* stream_ptr = get_stream(streamNumber);
			if ( !stream_ptr ) {
				interface_showMessageBox(_T("ERR_STREAM_IN_LIST"), thumbnail_error, 0);
				eprintf("RTSP: Stream number not found in linked list\n");
				return 0;
			}

			sprintf(URL, "rtsp://%s:%d/%s", appControlInfo.rtspInfo.streamIP, appControlInfo.rtspInfo.RTSPPort, stream_ptr->stream);
			if( cmd->command == interfaceCommandYellow )
			{
				eprintf("RTSP: Add to Playlist '%s'\n",URL);
				playlist_addUrl(URL, stream_ptr->name ? stream_ptr->name : stream_ptr->stream);
			} else if (cmd->command == interfaceCommandGreen ||
			           cmd->command == interfaceCommandInfo)
			{
				eprintf("RTSP: Stream %03d: '%s'\n", streamNumber, URL);
				if( stream_ptr->poster && stream_ptr->poster[0] )
				{
					interface_showPosterBox( stream_ptr->description,
					                         stream_ptr->name ? stream_ptr->name : stream_ptr->stream,
					                         INTERFACE_BORDER_RED, INTERFACE_BORDER_GREEN, INTERFACE_BORDER_BLUE, INTERFACE_BORDER_ALPHA,
					                         thumbnail_vod,
					                         stream_ptr->poster, NULL, SET_NUMBER(streamNumber));
				}
				//interface_showMessageBox(URL, thumbnail_info, 0);
			}
			return 0;
		}
	}

	return 1;
}
