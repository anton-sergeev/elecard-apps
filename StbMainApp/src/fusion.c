/*
 fusion.c

Copyright (C) 2015  Elecard Devices

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
 
#ifdef ENABLE_FUSION
#include "fusion.h"
#include <directfb.h>

#define eprintf(x...) \
	do { \
		time_t __ts__ = time(NULL); \
		struct tm *__tsm__ = localtime(&__ts__); \
		printf("[%02d:%02d:%02d]: ", __tsm__->tm_hour, __tsm__->tm_min, __tsm__->tm_sec); \
		printf(x); \
	} while(0)

#ifdef FUSION_ENCRYPTED
#define FUSION_TODAY_PLAYLIST_PATH "opened/fusion/"FUSION_PLAYLIST_NAME
#else
#define FUSION_TODAY_PLAYLIST_PATH "fusion/"FUSION_PLAYLIST_NAME
#endif

static char g_usbRoot[PATH_MAX] = {0};
static int gStatus = 0;
int g_shaping = FUSION_DEFAULT_BANDLIM_KBYTE;
static char g_adsPath[PATH_MAX] = {0};

interfaceFusionObject_t FusionObject;

typedef size_t (*curlWriteCB)(void *buffer, size_t size, size_t nmemb, void *userp);

int32_t fusion_checkDirectory(const char *path);
int fusion_getMostAppropFile (char * directory, char * pathEnd, int i);
int fusion_checkFileIsDownloaded(char * remotePath, char * localPath);
int fusion_checkLastModified (char * url);
int fusion_checkResponse (char * curlStream, cJSON ** ppRoot);
int fusion_checkSavedPlaylist();
size_t fusion_curlCallback(char * data, size_t size, size_t nmemb, void * stream);
size_t fusion_curlCallbackVideo(char * data, size_t size, size_t nmemb, void * stream);
int fusion_downloadPlaylist(char * url, cJSON ** ppRoot);
int fusion_getCommandOutput (char * cmd, char * result);
int fusion_getAndParsePlaylist ();
CURLcode fusion_getDataByCurl (char * url, char * curlStream, int * pStreamLen, curlWriteCB cb);
void fusion_getLocalFirmwareVer();
long fusion_getRemoteFileSize(char * url);
int fusion_getSecret ();
int fusion_getUsbRoot();
CURLcode fusion_getVideoByCurl (char * url, void * fsink/*char * curlStream*/, int * pStreamLen, curlWriteCB cb);
void fusion_ms2timespec(int ms, struct timespec * ts);
int fusion_readConfig();
int fusion_refreshDtmfEvent(void *pArg);
int fusion_removeFirmwareFormFlash();
void fusion_removeHwconfigUrl();
int fusion_setMoscowDateTime();
void * fusion_threadCheckReboot (void * param);
void * fusion_threadGetCreepline(void * param);
void * fusion_threadMonitorCreep(void * param);
void * fusion_threadDownloadFirmware(void * param);
void * fusion_threadFlipCreep (void * param);
void fusion_wait (unsigned int timeout_ms);

int fusion_removeAdLockFile();

int fusion_getUtcWithWget (char * utcBuffer, int size);
int fusion_getUtcFromTimeapi (char * utcBuffer, int size);
int fusion_getUtcFromEarthtools (char * utcBuffer, int size);
int fusion_getUtcFromCustomServiceByWget (char * customRequest, char * utcBuffer, int size);

static char helper_checkAdsOnUsb ();
int fusion_makeAdsPathFromUrl (char * fileUrl, char * resultPath, int duration, int size);
int fusion_saveFileByWget (char * url, char * filepath, int dtmf);
int fusion_checkAdIsComplete(char * filepath);
int fusion_waitAdIsDownloaded (char * filepath, int dtmf);
int fusion_savePlaylistToFile(char * path, char * playlistBuffer, int size);

extern int  helperParseLine(const char *path, const char *cmd, const char *pattern, char *out, char stopChar);

void fusion_ms2timespec(int ms, struct timespec * ts)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec + ms / 1000;
    ts->tv_nsec = tv.tv_usec * 1000 + (ms % 1000) * 1000000;
    if (ts->tv_nsec >= 1000000000)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

static int helper_fileExists (const char* filename)
{
	int file;
	int ret = 0;
	file = open(filename, O_RDONLY);
	if(file >= 0) {
		close(file);
		ret = 1;
	}
	return ret;
}

void fusion_wait (unsigned int timeout_ms)
{
	struct timespec ts;
	pthread_cond_t condition;
	pthread_mutex_t mutex;

	pthread_cond_init(&condition, NULL);
	pthread_mutex_init(&mutex, NULL);

	pthread_mutex_lock(&mutex);

	fusion_ms2timespec(timeout_ms, &ts);
	pthread_cond_timedwait(&condition, &mutex, &ts);
	pthread_mutex_unlock(&mutex);

	pthread_cond_destroy(&condition);
	pthread_mutex_destroy(&mutex);
	return;
}

int fusion_getCommandOutput (char * cmd, char * result)
{
	FILE *fp;
	char line[PATH_MAX];
	char * movingPtr;

	if (cmd == NULL) return -1;
	fp = popen(cmd, "r");
	if (fp == NULL) {
		eprintf("%s: Failed to run command %s\n", __FUNCTION__, cmd);
		return -1;
	}

	movingPtr = result;
	while (fgets(line, sizeof(line)-1, fp) != NULL) {
		sprintf(movingPtr, "%s", line);
		movingPtr += strlen(line);
		//eprintf ("%s: result = %s\n", __FUNCTION__, result);
	}
	pclose(fp);
	return 0;
}

