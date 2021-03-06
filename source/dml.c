#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/video_types.h>
#include <dirent.h>
#include <fcntl.h>
#include "globals.h"
#include "fsop/fsop.h"
#include "devices.h"
#include "mystring.h"
#include "stub.h"

#define DMLVER "DMLSDAT0001"
#define SEP 0xFF

#define BC 0x0000000100000100ULL
#define MIOS 0x0000000100000101ULL

/** Base address for video registers. */
#define MEM_VIDEO_BASE (0xCC002000)
#define IOCTL_DI_DVDLowAudioBufferConfig 0xE4

#define PLGC_Auto 0
#define PLGC_Game 1
#define PLGC_WII 2
#define PLGC_NTSC 3
#define PLGC_PAL50 4
#define PLGC_PAL60 5

#define VIDEO_MODE_NTSC 0
#define VIDEO_MODE_PAL 1
#define VIDEO_MODE_PAL60 2
#define VIDEO_MODE_NTSC480P 3
#define VIDEO_MODE_PAL480P 4

#define SRAM_ENGLISH 0
#define SRAM_GERMAN 1
#define SRAM_FRENCH 2
#define SRAM_SPANISH 3
#define SRAM_ITALIAN 4
#define SRAM_DUTCH 5

typedef struct DML_CFG
	{
	u32 Magicbytes;                 //0xD1050CF6
	u32 CfgVersion;                 //0x00000001
	u32 VideoMode;
	u32 Config;
	char GamePath[255];
	char CheatPath[255];
	} 
DML_CFG;

enum dmlconfig
	{
	DML_CFG_CHEATS          = (1<<0),
	DML_CFG_DEBUGGER        = (1<<1),
	DML_CFG_DEBUGWAIT       = (1<<2),
	DML_CFG_NMM             = (1<<3),
	DML_CFG_NMM_DEBUG       = (1<<4),
	DML_CFG_GAME_PATH       = (1<<5),
	DML_CFG_CHEAT_PATH      = (1<<6),
	DML_CFG_ACTIVITY_LED	= (1<<7),
	DML_CFG_PADHOOK         = (1<<8),
	DML_CFG_NODISC          = (1<<9), //changed from NODISC to FORCE_WIDE in 2.1+, but will remain named NODISC until stfour drops support for old DM(L)
	DML_CFG_BOOT_DISC       = (1<<10),
    DML_CFG_BOOT_DISC2      = (1<<11), //changed from BOOT_DOL to BOOT_DISC2 in 2.1+
    DML_CFG_NODISC2         = (1<<12), //added in DM 2.2+
	};

enum dmlvideomode
	{
	DML_VID_DML_AUTO        = (0<<16),
	DML_VID_FORCE           = (1<<16),
	DML_VID_NONE            = (2<<16),

	DML_VID_FORCE_PAL50     = (1<<0),
	DML_VID_FORCE_PAL60     = (1<<1),
	DML_VID_FORCE_NTSC      = (1<<2),
	DML_VID_FORCE_PROG      = (1<<3),
	DML_VID_PROG_PATCH      = (1<<4),
	};

static char *dmlFolders[] = {"ngc", "games"};

syssram* __SYS_LockSram();
u32 __SYS_UnlockSram(u32 write);
u32 __SYS_SyncSram(void);

static void SetGCVideoMode (void)
	{
	syssram *sram;
	sram = __SYS_LockSram();

	if(VIDEO_HaveComponentCable())
			sram->flags |= 0x80; //set progressive flag
	else
			sram->flags &= 0x7F; //clear progressive flag

	if (config.dmlvideomode == DMLVIDEOMODE_NTSC)
	{
			rmode = &TVNtsc480IntDf;
			sram->flags &= 0xFE; // Clear bit 0 to set the video mode to NTSC
			sram->ntd &= 0xBF; //clear pal60 flag
	}
	else
	{
			rmode = &TVPal528IntDf;
			sram->flags |= 0x01; // Set bit 0 to set the video mode to PAL
			sram->ntd |= 0x40; //set pal60 flag
	}

	__SYS_UnlockSram(1); // 1 -> write changes
	
	while(!__SYS_SyncSram());
	
	// TVPal528IntDf
	
	u32 *sfb;
	static GXRModeObj *rmode;
	
	//config.dmlvideomode = DMLVIDEOMODE_PAL;
	
	if (config.dmlvideomode == DMLVIDEOMODE_PAL)
		{
		rmode = &TVPal528IntDf;
		*(u32*)0x800000CC = VI_PAL;
		}
	else
		{
		rmode = &TVNtsc480IntDf;
		*(u32*)0x800000CC = VI_NTSC;
		}

	VIDEO_SetBlack(TRUE);
	VIDEO_Configure(rmode);
	sfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_ClearFrameBuffer(rmode, sfb, COLOR_BLACK);
	VIDEO_SetNextFramebuffer(sfb);

	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

	VIDEO_SetBlack(FALSE);
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
	}

s32 StartMIOS (void)
	{
	s32 ret;
	
	SetGCVideoMode ();
	
	tikview view ATTRIBUTE_ALIGN(32);
	
	ret = ES_GetTicketViews(BC, &view, 1);
	if (ret != 0) return -1;

	// Tell DML to boot the game from sd card
	*(u32 *)0x80001800 = 0xB002D105;
	DCFlushRange((void *)(0x80001800), 4);
	ICInvalidateRange((void *)(0x80001800), 4);			
	
	*(volatile unsigned int *)0xCC003024 |= 7;
	
	ret = ES_LaunchTitle(BC, &view);
	
	return -102;
	}
	
	