void * fusion_threadFlipCreep (void * param)
{
	while (1)
	{
		interface_updateFusionCreepSurface();
		fusion_wait(10);
	}
	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

void * fusion_threadDownloadFirmware(void * param)
{
	char filepath[PATH_MAX];

	char * url = (char*)param;
	if (!url || !strlen(url)){
		eprintf ("%s: ERROR! Invalid url.\n", __FUNCTION__);
		pthread_exit((void *)&gStatus);
		return (void*)NULL;
	}
	if (strlen(g_usbRoot) == 0){
		if (fusion_getUsbRoot() == 0){
			eprintf ("%s: ERROR! No flash found.\n", __FUNCTION__);
			pthread_exit((void *)&gStatus);
			return (void*)NULL;
		}
	}
	char cmd [PATH_MAX];
	char * ptrFilename = strstr(url, "STB8");
	if (!ptrFilename){
		eprintf ("%s: ERROR! Incorrect firmware url (%s).\n", __FUNCTION__, url);
		pthread_exit((void *)&gStatus);
		return (void*)NULL;
	}

	sprintf (filepath, "%s/%s", g_usbRoot, ptrFilename);

	// check if firmware already downloaded - not nessesary, 
	// because we check existance on FusionObject.firmware before thread starts
/*	FILE * f = fopen(filepath, "rb");
	if (f){
		long int localFileSize = 0;
		fseek(f, 0, SEEK_END);
		localFileSize = ftell(f);
		fclose(f);

		if (localFileSize){
			// check if filesize is eaual to remote filesize
			int remoteSize = fusion_getRemoteFileSize(url);
			if (localFileSize == remoteSize){
				eprintf ("%s: Firmware is already downloaded.\n", __FUNCTION__);
				pthread_exit((void *)&gStatus);
				return (void*)NULL;
			}
		}
	}
	*/
	eprintf ("%s: Save %s to %s ...\n", __FUNCTION__, url, filepath);
	//sprintf (cmd, "wget --limit-rate=%dk -c -q \"%s\" -O %s ", g_shaping, url, filepath); // 2>/dev/null, quiet
	//sprintf (cmd, "/usr/sbin/wget1.12 --limit-rate=%dk -c -q \"%s\" -O %s ", g_shaping, url, filepath); // 2>/dev/null, quiet
	sprintf (cmd, "wget -c -q \"%s\" -O %s ", url, filepath); // 2>/dev/null, quiet
	system(cmd);

	eprintf ("%s(%d): Exit.\n", __FUNCTION__, __LINE__);

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

int fusion_isRemoteFirmwareBetter(char * localFirmwareVer, char * remoteFirmwareVer)
{
	int remoteMon, remoteDay, remoteYear, remoteHour, remoteMin;
	int localMon, localDay, localYear, localHour, localMin;
	int scannedItemsRemote, scannedItemsLocal;
	if (!remoteFirmwareVer) return NO;
	// eg. 201407251445
	scannedItemsRemote = sscanf(remoteFirmwareVer, "%04d%02d%02d%02d%02d", &remoteYear, &remoteMon, &remoteDay, &remoteHour, &remoteMin);
	scannedItemsLocal = sscanf(localFirmwareVer,  "%04d%02d%02d%02d%02d", &localYear, &localMon, &localDay, &localHour, &localMin);
	do {
		//if (scannedItemsRemote != 5 || scannedItemsLocal != 5) break;
		if (scannedItemsRemote != 5) break;
		if (remoteYear < localYear) break;
		if (remoteYear == localYear && remoteMon < localMon) break;
		if (remoteYear == localYear && remoteMon == localMon && remoteDay < localDay) break;
		if (remoteYear == localYear && remoteMon == localMon && remoteDay == localDay && remoteHour < localHour) break;
		if (remoteYear == localYear && remoteMon == localMon && remoteDay == localDay && remoteHour == localHour && remoteMin <= localMin) break;
		//eprintf ("%s(%d): yes (%s vs %s)\n", __FUNCTION__, __LINE__, remoteFirmwareVer, localFirmwareVer);
		return YES;
	} while (0);
	return NO;
}

int fusion_isServerUnavailable ()
{
	char result [512] = {0};
	fusion_getCommandOutput ("ping -c 3 public.tv", result); //  | grep \", 0% packet loss\"
	if (strstr(result, ", 0% packet loss") != NULL) return NO;
	eprintf ("%s(%d): PING Failed. result = %s\n", __FUNCTION__, __LINE__, result);
	return YES;
}

void * fusion_threadCheckReboot (void * param)
{
	time_t now;
	struct tm nowDate, rebootDate;

	while (1)
	{
		time (&now);
		nowDate = *localtime (&now);

		rebootDate.tm_year = nowDate.tm_year;
		rebootDate.tm_mon = nowDate.tm_mon;
		rebootDate.tm_mday = nowDate.tm_mday;
		sscanf(FusionObject.reboottime, "%02d:%02d:%02d", &rebootDate.tm_hour, &rebootDate.tm_min, &rebootDate.tm_sec);
/*
		eprintf ("%s(%d): Reboot date is %04d-%02d-%02d %02d:%02d:%02d, now = %02d:%02d:%02d %02d:%02d:%02d, checktime = %d\n", 
					  __FUNCTION__, __LINE__, 
					rebootDate.tm_year, rebootDate.tm_mon, rebootDate.tm_mday,
					rebootDate.tm_hour, rebootDate.tm_min, rebootDate.tm_sec,
					nowDate.tm_year, nowDate.tm_mon, nowDate.tm_mday,
					nowDate.tm_hour, nowDate.tm_min, nowDate.tm_sec, 
					FusionObject.checktime);
*/
		double diff = difftime(mktime(&rebootDate), mktime(&nowDate));
		//eprintf ("%s(%d): diff = %f\n", __FUNCTION__, __LINE__, diff);
		if ((diff > 0) && (diff <= 60)){
			if (strlen(FusionObject.reboottime) && 
				(strncmp(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer, FUSION_FIRMWARE_VER_LEN) != 0) && 
				(fusion_isRemoteFirmwareBetter(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer) == YES) ||
				(fusion_isServerUnavailable() == YES))
			{
				//eprintf ("%s(%d): remoteTimestamp = %s, localTimestamp = %s, compare res = %d\n", __FUNCTION__, __LINE__, FusionObject.remoteFirmwareVer, FusionObject.localFirmwareVer,
				//	strncmp(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer, FUSION_FIRMWARE_VER_LEN));
				eprintf ("%s(%d): Reboot NOW.\n", __FUNCTION__, __LINE__);
				system ("reboot");
				break;
			}
		}

		fusion_wait(20 * 1000);
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

void fusion_clearMemCache()
{
	system ("echo 3 >/proc/sys/vm/drop_caches");
}

void * fusion_threadMonitorCreep(void * param)
{
	while (1)
	{
		fusion_wait(1000);

		if (FusionObject.creep.status == FUSION_FAIL || FusionObject.creep.status == FUSION_OK){
			continue;
		}
		/*
		else if (FusionObject.creep.status == FUSION_NEW_CREEP){
			gettimeofday(&tv, NULL);
			FusionObject.creep.startTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			eprintf ("%s(%d): FusionObject.creep.status = FUSION_NEW_CREEP. startTime = %lld.\n", __FUNCTION__, __LINE__, FusionObject.creep.startTime);
			//eprintf ("%s(%d): New creep got. Start it.\n", __FUNCTION__, __LINE__);
			FusionObject.creep.deltaTime = 0;
			FusionObject.creep.status = FUSION_SAME_CREEP;
		}*/
		else if (FusionObject.creep.status == FUSION_SAME_CREEP && FusionObject.creep.isShown)
		{
			struct timeval tv;
			//eprintf ("%s(%d): FUSION_SAME_CREEP. isShown = 1. Wait pause = %d sec and start again.\n", __FUNCTION__, __LINE__, FusionObject.creep.pause);
			fusion_wait(FusionObject.creep.pause * 1000);
			gettimeofday(&tv, NULL);
			FusionObject.creep.startTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			FusionObject.creep.isShown = 0;
			FusionObject.creep.deltaTime = 0;
		}
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

void * fusion_threadGetCreepline(void * param)
{
	fusion_readConfig();

	while (1)
	{
		FusionObject.creep.status = fusion_getAndParsePlaylist();

		if (FusionObject.creep.status == FUSION_NEW_CREEP)
		{
			struct timeval tv;
			gettimeofday(&tv, NULL);
			FusionObject.creep.startTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			//eprintf ("%s(%d): FUSION_NEW_CREEP -> FUSION_SAME_CREEP. Set iShown = 0. startTime = %lld.\n", __FUNCTION__, __LINE__, FusionObject.creep.startTime);
			FusionObject.creep.deltaTime = 0;
			FusionObject.creep.isShown = 0;	// test
			FusionObject.creep.status = FUSION_SAME_CREEP;
		}/*
		else if (FusionObject.creep.status == FUSION_SAME_CREEP && FusionObject.creep.isShown)
		{
			eprintf ("%s(%d): FusionObject.creep.status = FUSION_SAME_CREEP, creepShown got. Wait pause = %d sec and start again.\n", __FUNCTION__, __LINE__, FusionObject.creep.pause);
			fusion_wait(FusionObject.creep.pause * 1000);
			gettimeofday(&tv, NULL);
			FusionObject.creep.startTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
			FusionObject.creep.isShown = 0;
			FusionObject.creep.deltaTime = 0;
		}*/

		fusion_clearMemCache();
		fusion_wait(FusionObject.checktime * 1000);
	}

	pthread_exit((void *)&gStatus);
	return (void*)NULL;
}

int fusion_readConfig()
{
	char * ptr;
	char line [512];
	FILE * f;

	FusionObject.checktime = FUSION_DEFAULT_CHECKTIME;
	sprintf (FusionObject.server, "%s", FUSION_DEFAULT_SERVER_PATH);
	sprintf (FusionObject.utcUrl, "%s", FUSION_DEFAULT_UTC_URI);

	FusionObject.logoTopLeftX  = 100;
	FusionObject.logoTopLeftY  = 100;
	FusionObject.logoTopRightX = -1;
	FusionObject.logoTopRightY = 100;
	FusionObject.logoBotLeftX  = 100;
	FusionObject.logoBotLeftY  = interfaceInfo.screenHeight - 200;
	FusionObject.logoBotRightX = -1;
	FusionObject.logoBotRightY = interfaceInfo.screenHeight - 200;
	FusionObject.creepY = interfaceInfo.screenHeight - FUSION_SURF_HEIGHT;

	FusionObject.demoUrl[0] = '\0';

	f = fopen(FUSION_HWCONFIG, "rt");
	if (!f) return -1;

	while (!feof(f)){
		fgets(line, 256, f);
		if (feof(f)){
			break;
		}
		line[strlen(line)-1] = '\0';
		ptr = NULL;
		if ((ptr = strcasestr((const char*)line, (const char*)"SERVER ")) != NULL){
			ptr += 7;
			sprintf (FusionObject.server, "%s", ptr);
			eprintf (" %s: server = %s\n",   __FUNCTION__, FusionObject.server);
		}else if ((ptr = strcasestr((const char*) line, (const char*)"CHECKTIME ")) != NULL){
			ptr += 10;
			FusionObject.checktime = atoi(ptr);
			eprintf (" %s: checktime = %d\n",   __FUNCTION__, FusionObject.checktime);
		}else if ((ptr = strcasestr((const char*) line, (const char*)"DEMO ")) != NULL){
			ptr += 5;
			sprintf (FusionObject.demoUrl, "%s", ptr);
			eprintf (" %s: demo url = %s\n",   __FUNCTION__, FusionObject.demoUrl);
		}
		// --------- logo coord section ----------------------------- //
		else if ((ptr = strcasestr((const char*) line, (const char*)"TOPLEFTX ")) != NULL){
			ptr += 9;
			FusionObject.logoTopLeftX = atoi(ptr);
			eprintf (" %s: topleft logo x = %d\n",   __FUNCTION__, FusionObject.logoTopLeftX);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"TOPLEFTY ")) != NULL){
			ptr += 9;
			FusionObject.logoTopLeftY = atoi(ptr);
			eprintf (" %s: topleft logo y = %d\n",   __FUNCTION__, FusionObject.logoTopLeftY);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"TOPRIGHTX ")) != NULL){
			ptr += 10;
			FusionObject.logoTopRightX = atoi(ptr);
			eprintf (" %s: topright logo x = %d\n",   __FUNCTION__, FusionObject.logoTopRightX);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"TOPRIGHTY ")) != NULL){
			ptr += 10;
			FusionObject.logoTopRightY = atoi(ptr);
			eprintf (" %s: topright logo y = %d\n",   __FUNCTION__, FusionObject.logoTopRightY);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"BOTLEFTX ")) != NULL){
			ptr += 9;
			FusionObject.logoBotLeftX = atoi(ptr);
			eprintf (" %s: bottomleft logo x = %d\n",   __FUNCTION__, FusionObject.logoBotLeftX);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"BOTLEFTY ")) != NULL){
			ptr += 9;
			FusionObject.logoBotLeftY = atoi(ptr);
			eprintf (" %s: bottomleft logo y = %d\n",   __FUNCTION__, FusionObject.logoBotLeftY);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"BOTRIGHTX ")) != NULL){
			ptr += 10;
			FusionObject.logoBotRightX = atoi(ptr);
			eprintf (" %s: bottomright logo x = %d\n",   __FUNCTION__, FusionObject.logoBotRightX);
		}
		else if ((ptr = strcasestr((const char*) line, (const char*)"BOTRIGHTY ")) != NULL){
			ptr += 10;
			FusionObject.logoBotRightY = atoi(ptr);
			eprintf (" %s: bottomright logo y = %d\n",   __FUNCTION__, FusionObject.logoBotRightY);
		}
		// --------- end logo coord section ------------------------- //
		else if ((ptr = strcasestr((const char*) line, (const char*)"CREEPY ")) != NULL){
			ptr += 7;
			FusionObject.creepY = atoi(ptr);
			eprintf (" %s: creep y = %d\n",   __FUNCTION__, FusionObject.creepY);
		}
		// --------- end creep coord section ------------------------- //
		else if ((ptr = strcasestr((const char*) line, (const char*)"LIMIT ")) != NULL){
			ptr += 6;
			g_shaping = atoi(ptr);
			eprintf (" %s: band limit = %d\n",   __FUNCTION__, g_shaping);
		}
		//---------- UTC web service url ----------------------------- //
		else if ((ptr = strcasestr((const char*) line, (const char*)"UTC ")) != NULL){
			ptr += 4;
			sprintf (FusionObject.utcUrl, "%s", ptr);
			eprintf (" %s: UTC datetime server URI = %s\n",   __FUNCTION__, FusionObject.utcUrl);
		}
		//---------- end UTC web service url ------------------------- //
	}
	fclose (f);
	return 0;
}

int fusion_refreshDtmfEvent(void *pArg)
{
	if (FusionObject.audHandle == 0){
		FILE * faudhandle = fopen ("/tmp/fusion.audhandle", "rb");
		if (faudhandle){
			fread (&FusionObject.audHandle, sizeof(unsigned int), 1, faudhandle);
			fclose (faudhandle);
			if (FusionObject.audHandle) eprintf("%s(%d): Got FusionObject.audHandle = %d.\n", __FUNCTION__, __LINE__, FusionObject.audHandle);
		}
	}
	else {
		int staudlx_fd;
		STAUD_Ioctl_GetDtmf_t UserData;

		if ((staudlx_fd = open("/dev/stapi/staudlx_ioctl", O_RDWR)) < 0) {
			eprintf ("%s(%d): ERROR! /dev/stapi/staudlx_ioctl open failed.\n",   __FUNCTION__, __LINE__);
		}
		else {
			memset (&UserData, 0, sizeof(STAUD_Ioctl_GetDtmf_t));
			UserData.Handle = FusionObject.audHandle;
			UserData.ErrorCode = 0;
			UserData.count = 0;

			if (ioctl (staudlx_fd, 0xc004c0e4, &UserData)){   // _IOWR(0X16, 228, NULL)   // STAUD_IOC_GETDTMF
				eprintf ("%s(%d): ERROR! ioctl failed.\n",   __FUNCTION__, __LINE__);
			}
			else {
				if ((UserData.ErrorCode == 0) && (UserData.digits[0] != ' '))
				{
					//eprintf ("%s(%d): ioctl rets %c\n", __FUNCTION__, __LINE__, UserData.digits[0]);	// todo : return it!

					pthread_mutex_lock(&FusionObject.mutexDtmf);
					FusionObject.currentDtmfDigit = UserData.digits[0];
					pthread_mutex_unlock(&FusionObject.mutexDtmf);
/*
					pthread_mutex_lock(&FusionObject.mutexDtmf);
					if (strlen(FusionObject.currentDtmfMark) == 0){
						struct timeval tv;
						gettimeofday(&tv, NULL);
						FusionObject.dtmfStartTime = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
					}
					// add char to end
					if (strlen(FusionObject.currentDtmfMark) >= 5){
						memset (FusionObject.currentDtmfMark, 0, sizeof(FusionObject.currentDtmfMark));
					}
					FusionObject.currentDtmfMark[strlen(FusionObject.currentDtmfMark)] = FusionObject.currentDtmfDigit;

					pthread_mutex_unlock(&FusionObject.mutexDtmf);
					eprintf ("%s(%d): currentDtmfMark = %s\n", __FUNCTION__, __LINE__, FusionObject.currentDtmfMark);
					* */
				}
				else 
				{
					pthread_mutex_lock(&FusionObject.mutexDtmf);
					FusionObject.currentDtmfDigit = '_';
					pthread_mutex_unlock(&FusionObject.mutexDtmf);
				}
				interface_displayDtmf();
			}
		}
		close (staudlx_fd);
	}

	interface_addEvent(fusion_refreshDtmfEvent, (void*)NULL, FUSION_REFRESH_DTMF_MS, 1);
	return 0;
}

int fusion_getUsbRoot()
{
	int hasDrives = 0;
	DIR *usbDir = opendir(USB_ROOT);
	if (usbDir != NULL) {
		struct dirent *first_item = NULL;
		struct dirent *item = readdir(usbDir);
		while (item) {
			if(strncmp(item->d_name, "sd", 2) == 0) {
				hasDrives++;
				if(!first_item)
					first_item = item;
			}
			item = readdir(usbDir);
		}
		if (hasDrives == 1) {
			sprintf(g_usbRoot, "%s%s", USB_ROOT, first_item->d_name);
			eprintf("%s: Found %s\n", __FUNCTION__, g_usbRoot);
			closedir(usbDir);
			return 1;  // yes
		}
		closedir(usbDir);
	}
	else {
		eprintf("%s: opendir %s failed\n", __FUNCTION__, USB_ROOT);
	}
	return 0; // no
}

int fusion_removeFirmwareFormFlash()
{
	char command[PATH_MAX];
	if (strlen(g_usbRoot) == 0){
		if (fusion_getUsbRoot() == 0) return 0; // no
	}
	int result = fusion_checkDirectory(g_usbRoot);
	if (result == 1) // yes
	{
		// remove all *.efp to this folder to prevent firmware update on reboot
		sprintf (command, "rm %s/*.efp", g_usbRoot);
		system (command);
	}
	return result;
}

void fusion_removeHwconfigUrl()
{
	system ("hwconfigManager f 0 UPURL");
	return;
}

void fusion_getLocalFirmwareVer()
{
	//FusionObject.localFirmwareVer[0] = '\0';
	memset(FusionObject.localFirmwareVer, '\0', FUSION_FIRMWARE_VER_LEN);
	fusion_getCommandOutput ("cat /firmwareDesc | grep \"pack name:\" | tr -s ' ' | cut -d'.' -f3", FusionObject.localFirmwareVer);  // eg. 201406111921
	FusionObject.localFirmwareVer[strlen (FusionObject.localFirmwareVer)-1] = '\0';
	eprintf ("%s(%d): local firmware version = %s\n", __FUNCTION__, __LINE__, FusionObject.localFirmwareVer);
}

void fusion_fakeRestart()
{
	FusionObject.mode = FUSION_MODE_UNDEF;
	return;
}

void fusion_startup()
{
#ifdef FUSION_EXT2
	system ("/opt/elecard/bin/mkfs_ext3.sh");
#endif

#ifdef FUSION_ENCRYPTED
	system ("/opt/elecard/bin/mount_encrypted.sh");
#endif

	system ("echo 3 >/proc/sys/vm/drop_caches");

	system ("/opt/elecard/bin/elcd-watchdog.sh &");

	fusion_readConfig();

	fusion_removeAdLockFile();

	if (fusion_setMoscowDateTime() != 0){
		eprintf ("%s(%d): WARNING! use preset datetime.\n", __FUNCTION__, __LINE__);
	}

	memset(&FusionObject, 0, sizeof(interfaceFusionObject_t));

	fusion_getSecret();
	fusion_removeFirmwareFormFlash();
	fusion_removeHwconfigUrl();
	fusion_getLocalFirmwareVer();

	pthread_mutex_init(&FusionObject.creep.mutex, NULL);
	pthread_mutex_init(&FusionObject.mutexLogo, NULL);
	pthread_mutex_init(&FusionObject.mutexDtmf, NULL);

	//interface_addEvent(fusion_refreshDtmfEvent, (void*)NULL, FUSION_REFRESH_DTMF_MS, 1);
	FusionObject.currentDtmfDigit = '_';
	FusionObject.creep.isShown = 1;

	pthread_create(&FusionObject.threadGetCreepHandle, NULL, fusion_threadGetCreepline, (void*)NULL);
	pthread_create(&FusionObject.threadMonCreepHandle, NULL, fusion_threadMonitorCreep, (void*)NULL);
	pthread_create(&FusionObject.threadCheckReboot, NULL, fusion_threadCheckReboot, (void*)NULL);
	pthread_create(&FusionObject.threadFlipCreep, NULL, fusion_threadFlipCreep, (void*)NULL);

	return;
}

int fusion_getSecret ()
{
	char mac [16];
	char input[64];
	char output[64];
	int i;
	
	mac[0] = 0;
	input[0] = 0;
	output[0] = 0;
	memset (FusionObject.secret, 0, sizeof(FusionObject.secret));

	FILE *f = popen ("cat /sys/class/net/eth0/address | tr -d ':'", "r");
	if (!f) return -1;

	fgets(mac, sizeof(mac), f);
	pclose(f);
	mac[strlen(mac) - 1] = '\0';

	strcpy(input, FUSION_SECRET);
	strcat(input, mac);

	/* Get MD5 sum of input and convert it to hex string */
	md5((unsigned char*)input, strlen(input), (unsigned char*)output);
	for (i = 0; i < 16; i++)
	{
		//sprintf(&FusionObject.secret[i*2], "%02hhx", output[i]);
		sprintf((char*)&FusionObject.secret[i*2], "%02hhx", output[i]);
	}

	return 0;
}

static char fusion_curlError[CURL_ERROR_SIZE];
int fusion_streamPos;
int fusion_videoStreamPos;

size_t fusion_curlCallback(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	memcpy ((char*)stream + fusion_streamPos, data, writtenBytes);
	fusion_streamPos += writtenBytes;
	return writtenBytes;
}

size_t fusion_curlCallbackVideo(char * data, size_t size, size_t nmemb, void * stream)
{
	int writtenBytes = size * nmemb;
	FILE * f = (FILE*)stream;
	fwrite(data, 1, writtenBytes, f);
	fusion_videoStreamPos += writtenBytes;
	return writtenBytes;
}