#define MAXGAMES 30
#define MAXROW 10

static bool GetName (char *path, char *id, char *name)
	{
	FILE *f;
	f = fopen(path, "rb");
	if (!f)	
		{
		*name = '\0';
		return false;
		}
	
	fread(id, 1, 8, f);
	//id[6] = 0;
	
	fseek( f, 0x20, SEEK_SET);
	fread(name, 1, 32, f);
	fclose(f);

	id[6]++;
	id[7]++;
	id[8] = 0;

	name[31] = 0;
	return true;
	}

int DMLRun (char *folder, char *id, u32 videomode)
	{
	char path[128];
	
	Debug ("DMLRun (%s, %s, %u)", folder, id, videomode);

	if (!devices_Get(DEV_SD)) return 0;
	
	if (videomode == PLGC_Auto)
		videomode = PLGC_Game;

	if (videomode == PLGC_PAL60)
		videomode = PLGC_PAL50;
	
	if (videomode == PLGC_Game) // GAME
		{
		if (id[3] == 'E' || id[3] == 'J' || id[3] == 'N')
			config.dmlvideomode = DMLVIDEOMODE_NTSC;
		else
			config.dmlvideomode = DMLVIDEOMODE_PAL;
		}
	if (videomode == PLGC_WII) // WII
		{
		if (CONF_GetRegion() == CONF_REGION_EU)
			config.dmlvideomode = DMLVIDEOMODE_PAL;
		else
			config.dmlvideomode = DMLVIDEOMODE_NTSC;
		}
	
	if (videomode == PLGC_NTSC)
		config.dmlvideomode = DMLVIDEOMODE_NTSC;

	if (videomode == PLGC_PAL50)
		config.dmlvideomode = DMLVIDEOMODE_PAL;

	sprintf (path, "%s://games/boot.bin", devices_Get(DEV_SD));
	
	FILE *f;
	f = fopen(path, "wb");
	if (!f)	return -1;
	fwrite(folder, 1, strlen(folder), f);
	fclose(f);
	
 	memcpy ((char *)0x80000000, id, 6);
	
	Shutdown ();
	
	StartMIOS ();
	return 1;
	}

int DMLRunNew (char *folder, char *id, s_gameConfig *gameconf, u32 slot)
	{
	DML_CFG cfg;
	char path[256];
	
	memset (&cfg, 0, sizeof (DML_CFG));
	
	Debug ("DMLRunNew (%s, %s, %u, %u, %u, %u)", folder, id, gameconf->dmlVideoMode, gameconf->dmlNoDisc, gameconf->dmlPadHook, gameconf->dmlNMM);
	
	cfg.Config |= DML_CFG_GAME_PATH;

	if (slot)
		{
		sprintf (path, "usb:/%s/game.iso", folder);
		sprintf (cfg.CheatPath, "usb:/codes/%s.gct", id);
		}
	else
		{
		sprintf (path, "sd:/%s/game.iso", folder);
		sprintf (cfg.CheatPath, "sd:/codes/%s.gct", id);
		}
		
	if (fsop_FileExist (path))
		{
		sprintf (path, "%s/game.iso", folder);
		}
	else
		{
		sprintf (path, "%s/", folder);
		}

	Debug ("DMLRunNew -> using %s", path);

	//if (!devices_Get(DEV_SD)) return 0;
	
	Shutdown ();

	cfg.Magicbytes = 0xD1050CF6;
	if (config.dmlVersion == GCMODE_DM22 || config.dmlVersion == GCMODE_DMAUTO)
		cfg.CfgVersion = 0x00000002;
	else
		cfg.CfgVersion = 0x00000001;
		
	if (gameconf->dmlVideoMode == PLGC_Auto) // AUTO
		{
		cfg.VideoMode |= DML_VID_DML_AUTO;
		}
	if (gameconf->dmlVideoMode == PLGC_Game) // GAME
		{
		if (id[3] == 'E' || id[3] == 'J' || id[3] == 'N')
			cfg.VideoMode |= DML_VID_FORCE_NTSC;
		else
			cfg.VideoMode |= DML_VID_FORCE_PAL50;
		}
	if (gameconf->dmlVideoMode == PLGC_WII) // WII
		{
		if (CONF_GetRegion() == CONF_REGION_EU)
			cfg.VideoMode |= DML_VID_FORCE_PAL50;
		else
			cfg.VideoMode |= DML_VID_FORCE_NTSC;
		}
	
	if (gameconf->dmlVideoMode == PLGC_NTSC)
		cfg.VideoMode |= DML_VID_FORCE_NTSC;

	if (gameconf->dmlVideoMode == PLGC_PAL50)
		cfg.VideoMode |= DML_VID_FORCE_PAL50;

	if (gameconf->dmlVideoMode == PLGC_PAL60)
		cfg.VideoMode |= DML_VID_FORCE_PAL60;
		
	//kept as nodisc for legacy purposes, but it also controls
	//widescreen force 16:9 in 2.1+
	if (gameconf->dmlNoDisc)
		cfg.Config |= DML_CFG_NODISC;

	if (gameconf->dmlFullNoDisc)
		cfg.Config |= DML_CFG_NODISC2;

	if (gameconf->dmlPadHook)
		cfg.Config |= DML_CFG_PADHOOK;

	if (gameconf->dmlNMM)
		cfg.Config |= DML_CFG_NMM;
		
    if(gameconf->ocarina)
        cfg.Config |= DML_CFG_CHEATS;

	strcpy (cfg.GamePath, path);
 	memcpy ((char *)0x80000000, id, 6);
	
	//Write options into memory
	memcpy((void *)0x80001700, &cfg, sizeof(DML_CFG));
	DCFlushRange((void *)(0x80001700), sizeof(DML_CFG));

	//DML v1.2+
	memcpy((void *)0x81200000, &cfg, sizeof(DML_CFG));
	DCFlushRange((void *)(0x81200000), sizeof(DML_CFG));

	/* Boot BC */
	WII_Initialize();
	return WII_LaunchTitle(0x100000100LL);
	}
	
///////////////////////////////////////////////////////////////////////////////////////////////////////	

void DMLResetCache (void)
	{
	char cachepath[128];
	sprintf (cachepath, "%s://ploader/dml.dat", vars.defMount);
	unlink (cachepath);
	}

#define BUFFSIZE (1024*64)

/*
static void cb_DML (void)
	{
	Video_WaitPanel (TEX_HGL, "Please wait...|Searching gamecube games");
	}
*/

char *DMLScanner  (bool reset)
	{
	//static bool xcheck = true; // do that one time only
	DIR *pdir;
	struct dirent *pent;
	char cachepath[128];
	char path[128];
	char fullpath[128];
	char name[32];
	char src[32];
	char b[128];
	char id[10];
	FILE *f;
	char *buff = calloc (1, BUFFSIZE); // Yes, we are wasting space...
	
	sprintf (cachepath, "%s://ploader/dml.dat", vars.defMount);

	//reset = 1;

	if (reset == 0)
		{
		f = fopen (cachepath, "rb");
		if (!f) 
			{
			Debug ("DMLScanner: cache file '%s' not found", cachepath);
			reset = 1;
			}
		else
			{
			Debug ("DMLScanner: cache file '%s' found, checking version", cachepath);
			
			fread (b, 1, strlen(DMLVER), f);
			b[strlen(DMLVER)] = 0;
			if (strcmp (b, DMLVER) != 0)
				{
				Debug ("DMLScanner: version mismatch, forcing rebuild");
				reset = 1;
				}
			else
				fread (buff, 1, BUFFSIZE-1, f);
				
			fclose (f);
			
			buff[BUFFSIZE-1] = 0;
			}
		}
	
	if (reset == 1)
		{
		// Allow usb only for DM
		if (config.dmlVersion < GCMODE_DM22 && !devices_Get(DEV_SD)) return 0;
		
		if (config.dmlVersion != GCMODE_DM22 && devices_Get(DEV_SD))
			{
			sprintf (path, "%s://games", devices_Get(DEV_SD));
			
			Debug ("DML: scanning %s", path);
			
			pdir=opendir(path);
			
			while ((pent=readdir(pdir)) != NULL) 
				{
				if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0) continue;
				
				int found = 0;
				if (config.dmlVersion == GCMODE_DEVO)
					{
					sprintf (fullpath, "%s/%s", path, pent->d_name);
					if (ms_strstr (fullpath, ".iso") && fsop_FileExist (fullpath))
						found = 1;
					}
				
				if (!found)
					{
					sprintf (fullpath, "%s/%s/game.iso", path, pent->d_name);
					found = fsop_FileExist (fullpath);
					}
				
				if (!found && config.dmlVersion != GCMODE_DEVO)
					{
					sprintf (fullpath, "%s/%s/sys/boot.bin", path, pent->d_name);
					found = fsop_FileExist (fullpath);
					}
					
				Debug ("DML: checking %s", fullpath);
				
				if (found)
					{
					Video_WaitPanel (TEX_HGL, 0, "Please wait...|Searching gamecube games");
					
					bool skip = false;
					
					/*
					if (xcheck && devices_Get(DEV_USB))
						{
						char sdp[256], usbp[256];
						
						sprintf (sdp, "%s://games/%s", devices_Get(DEV_SD), pent->d_name);
						
						int folder;
						for (folder = 0; folder < 2; folder++)
							{
							sprintf (usbp, "%s://%s/%s", devices_Get(DEV_USB), dmlFolders[folder], pent->d_name);
							
							if (fsop_DirExist (usbp))
								{
								int sdkb, usbkb;
								
								sdkb = fsop_GetFolderKb (sdp, cb_DML);
								usbkb = fsop_GetFolderKb (usbp, cb_DML);
								
								if (abs (sdkb - usbkb) > 5) // Let 5kb difference for codes
									{
									char mex[256];
									fsop_KillFolderTree (sdp, cb_DML);
									
									sprintf (mex, "Folder '%s' removed\n as it has the wrong size", sdp);
									grlib_menu (mex, "   OK   ");
									skip = true;
									}
								}
							}
						}
					*/
					
					if (!skip)
						{
						if (!GetName (fullpath, id, name)) continue;
						
						//ms_strtoupper (pent->d_name);
						if (config.dmlVersion != GCMODE_DEVO)
							sprintf (b, "%s%c%s%c%d%c%s/%s%c", name, SEP, id, SEP, DEV_SD, SEP, path, pent->d_name, SEP);
						else
							sprintf (b, "%s%c%s%c%d%c%s%c", name, SEP, id, SEP, DEV_SD, SEP, fullpath, SEP);
						
						strcat (buff, b);
						}
					}
				}
				
			closedir(pdir);
			}
		
		//xcheck = false;

		int i;
		for (i = DEV_USB; i < DEV_MAX; i++)
			{
			if (devices_Get(i))
				{
				int folder;
				for (folder = 0; folder < 2; folder++)
					{
					sprintf (path, "%s://%s", devices_Get(i), dmlFolders[folder]);
					
					Debug ("DML: scanning %s", path);
					
					pdir=opendir(path);

					while ((pent=readdir(pdir)) != NULL) 
						{
						if (strcmp (pent->d_name, ".") == 0 || strcmp (pent->d_name, "..") == 0) continue;
						
						Video_WaitPanel (TEX_HGL, 0, "Please wait...|Searching gamecube games");
						
						ms_strtoupper (pent->d_name);
						
						int found = 0;
						if (config.dmlVersion == GCMODE_DEVO)
							{
							sprintf (fullpath, "%s/%s", path, pent->d_name);
							if (ms_strstr (fullpath, ".iso") && fsop_FileExist (fullpath))
								found = 1;
							}
						
						if (!found)
							{
							sprintf (fullpath, "%s/%s/game.iso", path, pent->d_name);
							found = fsop_FileExist (fullpath);
							}
						
						if (!found && config.dmlVersion != GCMODE_DEVO)
							{
							sprintf (fullpath, "%s/%s/sys/boot.bin", path, pent->d_name);
							found = fsop_FileExist (fullpath);
							}
							
						Debug ("DML: checking %s", fullpath);
						
						if (!found || !GetName (fullpath, id, name)) continue;
						
						Debug ("DML: valid image!");
						
						sprintf (src, "%c%s%c", SEP, id, SEP); // make sure to find the exact name
						if (strstr (buff, src) == NULL)	// Make sure to not add the game twice
							{
							if (config.dmlVersion != GCMODE_DEVO)
								sprintf (b, "%s%c%s%c%d%c%s/%s%c", name, SEP, id, SEP, DEV_USB, SEP, path, pent->d_name, SEP);
							else
								sprintf (b, "%s%c%s%c%d%c%s%c", name, SEP, id, SEP, DEV_USB, SEP, fullpath, SEP);
							
							strcat (buff, b);
							}
						}
						
					closedir(pdir);
					}
				}
			}

		Debug ("WBFSSCanner: writing cache file");
		f = fopen (cachepath, "wb");
		if (f) 
			{
			fwrite (DMLVER, 1, strlen(DMLVER), f);
			fwrite (buff, 1, strlen(buff)+1, f);
			fclose (f);
			}
		}

	int i, l;
	
	l = strlen (buff);
	for (i = 0; i < l; i++)
		if (buff[i] == SEP)
			buff[i] = 0;

	return buff;
	}
	