CURLcode fusion_getDataByCurl (char * url, char * curlStream, int * pStreamLen, curlWriteCB cb)
{
	CURLcode retCode = CURLE_OK;
	CURL * curl = NULL;

	if (!url || !strlen(url)) return -1;

	curl = curl_easy_init();
	if (!curl) return -1;

	*pStreamLen = 0;
	fusion_streamPos = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_curlError);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0); // to make curl resolve after net connection re-established
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	res_init();

	//eprintf ("%s: rq: %s\n", __FUNCTION__, url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl = NULL;

	if (retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_curlError);
		return retCode;
	}
	if (!curlStream || strlen(curlStream) == 0) {
		eprintf ("%s: ERROR! empty response.\n", __FUNCTION__);
		return -2;
	}
	*pStreamLen = fusion_streamPos;
	return CURLE_OK;
}

int fusion_downloadPlaylist(char * url, cJSON ** ppRoot)
{
	int res;
	int buflen;

	if (!url || !strlen(url)) return -1;

	if (!FusionObject.playlistBuffer){
		FusionObject.playlistBuffer = (char *)malloc(FUSION_STREAM_SIZE);
		if (!FusionObject.playlistBuffer){
			eprintf ("%s(%d): ERROR! Malloc failed.\n",   __FUNCTION__, __LINE__);
			return -1;
		}
	}
	memset(FusionObject.playlistBuffer, 0, FUSION_STREAM_SIZE);

	eprintf ("%s(%d): rq: %s ...\n",   __FUNCTION__, __LINE__, url);
	fusion_getDataByCurl(url, FusionObject.playlistBuffer, &buflen, (curlWriteCB)fusion_curlCallback);

	//eprintf ("ans: %.*s ...\n", 256, FusionObject.playlistBuffer);

	if (strlen(FusionObject.playlistBuffer) == 0) {
		eprintf (" %s: ERROR! empty response.\n",   __FUNCTION__);
		return -1;
	}
	res = fusion_checkResponse(FusionObject.playlistBuffer, ppRoot);

	if (res == 0){	// todo : don't save if it is the same
		char savedPath [PATH_MAX];
		sprintf (savedPath, "%s/"FUSION_TODAY_PLAYLIST_PATH, g_usbRoot);
		fusion_savePlaylistToFile(savedPath, FusionObject.playlistBuffer, buflen);
	}

	return res;
}

int fusion_checkResponse (char * curlStream, cJSON ** ppRoot)
{
	*ppRoot = cJSON_Parse(curlStream);
	if (!(*ppRoot)) {
		eprintf ("%s: ERROR! Not a JSON response. %.*s\n", __FUNCTION__, 256, curlStream);
		return -1;
	}
	return 0;
}

int fusion_setMoscowDateTime()
{
	char setDateString[64];
	char utcBuffer [1024];
	memset(utcBuffer, 0, 1024);

	if (fusion_getUtcFromCustomServiceByWget(FusionObject.utcUrl, utcBuffer, 1024) != 0) {
		eprintf ("%s(%d): WARNING! Couldn't get UTC with wget from %s.\n", __FUNCTION__, __LINE__, FusionObject.utcUrl);
		// todo : use some extra ways to get UTC
		return -1;
	}

	// format UTC: 2014-04-04 11:43:48
	sprintf (setDateString, "date -u -s \"%s\"", utcBuffer);
	eprintf ("%s: Command: %s\n", __FUNCTION__, setDateString);
	system(setDateString);

	// set timezone
	char buf[1024];
	if (helper_fileExists("/var/etc/localtime") &&
	    helperParseLine("/tmp/info.txt", "readlink " "/var/etc/localtime", "zoneinfo/", buf, 0))
	{
		eprintf ("%s: Current timezone - %s.\n", __FUNCTION__, buf);
	}
	else {
		eprintf ("%s: Set Russia/Moscow timezone.\n", __FUNCTION__);
		system("rm /var/etc/localtime");
		system("ln -s /usr/share/zoneinfo/Russia/Moscow /var/etc/localtime");
	}
	system("hwclock -w -u");

	return 0;
}


int fusion_getUtcWithWget (char * utcBuffer, int size)
{
	FILE * f;
	char request[256];
	char ans[1024];

	if (!utcBuffer) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	// timeapi.org was dead recently
	// earthtools is dead now
	// use N instead of UTC because UTC is incorrect in timeapi.org
	sprintf (request, "wget \"http://www.timeapi.org/n/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S\" -O /tmp/utc.txt 2>/tmp/wget.log");  // 2>/dev/null
	//sprintf (request, "wget \"http://www.timeapi.org/utc/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S\" -O /tmp/utc.txt 2>/tmp/wget.log");  // 2>/dev/null
	//sprintf (request, "wget \"http://www.earthtools.org/timezone/0/0\" -O /tmp/utc.txt 2>/tmp/wget.log" );
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	system (request);

	f = fopen("/tmp/wget.log", "rt");
	if (f) {
		fread(ans, size, 1, f);
		fclose(f);
		if (strstr(ans, "failed:") || strstr(ans, "ERROR")){
			eprintf ("%s(%d): Some error occured:\n%s\n",   __FUNCTION__, __LINE__, ans);
			return -1;
		}
	}
	else {
		eprintf ("%s(%d): Some error occured. No wget answer.\n",   __FUNCTION__, __LINE__);
		return -1;
	}

	f = fopen("/tmp/utc.txt", "rt");
	if (!f) {
		eprintf ("%s(%d): ERROR! Couldn't open /tmp/utc.txt.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	fread(utcBuffer, size, 1, f);
	fclose(f);

	eprintf ("ans: %s\n", utcBuffer);

	if (!strlen(utcBuffer) || !strchr(utcBuffer, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, utcBuffer);
		return -1;
	}
	return 0;
}

int fusion_getUtcFromEarthtools (char * utcBuffer, int size)
{
	char request[256];
	char ans[1024];
	int buflen;
	char * ptrStartUtc, * ptrEndUtc;
	memset(utcBuffer, 0, size);
	memset(ans, 0, 1024);

	if (!utcBuffer || size <= 0) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	// use earthtools instead timeapi.org which is currently dead
	sprintf (request, "http://www.earthtools.org/timezone/0/0" );
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	fusion_getDataByCurl(request, ans, &buflen, (curlWriteCB)fusion_curlCallback);

	eprintf ("%s(%d): ans: %s\n", __FUNCTION__, __LINE__, ans);
	if (!strlen(ans) || !strchr(ans, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, ans);
		return -1;
	}

	ptrStartUtc = strstr(ans, "<utctime>");
	if (ptrStartUtc) {
		ptrStartUtc += 9;
		ptrEndUtc = strstr(ptrStartUtc, "</utctime>");
		if (ptrEndUtc){
			snprintf (utcBuffer, (int)(ptrEndUtc - ptrStartUtc) + 1, "%s", ptrStartUtc);
		}
	}

	return 0;
}

int fusion_getUtcFromTimeapi (char * utcBuffer, int size)
{
	char request[256];
	char ans[1024];
	int buflen;
	memset(utcBuffer, 0, size);
	memset(ans, 0, 1024);

	if (!utcBuffer || size <= 0) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	//sprintf (request, "wget \"http://www.timeapi.org/utc/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S\" -O /tmp/utc.txt");  // 2>/dev/null
	sprintf (request, "http://www.timeapi.org/utc/now?format=%%25Y-%%25m-%%25d%%20%%20%%25H:%%25M:%%25S");
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	fusion_getDataByCurl(request, ans, &buflen, (curlWriteCB)fusion_curlCallback);

	eprintf ("%s(%d): ans: %s\n", __FUNCTION__, __LINE__, ans);
	if (!strlen(ans) || !strchr(ans, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, ans);
		return -1;
	}
	snprintf (utcBuffer, min((unsigned)size, strlen (ans)), "%s", ans);
	eprintf ("%s(%d): utcBuffer: %s\n", __FUNCTION__, __LINE__, utcBuffer);
	return 0;
}

char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}

int fusion_getUtcFromCustomServiceByWget (char * customRequest, char * utcBuffer, int size)
{
	FILE * f;
	char request[1024];
	char ans[1024];
	char log[1024];

	if (!utcBuffer || !customRequest) {
		eprintf ("%s(%d): WARNING! Invalid arg.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	sprintf (request, "wget \"%s\" -O /tmp/utc.txt 2>/tmp/wget.log", customRequest);  // 2>/dev/null
	eprintf ("%s(%d): rq:  %s...\n",   __FUNCTION__, __LINE__, request);
	system (request);

	f = fopen("/tmp/wget.log", "rt");
	if (f) {
		fread(log, size, 1, f);
		fclose(f);
		if (strstr(log, "failed:") || strstr(log, "ERROR")){
			eprintf ("%s(%d): Some error occured:\n%s\n",   __FUNCTION__, __LINE__, log);
			return -1;
		}
	}
	else {
		eprintf ("%s(%d): Some error occured. No wget answer.\n",   __FUNCTION__, __LINE__);
		return -1;
	}

	f = fopen("/tmp/utc.txt", "rt");
	if (!f) {
		eprintf ("%s(%d): ERROR! Couldn't open /tmp/utc.txt.\n",   __FUNCTION__, __LINE__);
		return -1;
	}
	memset(ans, 0, 1024);
	fread(ans, 1024, 1, f);
	fclose(f);

	char * trimmed = trimwhitespace(ans);

	char * slashPtr = strstr(trimmed, "</");
	if (slashPtr != NULL) {	// contains xml
		// remove xml stuff from buffer
		// assume format is like 
		// <dateTime xmlns="http://tempuri.org/">2017-01-18T07:22:05.69Z</dateTime>
		slashPtr[0] = '\0';
		slashPtr = strrchr(trimmed, '>');
		if (slashPtr != NULL){
			strcpy(utcBuffer, slashPtr+1);
			if (utcBuffer[strlen(utcBuffer) - 1] == 'Z'){
				utcBuffer[strlen(utcBuffer) - 1] = '\0';
			}
			if ((slashPtr = strrchr(utcBuffer, 'T')) != NULL){
				slashPtr[0] = ' ';
			}
		}
		else {
			eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, ans);
			return -1;
		}
	}
	else {
		// plain text
		if (strlen(trimmed) > 32){
			eprintf ("%s(%d): ERROR! Incorrect answer: %s\n", __FUNCTION__, __LINE__, ans);
			return -1;
		}
		int y, m, d, h, min, s;
		if (sscanf(trimmed, "%04d-%02d-%02d %02d:%02d:%02d", &y, &m, &d, &h, &m, &s) != 6){
			eprintf ("%s(%d): ERROR! Incorrect format of answer: %s\n", __FUNCTION__, __LINE__, trimmed);
		}
		strcpy(utcBuffer, trimmed);
	}
	eprintf ("UTC time is: %s\n", utcBuffer);

	if (!strlen(utcBuffer) || !strchr(utcBuffer, ' ')) {
		eprintf ("%s(%d): WARNING! Incorrect answer: %s\n", __FUNCTION__, __LINE__, utcBuffer);
		return -1;
	}
	return 0;
}


int32_t fusion_checkDirectory(const char *path)
{
	DIR *d;

	d = opendir(path);
	if (d == NULL)
		return 0;
	closedir(d);
	return 1;
}

int fusion_getMostAppropFile (char * directory, char * pathEnd, int i)
{
	if (!directory || !pathEnd || (i < 0) || (i >= FusionObject.logoCount)) return NO;
	DIR * dir = opendir(directory);
	if (dir == NULL) {
		eprintf("%s: WARNING! %s opening failed\n", __FUNCTION__, directory);
		return NO;
	}
	struct dirent *item = readdir(dir);
	while (item) {
		// we take first file found.
		// it can lead to problems, eg.
		// logo_123size_public_tv_image.png
		// logo_321size_public_tv_image.png
		// so we can't determine which one is correct as we don't have 
		// net connection to get filesize
		char * ptr = strstr(item->d_name, pathEnd);
		if (ptr){
			sprintf (FusionObject.logos[i].filepath, "%s/%s", directory, item->d_name);
			break;
		}
		item = readdir(dir);
	}
	closedir (dir);
	return YES;
}

int fusion_checkLastModified (char * url)
{
	CURL *curl;
	CURLcode res;
	long lastModified;
	if (!url || strlen(url) == 0) 
		return FUSION_ERR_FILETIME;

	curl = curl_easy_init();
	if (!curl) return FUSION_ERR_FILETIME;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
	curl_easy_setopt(curl, CURLOPT_FILETIME, 1);
	curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0); // to make curl resolve after net connection re-established
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	res_init();

	curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLOPT_FILETIME, &lastModified);
	curl_easy_cleanup(curl);
	if (res != 0) {
		eprintf ("%s: WARNING! Couldn't get filetime for %s\n", __FUNCTION__, url);
		return FUSION_ERR_FILETIME;
	}
	eprintf ("%s: lastModified = %ld for %s...\n", __FUNCTION__, lastModified, url);

	if (FusionObject.lastModified == lastModified)
		return FUSION_NOT_MODIFIED;

	FusionObject.lastModified = lastModified;
	return FUSION_MODIFIED;
}