/*

DMLRemove will prompt to remove same games from sd to give space to new one

*/

int DMLInstall (char *gamename, size_t reqKb)
	{
	int ret;
	int i = 0, j;
	DIR *pdir;
	struct dirent *pent;
	char path[128];
	char menu[2048];
	char buff[64];
	char name[64];
	char title[256];
	
	char files[MAXGAMES][64];
	size_t sizes[MAXGAMES];

	size_t devKb = 0;
	
	Debug ("DMLInstall (%s): Started", gamename);

	if (!devices_Get(DEV_SD))
		{
		Debug ("DMLInstall (%s): ERR SD Device invalid", gamename);
		return 0;
		}
	
	sprintf (path, "%s://games", devices_Get(DEV_SD));

	devKb = fsop_GetFreeSpaceKb (path);
	
	Debug ("DMLInstall: devKb = %u, reqKb = %u", devKb, reqKb);
	
	if (devKb > reqKb) 
		{
		Debug ("DMLInstall (%s): OK there is enaught space", gamename);
		return 1; // We have the space
		}
	
	while (devKb < reqKb)
		{
		*menu = '\0';
		i = 0;
		j = 0;
		
		pdir=opendir(path);
		
		while ((pent=readdir(pdir)) != NULL) 
			{
			if (strlen (pent->d_name) ==  6)
				{
				strcpy (files[i], pent->d_name);
				
				GetName (DEV_SD, files[i], name);
				if (strlen (name) > 20)
					{
					name[12] = 0;
					strcat(name, "...");
					}
					
				sprintf (buff, "%s/%s", path, files[i]);
				sizes[i] = fsop_GetFolderKb (buff, NULL);
				grlib_menuAddItem (menu, i, "%s (%d Mb)", name, sizes[i] / 1000);
				
				i++;
				j++;
			
				if (j == MAXROW)
					{
					grlib_menuAddColumn (menu);
					j = 0;
					}
				
				if (i == MAXGAMES)
					{
					Debug ("DMLInstall (%s): Warning... too many games", gamename);

					break;
					}
				}
			}
		
		closedir(pdir);

		Debug ("DMLInstall (%s): found %d games on sd", gamename, i);
		
		sprintf (title, "You must free %u Mb to install %s\nClick on game to remove it from SD, game size is %u Mb), Press (B) to close", 
			(reqKb - devKb) / 1000, gamename, reqKb / 1000);

		ret = grlib_menu (0, title, menu);
		if (ret == -1)
			{
			Debug ("DMLInstall (%s): aborted by user", gamename);
			return 0;
			}
			
		if (ret >= 0)
			{
			char gamepath[128];
			sprintf (gamepath, "%s://games/%s", devices_Get(DEV_SD), files[ret]);
			
			Debug ("DMLInstall deleting '%s'", gamepath);
			fsop_KillFolderTree (gamepath, NULL);
			
			DMLResetCache (); // rebuild the cache next time
			}
			
		devKb = fsop_GetFreeSpaceKb (path);
		if (devKb > reqKb)
			{
			Debug ("DMLInstall (%s): OK there is enough space", gamename);
			return 1; // We have the space
			}
		}
	
	Debug ("DMLInstall (%s): Something has gone wrong", gamename);
	return 0;
	}
	

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Devolution: based on FIX94 implementation in WiiFlow
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// constant value for identification purposes
#define CONFIG_SIG        0x3EF9DB23