long fusion_getRemoteFileSize(char * url)
{
	CURL *curl;
	CURLcode res;
	double remoteFileSize = 0;
	if (!url || strlen(url) == 0) return 0;

	curl = curl_easy_init();
	if (!curl) return 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
	curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0); // to make curl resolve after net connection re-established
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	res_init();

	curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &remoteFileSize);
	if (res != 0) {
		remoteFileSize = -1;
	}
	curl_easy_cleanup(curl);

	//eprintf ("%s: RemoteFileSize = %ld for %s...\n", __FUNCTION__, (long)remoteFileSize, url);
	return (long)remoteFileSize;
}

int fusion_checkFileIsDownloaded(char * remotePath, char * localPath)
{
	long int localFileSize = 0;
	int remoteSize = 0;
	if (!remotePath || !localPath) return 0;

	// get local size
	FILE * f = fopen(localPath, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	localFileSize = ftell(f);
	fclose(f);
	if (localFileSize == 0) return 0;

	// get remote size
	remoteSize = fusion_getRemoteFileSize(remotePath);

	//eprintf ("%s: remoteFileSize = %ld, localFileSize = %ld\n", __FUNCTION__, remoteSize, localFileSize);
	if (remoteSize == localFileSize) return 1;
	return 0;
}

CURLcode fusion_getVideoByCurl (char * url, void * fsink/*char * curlStream*/, int * pStreamLen, curlWriteCB cb)
{
	CURLcode retCode = CURLE_OK;
	CURL * curl = NULL;

	if (!url || !strlen(url)) return -1;

	curl = curl_easy_init();
	if (!curl) return -1;

	*pStreamLen = 0;
	fusion_videoStreamPos = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, fusion_curlError);
	//curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlStream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fsink);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);  // 15
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)1);
	curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0); // to make curl resolve after net connection re-established
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	res_init();

	//eprintf ("%s: rq: %s\n", __FUNCTION__, url);

	retCode = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl = NULL;

	if (retCode != CURLE_OK && retCode != CURLE_WRITE_ERROR)
	{
		eprintf ("%s: ERROR! %s\n", __FUNCTION__, fusion_curlError);
		return retCode;
	}
	*pStreamLen = fusion_videoStreamPos;
	return CURLE_OK;
}

int fusion_checkSavedPlaylist()
{
	FILE * f;
	char cmd[PATH_MAX];
	char savedPath [PATH_MAX];

	sprintf (savedPath, "%s/"FUSION_TODAY_PLAYLIST_PATH, g_usbRoot);
	memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);

	f = fopen(savedPath, "rt");
	if (!f) {
		eprintf ("%s(%d): No saved playlist yet.\n", __FUNCTION__, __LINE__);
		return -1;
	}

	fread(FusionObject.savedPlaylist, FUSION_STREAM_SIZE, 1, f);
	fclose(f);

	if (strlen(FusionObject.savedPlaylist) == 0) {
		eprintf ("%s(%d): WARNING! Empty saved playlist. Remove it.\n", __FUNCTION__, __LINE__);
		memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);
		sprintf (cmd, "rm %s", savedPath);
		system(cmd);
		return -1;
	}
	return 1;
}

int fusion_savePlaylistToFile(char * path, char * playlistBuffer, int size)
{
	FILE * f;
	if (!playlistBuffer) {
		eprintf ("%s(%d): \n", __FUNCTION__, __LINE__);
		return -1;
	}
	f = fopen(path, "wt");
	if (!f) {
		eprintf ("%s(%d): WARNING! Couldn't create file %s.\n", __FUNCTION__, __LINE__, path);
		return -1;
	}
	fwrite(playlistBuffer, size, 1, f);
	fclose(f);
	return 0;
}

int fusion_parsePlaylistMode (cJSON * root)
{
	int oldMode;
	if (!root) return -1;

	oldMode = FusionObject.mode;
	cJSON* jsonMode = cJSON_GetObjectItem(root, "mode");
	if (jsonMode)
	{
		if (jsonMode->valuestring){
			if (strncmp("ondemand", jsonMode->valuestring, 8) == 0){
				FusionObject.mode = FUSION_MODE_FILES;
				snprintf(FusionObject.streamUrl, PATH_MAX, FUSION_STUB);
				//eprintf ("%s(%d): On-demand stub = %s.\n", __FUNCTION__, __LINE__, FusionObject.streamUrl);
			}
			else if (strncmp("stream", jsonMode->valuestring, 6) == 0){
				FusionObject.mode = FUSION_MODE_HLS;

				cJSON * jsonStreamUrl =  cJSON_GetObjectItem(root, "stream");
				if (jsonStreamUrl && jsonStreamUrl->valuestring){
					snprintf(FusionObject.streamUrl, PATH_MAX, jsonStreamUrl->valuestring);
				}
			}
			else if (strncmp("tv", jsonMode->valuestring, 2) == 0){
				//eprintf ("%s(%d): Mode satellite.\n", __FUNCTION__, __LINE__);
				FusionObject.mode = FUSION_MODE_TV;
			}
		}
	}
	if (oldMode != FusionObject.mode)
	{
		// stop old video
		if (appControlInfo.mediaInfo.active){
			eprintf ("%s(%d): WARNING! Mode changed. Stop current playback.\n", __FUNCTION__, __LINE__);
			media_stopPlayback();
			appControlInfo.mediaInfo.filename[0] = 0;
		}
		// start new video
		switch (FusionObject.mode){
			case FUSION_MODE_FILES:
			case FUSION_MODE_HLS:				// test to see how fusion ondemand stops
				snprintf (appControlInfo.mediaInfo.filename, PATH_MAX, "%s", FusionObject.streamUrl);
				appControlInfo.playbackInfo.playingType = media_getMediaType(appControlInfo.mediaInfo.filename);
				appControlInfo.mediaInfo.bHttp = 1;

				eprintf ("%s(%d): Start playing in new mode %s.\n", __FUNCTION__, __LINE__, (FusionObject.mode == FUSION_MODE_FILES)?"ondemand":"hls");
				int result = media_startPlayback();
				if (result == 0){
					eprintf ("%s(%d): Started %s\n", __FUNCTION__, __LINE__, FusionObject.streamUrl);
				}
				else {
					eprintf ("%s(%d): ERROR! media_startPlayback rets %d\n", __FUNCTION__, __LINE__, result);
				}
			break;
			default:
				eprintf ("%s(%d): WARNING! Mode %d unsupported.\n", __FUNCTION__, __LINE__, FusionObject.mode);
			break;
		}
	}
	else {
		// do nothing
	}
	return 0;
}

int fusion_parsePlaylistReboot(cJSON * root)
{
	if (!root) return -1;

	cJSON * jsonFirmware = cJSON_GetObjectItem(root, "firmware");
	cJSON * jsonReboot = cJSON_GetObjectItem(root, "reboot_time");
	if (jsonFirmware && jsonReboot){
		if (strcmp(FusionObject.firmware, jsonFirmware->valuestring)){
			char cmd [PATH_MAX];
			snprintf (FusionObject.firmware, PATH_MAX, jsonFirmware->valuestring);

			if (strcmp(FusionObject.reboottime, jsonReboot->valuestring)){
				snprintf (FusionObject.reboottime, PATH_MAX, jsonReboot->valuestring);
				eprintf ("%s(%d): reboottime = %s\n", __FUNCTION__, __LINE__, FusionObject.reboottime);
			}

			// get datetime of remote firmware
			memset(FusionObject.remoteFirmwareVer, '\0', FUSION_FIRMWARE_VER_LEN);
			char * ptrDev = strstr(FusionObject.firmware, "dev");
			if (ptrDev){
				char * ptrFirstDot = strchr(ptrDev, '.');
				if (ptrFirstDot){
					ptrFirstDot ++;
					char * ptrLastDot = strchr(ptrFirstDot, '.');
					if (ptrLastDot){
						snprintf (FusionObject.remoteFirmwareVer, min(32, (int)(ptrLastDot - ptrFirstDot + 1)), ptrFirstDot);
						eprintf ("%s(%d): remoteTimestamp = %s\n", __FUNCTION__, __LINE__, FusionObject.remoteFirmwareVer);
					}
				}
			}
			if (fusion_isRemoteFirmwareBetter(FusionObject.localFirmwareVer, FusionObject.remoteFirmwareVer) == YES)
			{
				eprintf ("%s(%d): Remote firmware is newer than installed one.\n", __FUNCTION__, __LINE__);
				eprintf ("%s(%d): Firmware path is %s\n", __FUNCTION__, __LINE__, FusionObject.firmware);
				eprintf ("%s(%d): Reboottime is %s\n", __FUNCTION__, __LINE__, FusionObject.reboottime);
				system("hwconfigManager s 0 UPFOUND 1");
				//system("hwconfigManager s 0 UPNOUSB 1");	// switch off usb check
				system("hwconfigManager f 0 UPNOUSB");	// remove no-checking usb on reboot
				system("hwconfigManager s 0 UPNET 1");	// check remote firmware every reboot
				sprintf (cmd, "hwconfigManager l 0 UPURL '%s'", FusionObject.firmware);
				system (cmd);

				// check if we have wifi connection
				/*char wifiStatus[8];
				fusion_getCommandOutput ("edcfg /var/etc/ifcfg-wlan0 get WAN_MODE", wifiStatus);
				if (strncmp(wifiStatus, "1", 1) == 0){
					// wifi is on and is used as external interface
					eprintf ("%s(%d): Wifi detected.\n", __FUNCTION__, __LINE__);
					pthread_t handle;
					pthread_create(&handle, NULL, fusion_threadDownloadFirmware, (void*)FusionObject.firmware);
					pthread_detach(handle);
				}*/
				// download firmware anyway
				pthread_t handle;
				pthread_create(&handle, NULL, fusion_threadDownloadFirmware, (void*)FusionObject.firmware);
				pthread_detach(handle);
			}
		}
	}else {
		FusionObject.firmware[0] = '\0';
		FusionObject.reboottime[0] = '\0';
		system("hwconfigManager f 0 UPURL 1");	// remove update url
	}
	return 0;
}