// version may change when future options are added
#define CONFIG_VERSION    0x0110

typedef struct global_config
{
	uint32_t signature;
	uint16_t version;
	uint16_t device_signature;
	uint32_t memcard_cluster;
	uint32_t disc1_cluster;
	uint32_t disc2_cluster;
	u32 options;
} gconfig; 

// constant value for identification purposes
#define DEVO_CONFIG_SIG                 0x3EF9DB23
// version may change when future options are added
#define DEVO_CONFIG_VERSION             0x0110
// option flags
#define DEVO_CONFIG_WIFILOG             (1<<0)
#define DEVO_CONFIG_WIDE                (1<<1)
#define DEVO_CONFIG_NOLED               (1<<2)

u8 *loader_bin = NULL;
static gconfig *DEVO_CONFIG = (gconfig*)0x80000020;

static bool IsGCCardAvailable (void)
	{
	CARD_Init (NULL, NULL);
    return (CARD_Probe (CARD_SLOTA) <= 0) ? false : true;
	}

// path is the full path to iso image
bool DEVO_Boot (char *path, u8 memcardId, bool widescreen, bool activity_led, bool wifi)
	{    
	//Read in loader.bin
	char loader_path[256];
	
	Debug ("DEVO_Boot: %s", path);
		
	snprintf(loader_path, sizeof (loader_path), "%s://ploader/plugins/loader.bin", vars.defMount);
	
	loader_bin = fsop_ReadFile (loader_path, 0, NULL);
	if (!loader_bin) return false;
	
	Debug ("DEVO_Boot: loader in memory");
	
	//start writing cfg to mem
	struct stat st;
	char full_path[256];
	int data_fd;
	char gameID[7];

	FILE *f = fopen(path, "rb");
	if (!f)
		{
		free (loader_bin);
		return false;
		}
		
	fread ((u8*)0x80000000, 1, 32, f);
	fclose (f);

	memcpy (&gameID, (u8*)0x80000000, 6);

	stat (path, &st);

	// fill out the Devolution config struct
	memset(DEVO_CONFIG, 0, sizeof(*DEVO_CONFIG));
	DEVO_CONFIG->signature = DEVO_CONFIG_SIG;
	DEVO_CONFIG->version = DEVO_CONFIG_VERSION;
	DEVO_CONFIG->device_signature = st.st_dev;
	DEVO_CONFIG->disc1_cluster = st.st_ino;

      // Pergame options
	if(wifi)
			DEVO_CONFIG->options |= DEVO_CONFIG_WIFILOG;
	if(widescreen)
			DEVO_CONFIG->options |= DEVO_CONFIG_WIDE;
	if(!activity_led)
			DEVO_CONFIG->options |= DEVO_CONFIG_NOLED;

	// make sure these directories exist, they are required for Devolution to function correctly
	snprintf(full_path, sizeof(full_path), "%s:/apps", fsop_GetDev (path));
	fsop_MakeFolder(full_path);
	
	snprintf(full_path, sizeof(full_path), "%s:/apps/gc_devo", fsop_GetDev (path));
	fsop_MakeFolder(full_path);

	if (!IsGCCardAvailable ())
		{
		char cardname[64];
		
		if (memcardId == 0)
			{
			if(gameID[3] == 'J') //Japanese Memory Card
				sprintf (cardname, "memcard_jap.bin");
			else
				sprintf (cardname, "memcard.bin");
			}
		else
			{
			if(gameID[3] == 'J') //Japanese Memory Card
				sprintf (cardname, "memcard%u_jap.bin", memcardId);
			else
				sprintf (cardname, "memcard%u.bin", memcardId);
			}
		
		Debug ("DEVO_Boot: using emulated card");
		
		// find or create a memcard file for emulation(as of devolution r115 it doesn't need to be 16MB)
		// this file can be located anywhere since it's passed by cluster, not name
		if(gameID[3] == 'J') //Japanese Memory Card
			snprintf(full_path, sizeof(full_path), "%s:/apps/gc_devo/%s", fsop_GetDev (path), cardname);
		else
			snprintf(full_path, sizeof(full_path), "%s:/apps/gc_devo/%s", fsop_GetDev (path), cardname);

		// check if file doesn't exist
		if (stat(full_path, &st) == -1)
			{
			// need to create it
			data_fd = open(full_path, O_WRONLY|O_CREAT);
			if (data_fd >= 0)
				{
				// make it 16MB, if we're creating a new memory card image
				//gprintf("Resizing memcard file...\n");
				ftruncate(data_fd, 16<<20);
				if (fstat(data_fd, &st) == -1)
						{
						// it still isn't created. Give up.
						st.st_ino = 0;
						}
				close(data_fd);
				}
			else
				{
				// couldn't open or create the memory card file
				st.st_ino = 0;
				}
			}
		}
	else
		{
		Debug ("DEVO_Boot: using real card");
		st.st_ino = 0;
		}


	// set FAT cluster for start of memory card file
	// if this is zero memory card emulation will not be used
	DEVO_CONFIG->memcard_cluster = st.st_ino;

	// flush disc ID and Devolution config out to memory
	DCFlushRange((void*)0x80000000, 64);
	
	Shutdown ();

	// Configure video mode as "suggested" to devolution
	GXRModeObj *vidmode;

	if (gameID[3] == 'E' || gameID[3] == 'J')
		vidmode = &TVNtsc480IntDf;
	else
		vidmode = &TVPal528IntDf;
		
	static u8 *sfb = NULL;
	sfb = SYS_AllocateFramebuffer(vidmode);
	VIDEO_ClearFrameBuffer(vidmode, sfb, COLOR_BLACK);
	sfb = MEM_K0_TO_K1(sfb);
	VIDEO_Configure(vidmode);
	VIDEO_SetNextFramebuffer(sfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();

	// the Devolution blob has an ID string at offset 4
	gprintf((const char*)loader_bin + 4);

	// devolution seems to like hbc stub. So we can force it.
	((void(*)(void))loader_bin)();

	return true;
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Nintendont support: The structs and enums are borrowed from Nintendont (as is IsWiiU).
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NIN_CFG_VERSION 0x00000003
#define NIN_MAGIC 0x01070CF6
#define NIN_CFG_MAXPADS 4

	typedef struct NIN_CFG
	{
		unsigned int		Magicbytes;
		unsigned int		Version;
		unsigned int		Config;
		unsigned int		VideoMode;
		unsigned int		Language;
		char	GamePath[255];
		char	CheatPath[255];
		unsigned int		MaxPads;
		unsigned int		GameID;
		unsigned int		MemCardBlocks;
	} NIN_CFG;

	enum ninconfigbitpos
	{
		NIN_CFG_BIT_CHEATS = (0),
		NIN_CFG_BIT_DEBUGGER = (1),	// Only for Wii Version
		NIN_CFG_BIT_DEBUGWAIT = (2),	// Only for Wii Version
		NIN_CFG_BIT_MEMCARDEMU = (3),
		NIN_CFG_BIT_CHEAT_PATH = (4),
		NIN_CFG_BIT_FORCE_WIDE = (5),
		NIN_CFG_BIT_FORCE_PROG = (6),
		NIN_CFG_BIT_AUTO_BOOT = (7),
		NIN_CFG_BIT_HID = (8),
		NIN_CFG_BIT_OSREPORT = (9),
		NIN_CFG_BIT_USB = (10),
		NIN_CFG_BIT_LED = (11),
		NIN_CFG_BIT_LOG = (12),
		NIN_CFG_BIT_LAST = (13),

		NIN_CFG_BIT_MC_MULTI = (13),
		NIN_CFG_BIT_NATIVE_SI = (14),
		NIN_CFG_BIT_WIIU_WIDE = (15),
	};

	enum ninconfig
	{
		NIN_CFG_CHEATS = (1 << NIN_CFG_BIT_CHEATS),
		NIN_CFG_DEBUGGER = (1 << NIN_CFG_BIT_DEBUGGER),	// Only for Wii Version
		NIN_CFG_DEBUGWAIT = (1 << NIN_CFG_BIT_DEBUGWAIT),	// Only for Wii Version
		NIN_CFG_MEMCARDEMU = (1 << NIN_CFG_BIT_MEMCARDEMU),
		NIN_CFG_CHEAT_PATH = (1 << NIN_CFG_BIT_CHEAT_PATH),
		NIN_CFG_FORCE_WIDE = (1 << NIN_CFG_BIT_FORCE_WIDE),
		NIN_CFG_FORCE_PROG = (1 << NIN_CFG_BIT_FORCE_PROG),
		NIN_CFG_AUTO_BOOT = (1 << NIN_CFG_BIT_AUTO_BOOT),
		NIN_CFG_HID = (1 << NIN_CFG_BIT_HID),
		NIN_CFG_OSREPORT = (1 << NIN_CFG_BIT_OSREPORT),
		NIN_CFG_USB = (1 << NIN_CFG_BIT_USB),
		NIN_CFG_LED = (1 << NIN_CFG_BIT_LED),
		NIN_CFG_LOG = (1 << NIN_CFG_BIT_LOG),
		NIN_CFG_MC_MULTI = (1 << NIN_CFG_BIT_MC_MULTI),
		NIN_CFG_NATIVE_SI = (1 << NIN_CFG_BIT_NATIVE_SI),
		NIN_CFG_WIIU_WIDE = (1 << NIN_CFG_BIT_WIIU_WIDE),
	};

	enum ninvideomodeindex
	{
		NIN_VID_INDEX_AUTO = (0),
		NIN_VID_INDEX_FORCE = (1),
		NIN_VID_INDEX_NONE = (2),
		NIN_VID_INDEX_FORCE_DF = (4),
		NIN_VID_INDEX_FORCE_PAL50 = (0),
		NIN_VID_INDEX_FORCE_PAL60 = (1),
		NIN_VID_INDEX_FORCE_NTSC = (2),
		NIN_VID_INDEX_FORCE_MPAL = (3),
	};

	enum ninvideomode
	{
		NIN_VID_AUTO = (NIN_VID_INDEX_AUTO << 16),
		NIN_VID_FORCE = (NIN_VID_INDEX_FORCE << 16),
		NIN_VID_NONE = (NIN_VID_INDEX_NONE << 16),
		NIN_VID_FORCE_DF = (NIN_VID_INDEX_FORCE_DF << 16),

		NIN_VID_MASK = NIN_VID_AUTO | NIN_VID_FORCE | NIN_VID_NONE | NIN_VID_FORCE_DF,

		NIN_VID_FORCE_PAL50 = (1 << NIN_VID_INDEX_FORCE_PAL50),
		NIN_VID_FORCE_PAL60 = (1 << NIN_VID_INDEX_FORCE_PAL60),
		NIN_VID_FORCE_NTSC = (1 << NIN_VID_INDEX_FORCE_NTSC),
		NIN_VID_FORCE_MPAL = (1 << NIN_VID_INDEX_FORCE_MPAL),

		NIN_VID_FORCE_MASK = NIN_VID_FORCE_PAL50 | NIN_VID_FORCE_PAL60 | NIN_VID_FORCE_NTSC | NIN_VID_FORCE_MPAL,

		NIN_VID_PROG = (1 << 4),	//important to prevent blackscreens
	};

	enum
	{
		VID_AUTO = 0,
		VID_NTSC = 1,
		VID_MPAL = 2,
		VID_PAL50 = 3,
		VID_PAL60 = 4,
	};

	// Borrowed from Nintendont, renamed from IsWiiU
	bool RunningOnWiiU(void)
	{
		return ((*(vu32*)(0xCd8005A0) >> 16) == 0xCAFE);
	}

	char *NIN_GetLanguage(int language)
	{
		static char *languageoptions[6] = { "English", "German", "French", "Spanish", "Italian", "Dutch" };

		return language < 0 || language >= ARRAY_LENGTH(languageoptions) ? "Auto" : languageoptions[language];
	}

	bool NIN_SupportsArgsboot(const char * ninPath)
	{
		if (ninPath == NULL)
			return false;

		if (!fsop_FileExist(ninPath))
			return false;

		bool supportsArgsboot = false;

		const size_t bufferSize = 4096;
		char buffer[bufferSize];

		const char * const magic = "argsboot";
		const size_t magicLength = strlen(magic);

		FILE *file = fopen(ninPath, "rb");

		if (file == NULL)
			return false;

		while (fread(buffer, bufferSize, 1, file) > 0)
		{
			int i;
			// Nintendont aligns this to a 16 byte boundary.
			for (i = 0; i < bufferSize; i += 16)
			{
				if (memcmp(buffer + i, magic, magicLength) == 0)
				{
					supportsArgsboot = true;
					break;
				}
			}

			if (supportsArgsboot)
				break;
		}

		fclose(file);

		return supportsArgsboot;
	}

	void NIN_GetVersion(char * const ninPath, char * versionBuffer)
	{
		if (ninPath == NULL)
			return;

		if (!fsop_FileExist(ninPath))
			return;

		const char * const magic = "$$Version:";
		const size_t magicLength = strlen(magic);

		FILE * file = fopen(ninPath, "rb");

		if (file == NULL)
			return;

		const size_t bufferSize = 4096;
		char buffer[bufferSize];

		bool done = false;

		while (fread(buffer, bufferSize, 1, file) > 0)
		{
			int i;
			// Nintendont aligns this to a 32 byte boundary.
			for (i = 0; i < bufferSize; i += 32)
			{
				if (memcmp(buffer + i, magic, magicLength) == 0)
				{
					strcpy(versionBuffer, buffer + i + magicLength);

					done = true;
					break;
				}
			}

			if (done)
				break;
		}
		fclose(file);
	}

	bool NIN_Boot(s_game *game, s_gameConfig *gameConf, char *error_string, int error_strlen)
	{
		if (game == NULL)
		{
			Debug ("NIN_Boot: game is null!");
			const char * const error = "An internal error occurred. Game info is null.";
			if (error_string != NULL)
				strncpy (error_string, error, strlen(error));
			return false;
		}

		if (gameConf == NULL)
		{
			Debug ("NIN_Boot: gameConf is null!");
			const char * const error = "An internal error occurred. Game config is null.";
			if (error_string != NULL)
				strncpy (error_string, error, strlen(error));
			return false;
		}

		const bool gameIsOnUSB = strstr(game->source, "usb:/") != NULL;

		// Prepare to boot Nintendont!
		char ninPath[256] = { 0 };
		sprintf(ninPath, "%s://apps/nintendont/boot.dol", gameIsOnUSB ? "usb" : "sd");
		Debug("NIN_Boot: looking for Nintendont at %s.", ninPath);

		if (!fsop_FileExist(ninPath))
		{
			Debug("NIN_Boot: unable to find Nintendont at %s.", ninPath);
			const char * const error = "Nintendont doesn't seem to be installed in /apps/nintendont.";
			if (error_string != NULL)
				strncpy(error_string, error, strlen(error));
			return false;
		}

		if (!NIN_SupportsArgsboot(ninPath))
		{
			Debug("NIN_Boot: This version of Nintendont doesn't support argsboot. postLoader only supports Nintendont version 1.98 and higher.");
			const char * const error = "This version of Nintendont doesn't support argsboot.\nPlease install Nintendont 1.98 or higher.\n";
			if (error_string != NULL)
				strncpy(error_string, error, strlen(error));
			return false;
		}

		char versionBuffer[256] = { 0 };
		NIN_GetVersion(ninPath, versionBuffer);

		Debug("NIN_Boot: version %s is installed.", strlen(versionBuffer) == 0 ? "older than 3.324" : versionBuffer);

		Debug ("NIN_Boot: preparing to launch %s", game->name);

		NIN_CFG nin_config = { 0 };
		nin_config.Magicbytes = NIN_MAGIC;
		nin_config.Version = NIN_CFG_VERSION;

		nin_config.Config = NIN_CFG_AUTO_BOOT;

		/*
		   OSReport is only useful for USB Gecko users.
		   In the future, if Nintendont gains wifi logging and the user enables it, we'll toggle it on as well as well.
		   For now, however, wifi is commented out. 
		*/
		if (!RunningOnWiiU () && (usb_isgeckoalive (EXI_CHANNEL_1) /* || gameConf->wifi*/ ))
			nin_config.Config |= NIN_CFG_OSREPORT;
		

		if (gameIsOnUSB)
		{
			Debug ("NIN_Boot: game is on USB.");
			nin_config.Config |= NIN_CFG_USB;
		}
		else
			Debug ("NIN_Boot: game is on SD.");

		if (gameConf->dmlNMM)
		{
			Debug ("NIN_Boot: enabling MC emulation.");
			nin_config.Config |= NIN_CFG_MEMCARDEMU;
		}

		if (gameConf->widescreen)
		{
			Debug ("NIN_Boot: forcing widescreen.");
			nin_config.Config |= NIN_CFG_FORCE_WIDE;
		}

		if (gameConf->activity_led)
		{
			Debug ("NIN_Boot: activity LED will be active.");
			nin_config.Config |= NIN_CFG_LED;
		}

		// On newer versions of Nintendont, this is always enabled.
		nin_config.Config |= NIN_CFG_HID;

		const bool progressive = (CONF_GetProgressiveScan() > 0) && VIDEO_HaveComponentCable();
		if (progressive) //important to prevent blackscreens
		{
			Debug ("NIN_Boot: this %s seems to be able to use progressive mode.", RunningOnWiiU() ? "Wii U" : "Wii");
			nin_config.VideoMode |= NIN_VID_PROG;
		}
		else
			nin_config.VideoMode &= ~NIN_VID_PROG;

		int force = 0;

		switch (gameConf->dmlVideoMode)
		{
		case VID_NTSC:
			force = NIN_VID_FORCE | NIN_VID_FORCE_NTSC;
			Debug ("NIN_Boot: forcing video mode to NTSC.");
			break;
		case VID_MPAL:
			force = NIN_VID_FORCE | NIN_VID_FORCE_MPAL;
			Debug ("NIN_Boot: forcing video mode to MPAL.");
			break;
		case VID_PAL50:
			force = NIN_VID_FORCE | NIN_VID_FORCE_PAL50;
			Debug ("NIN_Boot: forcing video mode to PAL50.");
			break;
		case VID_PAL60:
			force = NIN_VID_FORCE | NIN_VID_FORCE_PAL60;
			Debug ("NIN_Boot: forcing video mode to PAL60.");
			break;
		default:
			force = NIN_VID_AUTO;
			Debug ("NIN_Boot: letting Nintendont decide on the video mode.");
			break;
		}

		nin_config.VideoMode |= force;
		
		nin_config.Language = gameConf->ninLanguage;
		Debug ("NIN_Boot: game language set to %s.", NIN_GetLanguage(nin_config.Language));

		/*
			Nintendont expects the path to look something like this (we use /games since most other loaders do, too):
			"/games/<game id or folder name>/game.iso", without the boot device (e.g. "usb:/", "sd:/"),
			so we skip it as we build the path.
		*/

		// It should be safe to assume that strstr isn't null here.
		const char * const gamesPath = strstr (game->source, "/games/");
		sprintf (nin_config.GamePath, "%s/game.iso", gamesPath);
		
		Debug ("NIN_Boot: using %s as game ISO path.", nin_config.GamePath);

		// Just let Nintendont handle the cheat path.
		if (gameConf->ocarina)
		{
			nin_config.Config |= NIN_CFG_CHEATS;
			Debug ("NIN_Boot: cheats are enabled. Nintendont will decide on the cheat path.");
		}

		nin_config.MaxPads = NIN_CFG_MAXPADS;

		memcpy (&nin_config.GameID, game->asciiId, sizeof(int));
		Debug ("NIN_Boot: game ID is %s.", game->asciiId);

		if (LoadHB(ninPath, EXECUTE_ADDR) <= 0)
		{
			const char * const error = "Unable to load nintendont dol.";
			if (error_string != NULL)
				strncpy(error_string, error, strlen(error));
			return false;
		}

		// Point of no return: we assume everything is good here, because if not, postLoader is likely going to crash.
		Video_LoadTheme(0); // Make sure that theme isn't loaded

		CoverCache_Flush();

		struct __argv arg = { 0 };

		arg.argvMagic = ARGV_MAGIC;
		arg.argc = 2;

		arg.commandLine = (char *)CMDL_ADDR;

		sprintf(arg.commandLine, "%s", ninPath);

		arg.length = strlen(ninPath) + 1;

		memcpy(&arg.commandLine[arg.length], &nin_config, sizeof(NIN_CFG));
		
		arg.length += sizeof(NIN_CFG);

		arg.argv = &arg.commandLine;
		arg.endARGV = arg.argv + 1;

		memmove(ARGS_ADDR, &arg, sizeof(arg));
		DCFlushRange(ARGS_ADDR, sizeof(arg) + arg.length);

		// NIN_Boot handled the args already, so we don't want DolBoot to handle it for us.
		const bool useBuiltinArgs = false;
		DolBoot(useBuiltinArgs);

		Debug("NIN_Boot: we should never, ever reach here: %d in %s", __LINE__, __FILE__);

		// It's preferable to hang instead of crashing/going off to outer space.
		while (1);

		return true;
	}