int fusion_parsePlaylistLogo(cJSON * root)
{
	int i;
	if (!root) return -1;

	cJSON * jsonLogo = cJSON_GetObjectItem(root, "logo");
	if (jsonLogo){
		char logoDir[PATH_MAX];
		char fileNameEnding[PATH_MAX];
		if (fusion_checkDirectory(g_usbRoot) == NO){
			sprintf (logoDir, "/tmp/logos");
		}
		else {
			sprintf (logoDir, "%s/logos", g_usbRoot);
		}
		if (fusion_checkDirectory(logoDir) == NO){
			char command[PATH_MAX];
			sprintf (command, "mkdir %s", logoDir);
			system (command);
		}
		pthread_mutex_lock(&FusionObject.mutexLogo);

		FusionObject.logoCount = cJSON_GetArraySize(jsonLogo);
		if (FusionObject.logoCount > FUSION_MAX_LOGOS){
			FusionObject.logoCount = FUSION_MAX_LOGOS;
		}
		for (i=0; i<FusionObject.logoCount; i++){
			cJSON * jsonItem = cJSON_GetArrayItem(jsonLogo, i);
			if (!jsonItem || !jsonItem->string || !jsonItem->valuestring) continue;

			if (!strcasecmp(jsonItem->string, FUSION_TOP_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_TOP_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_TOP_RIGHT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_LEFT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_LEFT;
			}
			else if (!strcasecmp(jsonItem->string, FUSION_BOTTOM_RIGHT_STR)){
				FusionObject.logos[i].position = FUSION_BOTTOM_RIGHT;
			}
			else continue;

			sprintf (FusionObject.logos[i].url, "%s", jsonItem->valuestring);
			FusionObject.logos[i].filepath[0] = '\0';

			// make filepath regardless if we have filesize
			// problem is the same logo name too often on server, with different sizes
			char tmpLogoUrl[PATH_MAX];
			char * ptr, *ptrSlash;
			sprintf (tmpLogoUrl, "%s", FusionObject.logos[i].url);
			ptr = strstr(tmpLogoUrl, "http:");
			if (ptr != NULL) {
				ptr += 7;
				while ((ptrSlash = strchr(ptr, '/')) != NULL) ptrSlash[0] = '_';
				sprintf (fileNameEnding, "size_%s", ptr);
			}

			long logoFileSize = fusion_getRemoteFileSize(FusionObject.logos[i].url);

			if (logoFileSize <= 0){
				eprintf ("%s(%d): WARNING! Failed to get remote logo size. Search saved one.\n", __FUNCTION__, __LINE__);

				if (fusion_getMostAppropFile (logoDir, fileNameEnding, i) == YES){
					eprintf ("%s(%d): %d-th logo: found approp = %s\n", __FUNCTION__, __LINE__, i, FusionObject.logos[i].filepath);
				}
			}
			else { // remove and wget only if we got remote size
				sprintf (FusionObject.logos[i].filepath, "%s/logo_%ld%s", logoDir, logoFileSize, fileNameEnding);
				//eprintf ("%s(%d): logo[%d] = %s, position = %d, path = %s\n", __FUNCTION__, __LINE__, 
				//	i, FusionObject.logos[i].url, FusionObject.logos[i].position, FusionObject.logos[i].filepath);

				// get local file size and compare
				// dont donwload if we have logo on flash
				long int localFileSize = 0;
				FILE * f = fopen(FusionObject.logos[i].filepath, "rb");
				if (f) {
					fseek(f, 0, SEEK_END);
					localFileSize = ftell(f);
					fclose(f);
				}
				if (localFileSize != logoFileSize){
					char cmd[128];
					//sprintf (cmd, "wget \"%s\" -O %s 2>/dev/null", url, logoPath);
					sprintf (cmd, "rm -f \"%s\"", FusionObject.logos[i].filepath);
					system(cmd);
					sprintf (cmd, "wget \"%s\" -O %s", FusionObject.logos[i].url, FusionObject.logos[i].filepath);
					system(cmd);
				}
			}
		}
		pthread_mutex_unlock(&FusionObject.mutexLogo);
		interface_displayMenu(1);
	}
	return 0;
}

int fusion_createCreeplineSurface()
{
	if (FusionObject.creepWidth == 0) return -1;

	int surfaceHeight = FUSION_SURF_HEIGHT;
	int surfaceWidth = FusionObject.creepWidth + interfaceInfo.screenWidth;

	pthread_mutex_lock(&FusionObject.mutexDtmf);
	if (FusionObject.preallocSurface){
		free (FusionObject.preallocSurface);
		FusionObject.preallocSurface = NULL;
	}
	FusionObject.preallocSurface = malloc (surfaceWidth * surfaceHeight * 4);
	if (FusionObject.preallocSurface == NULL)
	{
		pthread_mutex_unlock(&FusionObject.mutexDtmf);
		eprintf("%s(%d): ERROR malloc %d bytes\n", __FUNCTION__, __LINE__, FusionObject.creepWidth * surfaceHeight * 4);
		return FUSION_FAIL;
	}

	DFBSurfaceDescription fusion_desc;
	fusion_desc.flags = DSDESC_PREALLOCATED | DSDESC_PIXELFORMAT | DSDESC_WIDTH | DSDESC_HEIGHT;
	//fusion_desc.caps = DSCAPS_SYSTEMONLY | DSCAPS_STATIC_ALLOC | DSCAPS_DOUBLE | DSCAPS_FLIPPING;
	fusion_desc.caps = DSCAPS_NONE;
	fusion_desc.pixelformat = DSPF_ARGB;
	fusion_desc.width = surfaceWidth;
	fusion_desc.height = surfaceHeight;

	fusion_desc.preallocated[0].data = FusionObject.preallocSurface;
	fusion_desc.preallocated[0].pitch = fusion_desc.width * 4;
	fusion_desc.preallocated[1].data = NULL;
	fusion_desc.preallocated[1].pitch = 0;

	if (fusion_surface){
		fusion_surface->Release(fusion_surface);
		fusion_surface = NULL;
	}

	DFBCHECK (pgfx_dfb->CreateSurface (pgfx_dfb, &fusion_desc, &fusion_surface));
	fusion_surface->GetSize (fusion_surface, &fusion_desc.width, &fusion_desc.height);

	int x = 0;
	int y = FUSION_FONT_HEIGHT;
	gfx_drawText(fusion_surface, fusion_font, 255, 255, 255, 255, x, y, FusionObject.creepline, 0, 1);
	// clear fusion_surface after creep tail
	gfx_drawRectangle(fusion_surface, 0x0, 0x0, 0x0, 0x0, x+FusionObject.creepWidth, 0, fusion_desc.width - (x + FusionObject.creepWidth), fusion_desc.height);

	pthread_mutex_unlock(&FusionObject.mutexDtmf);

	return 0;
}

int fusion_parsePlaylistCreep(cJSON * root)
{
	int i;
	int result = 0;
	if (!root) return -1;

	result = FUSION_SAME_CREEP;
	//cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep\u00adline");
	cJSON * jsonCreep = cJSON_GetObjectItem(root, "creep-line");
	if (!jsonCreep)
	{
		//eprintf("%s(%d): No creepline field on playlist.\n", __FUNCTION__, __LINE__);
		pthread_mutex_lock(&FusionObject.creep.mutex);
		if (FusionObject.creepline) {
			free (FusionObject.creepline);
			FusionObject.creepline = NULL;
		}
		pthread_mutex_unlock(&FusionObject.creep.mutex);
		return FUSION_FAIL;
	}

	// todo : manage more than 6 large creeps
	int creepCount = cJSON_GetArraySize(jsonCreep);
	int allCreepsLen = 0;
	int allCreepsWithSpaceLen = 0;
	char * allCreeps;
	for (i=0; i<creepCount; i++){
		cJSON * jsonItem = cJSON_GetArrayItem(jsonCreep, i);
		if (!jsonItem) continue;
		cJSON * jsonText = cJSON_GetObjectItem(jsonItem, "text");
		if (!jsonText) continue;
		allCreepsLen += FUSION_MAX_CREEPLEN;
	}
	allCreepsWithSpaceLen = allCreepsLen + (creepCount-1)*strlen(FUSION_CREEP_SPACES);
	allCreeps = (char*)malloc(allCreepsWithSpaceLen);
	if (!allCreeps) {
		eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes.\n", __FUNCTION__, __LINE__, allCreepsLen);
		cJSON_Delete(root);
		return FUSION_SAME_CREEP;
	}
	allCreeps[0] = '\0';
	for (i=0; i<creepCount; i++){
		cJSON * jsonItem = cJSON_GetArrayItem(jsonCreep, i);
		if (!jsonItem) continue;
		cJSON * jsonText = cJSON_GetObjectItem(jsonItem, "text");
		if (!jsonText) continue;

		// todo : now count and pause are rewritten
		// do something with it
		cJSON * jsonPause = cJSON_GetObjectItem(jsonItem, "count");
		if (!jsonPause) FusionObject.creep.pause = FUSION_DEFAULT_CREEP_PAUSE;
		else {
			FusionObject.creep.pause = atoi(jsonPause->valuestring);
		}

		if (i) strncat(allCreeps, FUSION_CREEP_SPACES, strlen(FUSION_CREEP_SPACES));
		strncat(allCreeps, jsonText->valuestring, FUSION_MAX_CREEPLEN); // truncate
	}

	for (int k=0; k<allCreepsWithSpaceLen; k++){
		if (allCreeps[k] == '\n' || allCreeps[k] == '\r') allCreeps[k] = ' ';
	}
	int creepLen = strlen(allCreeps) + 32;

	if (!FusionObject.creepline)
	{
		pthread_mutex_lock(&FusionObject.creep.mutex);
		FusionObject.creepline = (char*)malloc(creepLen);

		if (!FusionObject.creepline) {
			eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes\n", __FUNCTION__, __LINE__, creepLen);
			cJSON_Delete(root);
			pthread_mutex_unlock(&FusionObject.creep.mutex);
			return FUSION_SAME_CREEP;
		}
		FusionObject.creepline[0] = '\0';
		snprintf (FusionObject.creepline, creepLen, "%s", allCreeps);
		fusion_font->GetStringWidth(fusion_font, FusionObject.creepline, -1, &FusionObject.creepWidth);

		fusion_createCreeplineSurface();
		result = FUSION_NEW_CREEP;
		pthread_mutex_unlock(&FusionObject.creep.mutex);
		free (allCreeps);

		eprintf ("%s(%d): Creepline (initial) = %s, pause = %d\n", __FUNCTION__, __LINE__, 
			FusionObject.creepline, FusionObject.creep.pause);
	}
	else if (strcmp(FusionObject.creepline, allCreeps))
	{
		pthread_mutex_lock(&FusionObject.creep.mutex);
		if (FusionObject.creepline) 
		{
			if (FusionObject.creep.isShown == 0) {
				eprintf ("%s(%d): Got new creep. Wait till current creep ends...\n", __FUNCTION__, __LINE__);
			}
			while (FusionObject.creep.isShown == 0){
				fusion_wait(500);
			}
			eprintf ("%s(%d): End wait.\n", __FUNCTION__, __LINE__);

			free (FusionObject.creepline);
			FusionObject.creepline = NULL;
		}
		FusionObject.creepline = (char*)malloc(creepLen);
		if (!FusionObject.creepline) {
			eprintf ("%s(%d): ERROR! Couldn't malloc %d bytes\n", __FUNCTION__, __LINE__, creepLen);
			cJSON_Delete(root);
			pthread_mutex_unlock(&FusionObject.creep.mutex);
			return FUSION_SAME_CREEP;
		}
		snprintf (FusionObject.creepline, creepLen, "%s", allCreeps);
		fusion_font->GetStringWidth(fusion_font, FusionObject.creepline, -1, &FusionObject.creepWidth);

		fusion_createCreeplineSurface();
		result = FUSION_NEW_CREEP;
		pthread_mutex_unlock(&FusionObject.creep.mutex);
		free (allCreeps);

		eprintf ("%s(%d): Creepline (updated) = %s, pause = %d\n", __FUNCTION__, __LINE__, FusionObject.creepline, FusionObject.creep.pause);
	}
	else if (strcmp(FusionObject.creepline, allCreeps) == 0)
	{
		free (allCreeps);
		return FUSION_SAME_CREEP;
	}

	return result;
}

int fusion_touchLockFile()
{
	system ("touch "FUSION_LOCK_FILE);
	return 0;
}

int fusion_removeAdLockFile()
{
	system ("rm -f "FUSION_LOCK_FILE);
	return 0;
}

int fusion_parsePlaylistMarks(cJSON * root)
{
	int i;
	if (!root) return -1;

	cJSON * jsonMarks = cJSON_GetObjectItem(root, "mark");
	if (jsonMarks){

		if (strlen(g_adsPath) == 0){
			if (helper_checkAdsOnUsb() == NO){
				eprintf ("%s(%d): No ads folder.\n", __FUNCTION__, __LINE__);
				return -1;
			}
		}

		int markCount = cJSON_GetArraySize(jsonMarks);
		if (markCount) {
			fusion_touchLockFile();
		}

		for (i=0; i<markCount; i++)
		{
			char filepath[PATH_MAX];
			int dtmfIndex = -1;

			cJSON * jsonItem = cJSON_GetArrayItem(jsonMarks, i);
			if (!jsonItem) continue;

			cJSON * jsonLink = cJSON_GetObjectItem(jsonItem, "link");
			cJSON * jsonDuration = cJSON_GetObjectItem(jsonItem, "duration");

			if (!jsonItem->string || 
			    !jsonLink || !jsonLink->valuestring || 
			    !jsonDuration || !jsonDuration->valuestring)
			{
				eprintf ("%s(%d): WARNING! Incorrect mark format (%s). Skip.\n", __FUNCTION__, __LINE__, jsonItem->string);
				continue;
			}
			dtmfIndex = atoi(jsonItem->string);
			if (dtmfIndex < 0 || dtmfIndex >= FUSION_MAX_MARKS){
				eprintf ("%s(%d): WARNING! Incorrect mark index (%d). Skip.\n", __FUNCTION__, __LINE__, dtmfIndex);
				continue;
			}
			// dtmfIndex is ok

			int duration = atoi(jsonDuration->valuestring);
			if (duration < 0){
				eprintf ("%s(%d): WARNING! Incorrect mark duration (%s). Skip.\n", __FUNCTION__, __LINE__, jsonDuration->valuestring);
				continue;
			}
			int remoteSize = fusion_getRemoteFileSize(jsonLink->valuestring);
			if (remoteSize == 0){
				eprintf ("%s(%d): WARNING! Couldn't get remoteFileSize for %s. Skip\n", __FUNCTION__, __LINE__, jsonLink->valuestring);
				continue;
			}

			if (fusion_makeAdsPathFromUrl(jsonLink->valuestring, filepath, duration, remoteSize) == 0){
				fusion_saveFileByWget(jsonLink->valuestring, filepath, dtmfIndex);
				fusion_waitAdIsDownloaded(filepath, dtmfIndex);
			}
			FusionObject.marks[dtmfIndex].duration = duration;
			snprintf (FusionObject.marks[dtmfIndex].link, PATH_MAX, "%s", jsonLink->valuestring);
			snprintf (FusionObject.marks[dtmfIndex].filename, PATH_MAX, "%s", filepath);
		}

		fusion_removeAdLockFile();
		// todo : remove old ad videos in ads folder
	}
	return 0;
}

int fusion_getAndParsePlaylist ()
{
	cJSON * root;
	char request[FUSION_URL_LEN];
	time_t now;
	struct tm nowDate;
	int result = 0;

	time (&now);
	nowDate = *localtime (&now);
	if (nowDate.tm_year + 1900 == 2000){ // date was not set earlier
		if (fusion_setMoscowDateTime() == 0){
			time (&now);
			nowDate = *localtime (&now);
		}
	}

	if (strlen(FusionObject.demoUrl)){
		sprintf (request, "%s", FusionObject.demoUrl);
	}
	else {
		sprintf (request, "%s/?s=%s&c=playlist_full&date=%04d-%02d-%02d", FusionObject.server, FusionObject.secret, 
		         nowDate.tm_year + 1900, nowDate.tm_mon+1, nowDate.tm_mday);
	}
/*
	result = fusion_checkLastModified(request);
	if (result == FUSION_NOT_MODIFIED){
		eprintf ("%s(%d): Playlist not modified.\n",   __FILE__, __LINE__);
		return FUSION_SAME_CREEP;
	}
	else if (result == FUSION_ERR_FILETIME){
		eprintf ("%s(%d): WARNING! Problem getting playlist modification time.\n",   __FILE__, __LINE__);
		//return FUSION_SAME_CREEP;
	}
	else eprintf ("%s(%d): Playlist modified.\n",   __FILE__, __LINE__);
	*/

	if (fusion_downloadPlaylist(request, &root) != 0) {
		eprintf ("%s(%d): WARNING! download playlist failed. Search for saved one.\n",   __FILE__, __LINE__);
		// use saved playlist
		if (strlen(g_usbRoot) == 0){
			if (fusion_getUsbRoot() == 0){
				eprintf ("%s: ERROR! No flash found.\n", __FUNCTION__);
				return FUSION_FAIL;
			}
		}

		if (fusion_checkSavedPlaylist() == 1){
			root = cJSON_Parse(FusionObject.savedPlaylist);
			if (!root) {
				char cmd[PATH_MAX];
				char savedPath [PATH_MAX];
				eprintf ("%s: WARNING! Incorrect format in saved playlist. Remove it.\n",   __FUNCTION__);
				memset(FusionObject.savedPlaylist, 0, FUSION_STREAM_SIZE);

				sprintf (savedPath, "%s/"FUSION_TODAY_PLAYLIST_PATH, g_usbRoot);
				sprintf (cmd, "rm %s", savedPath);
				system(cmd);
				return FUSION_FAIL;
			}
			eprintf ("%s(%d): Got valid saved playlist.\n",   __FUNCTION__, __LINE__);
		}
		else {
			eprintf ("%s(%d): WARNING! No saved playlist.\n",   __FUNCTION__, __LINE__);
			// todo : no connection, no saved one, what to do ?
			return FUSION_FAIL;
		}
	}

	fusion_parsePlaylistMode(root);
	fusion_parsePlaylistReboot(root);
	fusion_parsePlaylistLogo(root);
	fusion_parsePlaylistMarks(root);
	result = fusion_parsePlaylistCreep(root);

	cJSON_Delete(root);
	return result;
}

void fusion_cleanup()
{
	if (FusionObject.threadGetCreepHandle){
		pthread_cancel(FusionObject.threadGetCreepHandle);
		pthread_join(FusionObject.threadGetCreepHandle, NULL);
	}
	if (FusionObject.threadMonCreepHandle){
		pthread_cancel(FusionObject.threadMonCreepHandle);
		pthread_join(FusionObject.threadMonCreepHandle, NULL);
	}
	if (FusionObject.threadCheckReboot){
		pthread_cancel(FusionObject.threadCheckReboot);
		pthread_join(FusionObject.threadCheckReboot, NULL);
	}
	if (FusionObject.threadFlipCreep){
		pthread_cancel(FusionObject.threadFlipCreep);
		pthread_join(FusionObject.threadFlipCreep, NULL);
	}
	if (FusionObject.creepline) {
		free (FusionObject.creepline);
		FusionObject.creepline = NULL;
	}

	if (FusionObject.playlistBuffer){
		free (FusionObject.playlistBuffer);
		FusionObject.playlistBuffer = NULL;
	}

	if (fusion_surface){
		fusion_surface->Release(fusion_surface);
		fusion_surface = NULL;
	}

	pthread_mutex_lock(&FusionObject.mutexDtmf);
	if (FusionObject.preallocSurface){
		free(FusionObject.preallocSurface);
		FusionObject.preallocSurface = 0;
	}
	pthread_mutex_unlock(&FusionObject.mutexDtmf);

	pthread_mutex_destroy(&FusionObject.creep.mutex);
	pthread_mutex_destroy(&FusionObject.mutexLogo);
	pthread_mutex_destroy(&FusionObject.mutexDtmf);

#ifdef FUSION_ENCRYPTED
	system ("/opt/elecard/bin/umount_encrypted.sh");
#endif
}

static char helper_checkAdsOnUsb ()
{
	if (strlen(g_usbRoot) == 0){
		if (fusion_getUsbRoot() == NO) return NO;
	}

	if (strlen(g_adsPath) == 0){
		sprintf (g_adsPath, "%s/ads", g_usbRoot);
	}
	if (fusion_checkDirectory(g_adsPath) == NO){
		char command[PATH_MAX];
		sprintf (command, "mkdir %s", g_adsPath);
		system(command);
	}

	eprintf("%s: Ads: path = %s\n", __FUNCTION__, g_adsPath);
	return YES;
}

void fusion_echoFilepath(char * symlinkPath, char * filepath)
{
	char command[PATH_MAX];
	sprintf (command, "echo %s > %s", filepath, symlinkPath);
	system (command);
	return;
}

int fusion_makeAdsSymlink (char * filepath, int dtmf)
{
	char symlinkPath[PATH_MAX];
	if (!filepath || dtmf < 0) return -1;

	snprintf (symlinkPath, PATH_MAX, "%s/dtmf_%d_.txt", g_adsPath, dtmf);

	if (helper_fileExists(symlinkPath)){
		if (strcmp(FusionObject.marks[dtmf].filename, filepath) == 0){
			// filename for this dtmf did not change
			return 0;
		}
		else { // new filepath for this dtmf
			fusion_echoFilepath(symlinkPath, filepath);
		}
	}
	else { // symlink was not created yet
		fusion_echoFilepath(symlinkPath, filepath);
	}
	return 0;
}

int fusion_makeAdsPathFromUrl (char * fileUrl, char * resultPath, int duration, int size)
{
	char *ptr, *ptrSlash, *ptrDot;
	char url[PATH_MAX];
	char ext[16];
	if (!fileUrl || !resultPath) return -1;

	sprintf (url, fileUrl);

	ptrDot = strrchr(url, '.');
	if (ptrDot){
		snprintf (ext, 16, "%s", ptrDot + 1);
		*ptrDot = '\0';
	}

	ptr = strstr(url, "http:");
	if (ptr == NULL) {
		eprintf ("%s: ERROR! incorrect url %s\n", __FUNCTION__, url);
		return -1;
	}
	ptr += 7;

	// replace / with _
	while ((ptrSlash = strchr(ptr, '/')) != NULL){
		ptrSlash[0] = '_';
	}

	snprintf (resultPath, PATH_MAX, "%s/%s_sec%d_size%d_.%s", g_adsPath, ptr, duration, size, ext);
	return 0;
}

int fusion_saveFileByWget (char * url, char * filepath, int dtmf)
{
	char cmd[PATH_MAX];

	if (fusion_checkAdIsComplete(filepath) == NO)
	{
		//sprintf (cmd, "wget --limit-rate=%dk -c -q %s -O %s", g_shaping, url, filepath); // quiet mode
		//sprintf (cmd, "/usr/sbin/wget1.12 --limit-rate=%dk -c -q %s -O %s", g_shaping, url, filepath); // quiet mode
		sprintf (cmd, "wget -c -q %s -O %s", url, filepath); // quiet mode
		eprintf ("%s(%d): %s ...\n",   __FUNCTION__, __LINE__, cmd);
		system(cmd);
	}

	return 0;
}

static int helper_cutStringPiece (char * haystack, char * needle, char * piece, int maxPieceLen)
{
	if (!haystack || !needle || !piece) return -1;

	char * ptrStartPiece = strstr(haystack, needle);
	if (ptrStartPiece == NULL){
		eprintf ("%s(%d): No %s found in %s.\n", __FUNCTION__, __LINE__, needle, haystack);
		return -1;
	}

	ptrStartPiece += strlen (needle);

	char * ptrUnderline = strchr(ptrStartPiece, '_');
	if (ptrUnderline == NULL){
		eprintf ("%s(%d): Incorrect format (%s).\n", __FUNCTION__, __LINE__, haystack);
		return -1;
	}
	int pieceLen = (int)(ptrUnderline - ptrStartPiece + 1);
	if (pieceLen > maxPieceLen){
		eprintf ("%s(%d): Incorrect format (%s).\n", __FUNCTION__, __LINE__, haystack);
		return -1;
	}
	snprintf (piece, pieceLen, "%s", ptrStartPiece);
	return 0;
}

int fusion_checkAdIsComplete(char * filepath)
{
	long int localSize = 0;
	int declaredSize = 0;
	char strSize[32];

	if (!filepath) return NO;

	FILE * f = fopen(filepath, "rb");
	if (!f) return NO;
	fseek(f, 0, SEEK_END);
	localSize = ftell(f);
	fclose(f);

	if (localSize == 1024){
		eprintf ("%s(%d): WARNING! %s is too small (%ld bytes)\n", __FUNCTION__, __LINE__, filepath, localSize);
		return NO;
	}

	if (helper_cutStringPiece(filepath, "_size", strSize, 32) != 0){
		eprintf ("%s(%d): WARNING! Couldnot get size form path %s\n", __FUNCTION__, __LINE__, filepath);
		return NO;
	}
	declaredSize = atoi(strSize);
	if (declaredSize <= 0) {
		eprintf ("%s(%d): WARNING! Couldnot get size form path %s\n", __FUNCTION__, __LINE__, filepath);
		return NO;
	}

	if (declaredSize == localSize) return YES;
	return NO;
}

int fusion_waitAdIsDownloaded (char * filepath, int dtmf)
{
	if (!filepath || dtmf < 0) return -1;

	while (1){ // todo : aux break condition?
		if (fusion_checkAdIsComplete(filepath) == YES){
			//eprintf ("%s(%d): %s downloaded OK.\n", __FUNCTION__, __LINE__, filepath);
			fusion_makeAdsSymlink(filepath, dtmf);
			break;
		}
		fusion_wait(1000 * 3);
	}
	return 0;
}
#endif // ENABLE_FUSION
