// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
// Copyright (C) 2000-2006 by Sergey Makovkin (CSDoom .62).
// Copyright (C) 2006-2010 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	CL_MAIN
//
//-----------------------------------------------------------------------------


#include "doomtype.h"
#include "doomstat.h"
#include "gstrings.h"
#include "d_player.h"
#include "g_game.h"
#include "d_net.h"
#include "p_local.h"
#include "s_sound.h"
#include "gi.h"
#include "i_net.h"
#include "i_system.h"
#include "i_video.h"
#include "c_dispatch.h"
#include "st_stuff.h"
#include "m_argv.h"
#include "cl_main.h"
#include "c_effect.h"
#include "c_console.h"
#include "d_main.h"
#include "p_ctf.h"
#include "m_random.h"
#include "w_wad.h"
#include "md5.h"
#include "m_fileio.h"
#include "r_sky.h"
#include "cl_demo.h"
#include "cl_download.h"
#include "cl_maplist.h"
#include "cl_vote.h"

#include <string>
#include <vector>
#include <map>
#include <sstream>

#ifdef _XBOX
#include "i_xbox.h"
#endif

#if _MSC_VER == 1310
#pragma optimize("",off)
#endif

// denis - fancy gfx, but no game manipulation
bool clientside = true, serverside = false;
baseapp_t baseapp = client;

extern bool step_mode;

// denis - client version (VERSION or other supported)
short version = 0;
int gameversion = 0;				// GhostlyDeath -- Bigger Game Version
int gameversiontosend = 0;		// If the server is 0.4, let's fake our client info

buf_t     net_buffer(MAX_UDP_PACKET);

bool      noservermsgs;
int       last_received;
byte      last_svgametic = 0;
int       last_player_update = 0;

std::string connectpasshash = "";

BOOL      connected;
netadr_t  serveraddr; // address of a server
netadr_t  lastconaddr;

int       packetseq[256];
byte      packetnum;

// denis - unique session key provided by the server
std::string digest;

// denis - clientside compressor, used for decompression
huffman_client compressor;

// denis - fast netid lookup
typedef std::map<size_t, AActor::AActorPtr> netid_map_t;
netid_map_t actor_by_netid;

std::string server_host = "";	// hostname of server

// [SL] 2011-06-27 - Class to record and playback network recordings
NetDemo netdemo;
static const std::string default_netdemo_filename("%n_%g_%w-%m_%d");
// [SL] 2011-07-06 - not really connected (playing back a netdemo)
bool simulated_connection = false;		

EXTERN_CVAR (sv_weaponstay)

EXTERN_CVAR (cl_name)
EXTERN_CVAR (cl_color)
EXTERN_CVAR (cl_team)
EXTERN_CVAR (cl_skin)
EXTERN_CVAR (cl_gender)
EXTERN_CVAR (cl_unlag)

CVAR_FUNC_IMPL (cl_autoaim)
{
	if (var < 0)
		var.Set(0.0f);
	else if (var > 5000.0f)
		var.Set(5000.0f);
}

CVAR_FUNC_IMPL (cl_updaterate)
{
	if (var < 1.0f)
		var.Set(1.0f);
	else if (var > 3.0f)
		var.Set(3.0f);
}

EXTERN_CVAR (cl_weaponpref1)
EXTERN_CVAR (cl_weaponpref2)
EXTERN_CVAR (cl_weaponpref3)
EXTERN_CVAR (cl_weaponpref4)
EXTERN_CVAR (cl_weaponpref5)
EXTERN_CVAR (cl_weaponpref6)
EXTERN_CVAR (cl_weaponpref7)
EXTERN_CVAR (cl_weaponpref8)
EXTERN_CVAR (cl_weaponpref9)

static cvar_t *weaponpref_cvar_map[NUMWEAPONS] = {
	&cl_weaponpref1, &cl_weaponpref2, &cl_weaponpref3, &cl_weaponpref4,
	&cl_weaponpref5, &cl_weaponpref6, &cl_weaponpref7, &cl_weaponpref8,
	&cl_weaponpref9 };

//
// CL_SetWeaponPreferenceCvar
//
// Sets the value of one of the cl_weaponpref* cvars identified by slot
//
void CL_SetWeaponPreferenceCvar(int slot, weapontype_t weapon)
{
	if (slot < 0 || slot >= NUMWEAPONS)
		return;

	weaponpref_cvar_map[slot]->Set(static_cast<float>(weapon));
}

//
// CL_RefreshWeaponPreferenceCvars
//
// Copies userinfo.weapon_prefs to the cl_weaponpref* cvars
//
void CL_RefreshWeaponPreferenceCvars()
{
	weapontype_t *prefs = consoleplayer().userinfo.weapon_prefs;

	for (size_t i = 0; i < NUMWEAPONS; i++)
		weaponpref_cvar_map[i]->Set(static_cast<float>(prefs[i]));
}

//
// CL_PrepareWeaponPreferenceUserInfo
//
// Copies the weapon order preferences from the cl_weaponpref* cvars
// to the userinfo struct for the consoleplayer.
//
void CL_PrepareWeaponPreferenceUserInfo()
{
	weapontype_t *prefs = consoleplayer().userinfo.weapon_prefs;

	for (size_t i = 0; i < NUMWEAPONS; i++)
		prefs[i] = static_cast<weapontype_t>(weaponpref_cvar_map[i]->asInt());
}

EXTERN_CVAR (sv_maxplayers)
EXTERN_CVAR (sv_maxclients)
EXTERN_CVAR (sv_infiniteammo)
EXTERN_CVAR (sv_fraglimit)
EXTERN_CVAR (sv_timelimit)
EXTERN_CVAR (sv_nomonsters)
EXTERN_CVAR (sv_fastmonsters)
EXTERN_CVAR (sv_allowexit)
EXTERN_CVAR (sv_fragexitswitch)
EXTERN_CVAR (sv_allowjump)
EXTERN_CVAR (sv_allowredscreen)
EXTERN_CVAR (sv_scorelimit)
EXTERN_CVAR (sv_monstersrespawn)
EXTERN_CVAR (sv_itemsrespawn)
EXTERN_CVAR (sv_allowcheats)
EXTERN_CVAR (sv_allowtargetnames)
EXTERN_CVAR (cl_mouselook)
EXTERN_CVAR (sv_freelook)
EXTERN_CVAR (cl_connectalert)
EXTERN_CVAR (cl_disconnectalert)
EXTERN_CVAR (waddirs)
EXTERN_CVAR (cl_autorecord)
EXTERN_CVAR (cl_splitnetdemos)

void CL_RunTics (void);
void CL_PlayerTimes (void);
void CL_GetServerSettings(void);
void CL_RequestDownload(std::string filename, std::string filehash = "");
void CL_TryToConnect(DWORD server_token);
void CL_Decompress(int sequence);

void CL_LocalDemoTic(void);
void CL_NetDemoStop(void);
void CL_NetDemoSnapshot(void);

//	[Toke - CTF]
void CalcTeamFrags (void);

// some doom functions (not csDoom)
size_t P_NumPlayersInGame();
void G_PlayerReborn (player_t &player);
void CL_SpawnPlayer ();
void P_KillMobj (AActor *source, AActor *target, AActor *inflictor, bool joinkill);
void P_SetPsprite (player_t *player, int position, statenum_t stnum);
void P_ExplodeMissile (AActor* mo);
void G_SetDefaultTurbo (void);
void P_CalcHeight (player_t *player);
bool P_CheckMissileSpawn (AActor* th);
void P_SetFloorCeil(AActor *mo);
void CL_SetMobjSpeedAndAngle(void);

void P_PlayerLookUpDown (player_t *p);
team_t D_TeamByName (const char *team);
gender_t D_GenderByName (const char *gender);
int V_GetColorFromString (const DWORD *palette, const char *colorstring);
void AM_Stop();

void Host_EndGame(const char *msg)
{
    Printf(PRINT_HIGH, "%s", msg);
	CL_QuitNetGame();
}

void CL_QuitNetGame(void)
{
	if(connected)
	{
		MSG_WriteMarker(&net_buffer, clc_disconnect);
		NET_SendPacket(net_buffer, serveraddr);
		SZ_Clear(&net_buffer);
		sv_gametype = GM_COOP;
	}
	
	if (paused)
	{
		paused = false;
		S_ResumeSound ();
	}

	memset (&serveraddr, 0, sizeof(serveraddr));
	connected = false;
	gameaction = ga_fullconsole;
	noservermsgs = false;
	AM_Stop();

	serverside = clientside = true;
	network_game = false;
	
	sv_freelook = 1;
	sv_allowjump = 1;
	sv_allowexit = 1;
	sv_allowredscreen = 1;

	actor_by_netid.clear();
	players.clear();

	if (netdemo.isRecording())
		netdemo.stopRecording();

	if (netdemo.isPlaying())
		netdemo.stopPlaying();

	// Reset the palette to default
	if (I_HardwareInitialized())
	{
		int lu_palette = W_GetNumForName("PLAYPAL");
		if (lu_palette != -1)
		{
			byte *pal = (byte *)W_CacheLumpNum(lu_palette, PU_CACHE);
			if (pal)
			{
				I_SetOldPalette(pal);
			}
		}
	}

	// Reset the palette to default
	if (I_HardwareInitialized())
	{
		int lu_palette = W_GetNumForName("PLAYPAL");
		if (lu_palette != -1)
		{
			byte *pal = (byte *)W_CacheLumpNum(lu_palette, PU_CACHE);
			if (pal)
			{
				I_SetOldPalette(pal);
			}
		}
	}
}


void CL_Reconnect(void)
{
	if (connected)
	{
		MSG_WriteMarker(&net_buffer, clc_disconnect);
		NET_SendPacket(net_buffer, serveraddr);
		SZ_Clear(&net_buffer);
		connected = false;
		gameaction = ga_fullconsole;

		actor_by_netid.clear();
	}
	else if (lastconaddr.ip[0])
	{
		serveraddr = lastconaddr;
	}

	connecttimeout = 0;
}

//
// CL_ConnectClient
//
void CL_ConnectClient(void)
{
	player_t &player = idplayer(MSG_ReadByte());
	
	if (!cl_connectalert)
		return;
	
	// GhostlyDeath <August 1, 2008> -- Play connect sound
	if (&player == &consoleplayer())
		return;
		
	S_Sound (CHAN_INTERFACE, "misc/pljoin", 1, ATTN_NONE);
}

//
// CL_DisconnectClient
//
void CL_DisconnectClient(void)
{
	player_t &player = idplayer(MSG_ReadByte());

	// if this was our displayplayer, update camera
	if(&player == &displayplayer())
		displayplayer_id = consoleplayer_id;

	if(player.mo)
	{
		player.mo->Destroy();
		P_DisconnectEffect (player.mo);
	}

	size_t i;

	for(i = 0; i < players.size(); i++)
	{
		if(&players[i] == &player)
		{
			// GhostlyDeath <August 1, 2008> -- Play disconnect sound
			// GhostlyDeath <August 6, 2008> -- Only if they really are inside
			if (cl_disconnectalert && &player != &consoleplayer())
				S_Sound (CHAN_INTERFACE, "misc/plpart", 1, ATTN_NONE);
			players.erase(players.begin() + i);
			break;
		}
	}

	// repair mo after player pointers are reset
	for(i = 0; i < players.size(); i++)
	{
		if(players[i].mo)
			players[i].mo->player = &players[i];
	}
}

/////// CONSOLE COMMANDS ///////

BEGIN_COMMAND (stepmode)
{
    if (step_mode)
        step_mode = false;
    else
        step_mode = true;
        
    return;
}
END_COMMAND (stepmode)

BEGIN_COMMAND (connect)
{
	if (argc == 1)
	{
	    Printf(PRINT_HIGH, "Usage: connect ip[:port] [password]\n");
	    Printf(PRINT_HIGH, "\n");
	    Printf(PRINT_HIGH, "Connect to a server, with optional port number");
	    Printf(PRINT_HIGH, " and/or password\n");
	    Printf(PRINT_HIGH, "eg: connect 127.0.0.1\n");
	    Printf(PRINT_HIGH, "eg: connect 192.168.0.1:12345 secretpass\n");
	    
	    return;
	}
	
	CL_QuitNetGame();

	if (argc > 1)
	{
		std::string target = argv[1];

        // [Russell] - Passworded servers
        if(argc > 2)
        {
            connectpasshash = MD5SUM(argv[2]);
        }
        else
        {
            connectpasshash = "";
        }

		if(NET_StringToAdr (target.c_str(), &serveraddr))
		{
			if (!serveraddr.port)
				I_SetPort(serveraddr, SERVERPORT);

			lastconaddr = serveraddr;
		}
		else
		{
			Printf(PRINT_HIGH, "Could not resolve host %s\n", target.c_str());
			memset(&serveraddr, 0, sizeof(serveraddr));
		}
	}

	connecttimeout = 0;
}
END_COMMAND (connect)


BEGIN_COMMAND (disconnect)
{
	CL_QuitNetGame();
}
END_COMMAND (disconnect)


BEGIN_COMMAND (reconnect)
{
	CL_Reconnect();
}
END_COMMAND (reconnect)

BEGIN_COMMAND (players)
{
	// Gather all ingame players
	std::map<int, std::string> mplayers;
	for (size_t i = 0; i < players.size(); ++i) {
		if (players[i].ingame()) {
			mplayers[players[i].id] = players[i].userinfo.netname;
		}
	}

	// Print them, ordered by player id.
	Printf(PRINT_HIGH, " PLAYERS IN GAME:\n");
	for (std::map<int, std::string>::iterator it = mplayers.begin();it != mplayers.end();++it) {
		Printf(PRINT_HIGH, "%d. %s\n", (*it).first, (*it).second.c_str());
	}
}
END_COMMAND (players)


BEGIN_COMMAND (playerinfo)
{
	player_t *player = &consoleplayer();

	if(argc > 1)
	{
		size_t who = atoi(argv[1]);
		player_t &p = idplayer(who);

		if(!validplayer(p))
		{
			Printf (PRINT_HIGH, "Bad player number\n");
			return;
		}
		else
			player = &p;
	}

	if(!validplayer(*player))
	{
		Printf (PRINT_HIGH, "Not a valid player\n");
		return;
	}

	Printf (PRINT_HIGH, "---------------[player info]----------- \n");
	Printf (PRINT_HIGH, " userinfo.netname     - %s \n",	player->userinfo.netname);
	Printf (PRINT_HIGH, " userinfo.team        - %d \n",	player->userinfo.team);
	Printf (PRINT_HIGH, " userinfo.color       - #%06x \n",	player->userinfo.color);
	Printf (PRINT_HIGH, " userinfo.skin        - %s \n",	skins[player->userinfo.skin].name);
	Printf (PRINT_HIGH, " userinfo.gender      - %d \n",	player->userinfo.gender);
	Printf (PRINT_HIGH, " spectator            - %d \n",	player->spectator);
	Printf (PRINT_HIGH, " time                 - %d \n",	player->GameTime);
	Printf (PRINT_HIGH, "--------------------------------------- \n");
}
END_COMMAND (playerinfo)


BEGIN_COMMAND (kill)
{
    if (sv_allowcheats)
        MSG_WriteMarker(&net_buffer, clc_kill);
    else
        Printf (PRINT_HIGH, "You must run the server with '+set sv_allowcheats 1' to enable this command.\n");
}
END_COMMAND (kill)


BEGIN_COMMAND (serverinfo)
{   
	std::vector<std::string> server_cvars;

    cvar_t *Cvar = GetFirstCvar();
    size_t MaxFieldLength = 0;
    
    // [Russell] - Find the largest cvar name, used for formatting
    while (Cvar)
	{			
        if (Cvar->flags() & CVAR_SERVERINFO)
        {
            size_t FieldLength = strlen(Cvar->name());
            
            if (FieldLength > MaxFieldLength)
                MaxFieldLength = FieldLength;                

			// store this cvar name in our vector to be sorted later
			server_cvars.push_back(Cvar->name());
        }
        
        Cvar = Cvar->GetNext();
    }

	// sort the list of cvars
	std::sort(server_cvars.begin(), server_cvars.end());

    // Heading
    Printf (PRINT_HIGH,	"\n%*s - Value\n", MaxFieldLength, "Name");
    
    // Data
	for (size_t i = 0; i < server_cvars.size(); i++)
	{
		cvar_t *dummy;
		Cvar = cvar_t::FindCVar(server_cvars[i].c_str(), &dummy);

		Printf(PRINT_HIGH, 
				"%*s - %s\n", 
				MaxFieldLength,
				Cvar->name(), 
				Cvar->cstring());          
	}
    
    Printf (PRINT_HIGH,	"\n");
}
END_COMMAND (serverinfo)

// rate: takes a kbps value
CVAR_FUNC_IMPL (rate)
{
/*  // [SL] 2011-09-02 - Commented out this code as it would force clients with
	// a version that has rate in kbps to have a rate of < 5000 bps on an older
	// server version that has rate in bps, resulting in an unplayable connection.

	const int max_rate = 5000;	// 40Mbps, likely set erroneously
	if (var > max_rate)
		var.RestoreDefault();		
*/
	if (connected)
	{
		MSG_WriteMarker(&net_buffer, clc_rate);
		MSG_WriteLong(&net_buffer, (int)var);
	}
}


BEGIN_COMMAND (rcon)
{
	if (connected && argc > 1)
	{
		char  command[256];

		if (argc == 2)
			sprintf(command, "%s", argv[1]);
		if (argc == 3)
			sprintf(command, "%s %s", argv[1], argv[2]);
		if (argc == 4)
			sprintf(command, "%s %s %s", argv[1], argv[2], argv[3]);

		MSG_WriteMarker(&net_buffer, clc_rcon);
		MSG_WriteString(&net_buffer, command);
	}
}
END_COMMAND (rcon)


BEGIN_COMMAND (rcon_password)
{
	if (connected && argc > 1)
	{
		bool login = true;

		MSG_WriteMarker(&net_buffer, clc_rcon_password);
		MSG_WriteByte(&net_buffer, login);

		std::string password = argv[1];
		MSG_WriteString(&net_buffer, MD5SUM(password + digest).c_str());
	}
}
END_COMMAND (rcon_password)

BEGIN_COMMAND (rcon_logout)
{
	if (connected)
	{
		bool login = false;

		MSG_WriteMarker(&net_buffer, clc_rcon_password);
		MSG_WriteByte(&net_buffer, login);
		MSG_WriteString(&net_buffer, "");
	}
}
END_COMMAND (rcon_logout)


BEGIN_COMMAND (playerteam)
{
	Printf (PRINT_MEDIUM, "Your Team is %d \n", consoleplayer().userinfo.team);
}
END_COMMAND (playerteam)

BEGIN_COMMAND (changeteams)
{
	if (consoleplayer().userinfo.team == TEAM_BLUE)
		cl_team.Set("RED");
	else if (consoleplayer().userinfo.team == TEAM_RED)
		cl_team.Set("BLUE");
}
END_COMMAND (changeteams)

BEGIN_COMMAND (spectate)
{
	MSG_WriteMarker(&net_buffer, clc_spectate);
	MSG_WriteByte(&net_buffer, true);
}
END_COMMAND (spectate)


BEGIN_COMMAND (join)
{
	if (P_NumPlayersInGame() >= sv_maxplayers)
	{
		C_MidPrint("The game is currently full", NULL);
		return;
	}

	MSG_WriteMarker(&net_buffer, clc_spectate);
	MSG_WriteByte(&net_buffer, false);
}
END_COMMAND (join)

void STACK_ARGS call_terms (void);

void CL_QuitCommand()
{
	call_terms();
	exit (0);
}

BEGIN_COMMAND (quit)
{
	CL_QuitCommand();
}
END_COMMAND (quit)

// An alias for 'quit'
BEGIN_COMMAND (exit)
{
	CL_QuitCommand();
}
END_COMMAND (exit)

//
// NetDemo related functions
//

//
// CL_GenerateNetDemoFileName
//
// 
std::string CL_GenerateNetDemoFileName(const std::string &filename = default_netdemo_filename)
{
	const std::string expanded_filename(M_ExpandTokens(filename));	
	std::string newfilename(expanded_filename + ".odd");
	newfilename = I_GetUserFileName(newfilename.c_str());

	FILE *fp;
	int counter = 1;
	char cntstr[5];

	// keep trying to find a filename that doesn't yet exist
	while ( (fp = fopen(newfilename.c_str(), "r")) )
	{
		fclose(fp);
		
		// add a number to the end of the filename
		sprintf(cntstr, "%i", counter);
		newfilename = expanded_filename + cntstr + ".odd";
		newfilename = I_GetUserFileName(newfilename.c_str());

		counter++;
		if (counter > 9999)		// don't overflow cntstr
			break;
	}

	return newfilename;
}

void CL_NetDemoStop()
{
	netdemo.stopPlaying();
}

void CL_NetDemoRecord(const std::string &filename)
{
	netdemo.startRecording(filename);
}

void CL_NetDemoPlay(const std::string &filename)
{
	netdemo.startPlaying(filename);
}

BEGIN_COMMAND(stopnetdemo)
{
	if (netdemo.isRecording())
	{
		netdemo.stopRecording();
	} 
	else if (netdemo.isPlaying())
	{
		netdemo.stopPlaying();
	}
}
END_COMMAND(stopnetdemo)

BEGIN_COMMAND(netrecord)
{
	if (netdemo.isRecording())
	{
		Printf(PRINT_HIGH, "Already recording a netdemo.  Please stop recording before beginning a new netdemo recording.\n");
		return;
	}

	if (!connected || simulated_connection)
	{
		Printf(PRINT_HIGH, "You must be connected to a server to record a netdemo.\n");
		return;
	}

	std::string filename;
	if (argc > 1 && strlen(argv[1]) > 0)
		filename = CL_GenerateNetDemoFileName(argv[1]);
	else
		filename = CL_GenerateNetDemoFileName();

	CL_NetDemoRecord(filename.c_str());
}
END_COMMAND(netrecord)

BEGIN_COMMAND(netpause)
{
	if (netdemo.isPaused())
	{
		netdemo.resume();
		paused = false;
		Printf(PRINT_HIGH, "Demo resumed.\n");
	} 
	else if (netdemo.isPlaying())
	{
		netdemo.pause();
		paused = true;
		Printf(PRINT_HIGH, "Demo paused.\n");
	}
}
END_COMMAND(netpause)

BEGIN_COMMAND(netplay)
{
	if(argc < 1)
	{
		Printf(PRINT_HIGH, "Usage: netplay <demoname>\n");
		return;
	}

	if (!connected)
	{
 		G_CheckDemoStatus();	// cleans up vanilla demo or single player game
	}

	CL_QuitNetGame();
	connected = false;

	std::string filename = argv[1];
	CL_NetDemoPlay(filename);
}
END_COMMAND(netplay)

BEGIN_COMMAND(netdemostats)
{
	if (!netdemo.isPlaying())
		return;

	std::vector<int> maptimes = netdemo.getMapChangeTimes();
	int curtime = netdemo.calculateTimeElapsed();
	int totaltime = netdemo.calculateTotalTime();

	Printf(PRINT_HIGH, "\n%s\n", netdemo.getFileName().c_str());
	Printf(PRINT_HIGH, "============================================\n");
	Printf(PRINT_HIGH, "Total time: %i seconds\n", totaltime);
	Printf(PRINT_HIGH, "Current position: %i seconds (%i%%)\n",
		curtime, curtime * 100 / totaltime);
	Printf(PRINT_HIGH, "Number of maps: %i\n", maptimes.size());
	for (size_t i = 0; i < maptimes.size(); i++)
	{
		Printf(PRINT_HIGH, "> %02i Starting time: %i seconds\n",
			i + 1, maptimes[i]);
	}
}
END_COMMAND(netdemostats)

BEGIN_COMMAND(netff)
{
	if (netdemo.isPlaying())
		netdemo.nextSnapshot(&net_message);
}
END_COMMAND(netff)

BEGIN_COMMAND(netrew)
{
	if (netdemo.isPlaying())
		netdemo.prevSnapshot(&net_message);
}
END_COMMAND(netrew)

BEGIN_COMMAND(netnextmap)
{
	if (netdemo.isPlaying())
		netdemo.nextMap(&net_message);
}
END_COMMAND(netnextmap)

BEGIN_COMMAND(netprevmap)
{
	if (netdemo.isPlaying())
		netdemo.prevMap(&net_message);
}
END_COMMAND(netprevmap)


//
// CL_MoveThing
//
void CL_MoveThing(AActor *mobj, fixed_t x, fixed_t y, fixed_t z)
{
	if (!mobj)
		return;

	// [SL] 2011-11-06 - Return before setting the thing's floorz value if
	// the thing hasn't moved.  This ensures the floorz value is correct for
	// things that have spawned (too close to a ledge) but have not yet moved.
	if (mobj->x == x && mobj->y == y && mobj->z == z)
		return;

	P_CheckPosition(mobj, x, y);
	mobj->UnlinkFromWorld ();

	mobj->x = x;
	mobj->y = y;
	mobj->z = z;
	mobj->floorz = tmfloorz;
	mobj->ceilingz = tmceilingz;
	mobj->LinkToWorld ();
}

//
// CL_SendUserInfo
//
void CL_SendUserInfo(void)
{
	userinfo_t *coninfo = &consoleplayer().userinfo;
	D_SetupUserInfo();

	MSG_WriteMarker	(&net_buffer, clc_userinfo);
	MSG_WriteString	(&net_buffer, coninfo->netname);
	MSG_WriteByte	(&net_buffer, coninfo->team); // [Toke]
	MSG_WriteLong	(&net_buffer, coninfo->gender);
	MSG_WriteLong	(&net_buffer, coninfo->color);
	MSG_WriteString	(&net_buffer, (char *)skins[coninfo->skin].name); // [Toke - skins]
	MSG_WriteLong	(&net_buffer, coninfo->aimdist);
	MSG_WriteByte	(&net_buffer, (char)coninfo->unlag);  // [SL] 2011-05-11
	MSG_WriteByte	(&net_buffer, (char)coninfo->update_rate);
	MSG_WriteByte	(&net_buffer, (char)coninfo->switchweapon);
	for (size_t i = 0; i < NUMWEAPONS; i++)
	{
		MSG_WriteByte (&net_buffer, (char)coninfo->weapon_prefs[i]);
	}
}


//
// CL_FindPlayer
//
player_t &CL_FindPlayer(size_t id)
{
	player_t *p = &idplayer(id);

	// Totally new player?
	if(!validplayer(*p))
	{
		if(players.size() >= MAXPLAYERS)
			return *p;

		players.push_back(player_s());

		p = &players.back();
		p->id = id;

		// repair mo after player pointers are reset
		for(size_t i = 0; i < players.size(); i++)
		{
			if(players[i].mo)
				players[i].mo->player = &players[i];
		}
	}

	return *p;
}

//
// CL_SetupUserInfo
//
void CL_SetupUserInfo(void)
{
	byte who = MSG_ReadByte();
	player_t *p = &CL_FindPlayer(who);

	strncpy(p->userinfo.netname, MSG_ReadString(), sizeof(p->userinfo.netname));

	p->userinfo.team	= (team_t)MSG_ReadByte();
	p->userinfo.gender	= (gender_t)MSG_ReadLong();
	p->userinfo.color	= MSG_ReadLong();
	p->userinfo.skin	= R_FindSkin(MSG_ReadString());

	p->GameTime			= MSG_ReadShort();

	R_BuildPlayerTranslation (p->id, p->userinfo.color);

	if(p->userinfo.gender >= NUMGENDER)
		p->userinfo.gender = GENDER_NEUTER;

	if (p->mo)
		p->mo->sprite = skins[p->userinfo.skin].sprite;

	extern bool st_firsttime;
	st_firsttime = true;
}


//
// CL_UpdateFrags
//
void CL_UpdateFrags(void)
{
	player_t &p = CL_FindPlayer(MSG_ReadByte());

	if(sv_gametype != GM_COOP)
		p.fragcount = MSG_ReadShort();
	else
		p.killcount = MSG_ReadShort();
	p.deathcount = MSG_ReadShort();
	p.points = MSG_ReadShort();
}

//
// [deathz0r] Receive team frags/captures
//
void CL_TeamPoints (void)
{
	for(size_t i = 0; i < NUMTEAMS; i++)
		TEAMpoints[i] = MSG_ReadShort();
}

//
// denis - fast netid lookup
//
AActor* CL_FindThingById(size_t id)
{
	netid_map_t::iterator i = actor_by_netid.find(id);

	if(i == actor_by_netid.end())
		return AActor::AActorPtr();
	else
		return i->second;
}

void CL_SetThingId(AActor *mo, size_t newnetid)
{
	mo->netid = newnetid;
	actor_by_netid[newnetid] = mo->ptr();
}

void CL_ClearID(size_t id)
{
    AActor *mo = CL_FindThingById(id);

	if(!mo)
		return;

	if(mo->player)
	{
		if(mo->player->mo == mo)
			mo->player->mo = AActor::AActorPtr();

		mo->player = NULL;
	}

	mo->Destroy();
}

//
// CL_MoveMobj
//
void CL_MoveMobj(void)
{
	AActor  *mo;
	int      netid;
	fixed_t  x, y, z;

	netid = MSG_ReadShort();
	mo = CL_FindThingById (netid);

	byte rndindex = MSG_ReadByte();
	x = MSG_ReadLong();
	y = MSG_ReadLong();
	z = MSG_ReadLong();

	if (!mo)
		return;

	CL_MoveThing (mo, x, y, z);
	mo->rndindex = rndindex;
}

//
// CL_DamageMobj
//
void CL_DamageMobj()
{
	AActor  *mo;
	int      netid, health, pain;

	netid = MSG_ReadShort();
	health = MSG_ReadShort();
	pain = MSG_ReadByte();

	mo = CL_FindThingById (netid);

	if (!mo)
		return;

	mo->health = health;

	if(pain < mo->info->painchance)
		P_SetMobjState(mo, mo->info->painstate);
}

int connecttimeout = 0;

//
// [denis] CL_RequestConnectInfo
// Do what a launcher does...
//
void CL_RequestConnectInfo(void)
{
	if (!serveraddr.ip[0])
		return;

	if(gamestate != GS_DOWNLOAD)
		gamestate = GS_CONNECTING;

	if(!connecttimeout)
	{
		connecttimeout = 140;

		Printf(PRINT_HIGH, "connecting to %s\n", NET_AdrToString(serveraddr));

		SZ_Clear(&net_buffer);
		MSG_WriteLong(&net_buffer, LAUNCHER_CHALLENGE);
		NET_SendPacket(net_buffer, serveraddr);
	}

	connecttimeout--;
}

//
// [denis] CL_PrepareConnect
// Process server info and switch to the right wads...
//
std::string missing_file, missing_hash;
bool CL_PrepareConnect(void)
{
	size_t i;
	DWORD server_token = MSG_ReadLong();
	server_host = MSG_ReadString();

	bool recv_teamplay_stats = 0;
	gameversiontosend = 0;

	byte playercount = MSG_ReadByte(); // players
	MSG_ReadByte(); // max_players

	std::string server_map = MSG_ReadString();
	byte server_wads = MSG_ReadByte();

	Printf(PRINT_HIGH, "\n");
	Printf(PRINT_HIGH, "> Server: %s\n", server_host.c_str());
	Printf(PRINT_HIGH, "> Map: %s\n", server_map.c_str());

	std::vector<std::string> wadnames(server_wads);
	for(i = 0; i < server_wads; i++)
		wadnames[i] = MSG_ReadString();

	MSG_ReadBool();							// deathmatch
	MSG_ReadByte();							// skill
	recv_teamplay_stats |= MSG_ReadBool();	// teamplay
	recv_teamplay_stats |= MSG_ReadBool();	// ctf

	for(i = 0; i < playercount; i++)
	{
		MSG_ReadString();
		MSG_ReadShort();
		MSG_ReadLong();
		MSG_ReadByte();
	}

	std::vector<std::string> wadhashes(server_wads);
	for(i = 0; i < server_wads; i++)
	{
		wadhashes[i] = MSG_ReadString();
		Printf(PRINT_HIGH, "> %s\n   %s\n", wadnames[i].c_str(), wadhashes[i].c_str());
	}

	MSG_ReadString();

	// Receive conditional teamplay information
	if (recv_teamplay_stats)
	{
		MSG_ReadLong();

		for(size_t i = 0; i < NUMTEAMS; i++)
		{
			bool enabled = MSG_ReadBool();

			if (enabled)
				MSG_ReadLong();
		}
	}

	version = MSG_ReadShort();

	Printf(PRINT_HIGH, "> Server protocol version: %i\n", version);

	if(version > VERSION)
		version = VERSION;
	if(version < 62)
		version = 62;
		
	/* GhostlyDeath -- Need the actual version info */
	if (version == 65)
	{
		size_t l;
		MSG_ReadString();
		
		for (l = 0; l < 3; l++)
			MSG_ReadShort();
		for (l = 0; l < 14; l++)
			MSG_ReadBool();
		for (l = 0; l < playercount; l++)
		{
			MSG_ReadShort();
			MSG_ReadShort();
			MSG_ReadShort();
		}
		
		MSG_ReadLong();
		MSG_ReadShort();
		
		for (l = 0; l < playercount; l++)
			MSG_ReadBool();
		
		MSG_ReadLong();
		MSG_ReadShort();

		gameversion = MSG_ReadLong();
		
		// GhostlyDeath -- Assume 40 for compatibility and fake it 
		if (((gameversion % 256) % 10) == -1)
		{
			gameversion = 40;
			gameversiontosend = 40;
		}

		Printf(PRINT_HIGH, "> Server Version %i.%i.%i\n", gameversion / 256, (gameversion % 256) / 10, (gameversion % 256) % 10);
	}

    Printf(PRINT_HIGH, "\n");

    // DEH/BEX Patch files
	std::vector<std::string> PatchFiles;
    size_t PatchCount = MSG_ReadByte();
    
    for (i = 0; i < PatchCount; ++i)
        PatchFiles.push_back(MSG_ReadString());
	
    // TODO: Allow deh/bex file downloads
	std::vector<size_t> missing_files = D_DoomWadReboot(wadnames, PatchFiles, wadhashes);

	if(!missing_files.empty())
	{
		// denis - download files
		missing_file = wadnames[missing_files[0]];
		missing_hash = wadhashes[missing_files[0]];

		if (netdemo.isPlaying())
		{
			// Playing a netdemo and unable to download from the server
			Printf(PRINT_HIGH, "Unable to find \"%s\".  Cannot download while playing a netdemo.\n", missing_file.c_str());
			CL_QuitNetGame();
			return false;
		}

		gamestate = GS_DOWNLOAD;
		Printf(PRINT_HIGH, "Will download \"%s\" from server\n", missing_file.c_str());
	}

	connecttimeout = 0;
	CL_TryToConnect(server_token);

	return true;
}

//
//  Connecting to a server...
//
extern std::string missing_file, missing_hash;
bool CL_Connect(void)
{
	players.clear();

	memset(packetseq, -1, sizeof(packetseq) );
	packetnum = 0;

	MSG_WriteMarker(&net_buffer, clc_ack);
	MSG_WriteLong(&net_buffer, 0);

	if(gamestate == GS_DOWNLOAD && missing_file.length())
	{
		// denis - do not download commercial wads
		if(W_IsIWAD(missing_file, missing_hash))
		{
			Printf(PRINT_HIGH, "This is a commercial wad and will not be downloaded.\n");
			CL_QuitNetGame();
			return false;
		}

		CL_RequestDownload(missing_file, missing_hash);
	}

	compressor.reset();

	connected = true;
    multiplayer = true;
    network_game = true;
	serverside = false;
	simulated_connection = netdemo.isPlaying();

	CL_Decompress(0);
	CL_ParseCommands();

	if (gameaction == ga_fullconsole) // Host_EndGame was called
		return false;

	D_SetupUserInfo();

	//raise the weapon
	if(validplayer(consoleplayer()))
		consoleplayer().psprites[ps_weapon].sy = 32*FRACUNIT+0x6000;

	noservermsgs = false;
	last_received = gametic;

	return true;
}


//
// CL_InitNetwork
//
void CL_InitNetwork (void)
{
    netgame = false;  // for old network code

    const char *v = Args.CheckValue ("-port");
    if (v)
    {
		localport = atoi (v);
		Printf (PRINT_HIGH, "using alternate port %i\n", localport);
    }
    else
		localport = CLIENTPORT;

    // set up a socket and net_message buffer
    InitNetCommon();

    SZ_Clear(&net_buffer);

    size_t ParamIndex = Args.CheckParm ("-connect");
    
    if (ParamIndex)
    {
		const char *ipaddress = Args.GetArg(ParamIndex + 1);

		if (ipaddress && ipaddress[0] != '-' && ipaddress[0] != '+')
		{
			NET_StringToAdr (ipaddress, &serveraddr);

			const char *passhash = Args.GetArg(ParamIndex + 2);

			if (passhash && passhash[0] != '-' && passhash[0] != '+')
			{
				connectpasshash = MD5SUM(passhash);            
			}

			if (!serveraddr.port)
				I_SetPort(serveraddr, SERVERPORT);

			lastconaddr = serveraddr;
			gamestate = GS_CONNECTING;
		}
    }

	G_SetDefaultTurbo ();

    connected = false;
}

void CL_TryToConnect(DWORD server_token)
{
	if (!serveraddr.ip[0])
		return;

	if (!connecttimeout)
	{
		connecttimeout = 140; // 140 tics = 4 seconds

		Printf(PRINT_HIGH, "challenging %s\n", NET_AdrToString(serveraddr));

		SZ_Clear(&net_buffer);
		MSG_WriteLong(&net_buffer, CHALLENGE); // send challenge
		MSG_WriteLong(&net_buffer, server_token); // confirm server token
		MSG_WriteShort(&net_buffer, version); // send client version

		if(gamestate == GS_DOWNLOAD)
			MSG_WriteByte(&net_buffer, 1); // send type of connection (play/spectate/rcon/download)
		else
			MSG_WriteByte(&net_buffer, 0); // send type of connection (play/spectate/rcon/download)
			
		// GhostlyDeath -- Send more version info
		if (gameversiontosend)
			MSG_WriteLong(&net_buffer, gameversiontosend);
		else
			MSG_WriteLong(&net_buffer, GAMEVER);

		CL_SendUserInfo(); // send userinfo

		MSG_WriteLong(&net_buffer, (int)rate);
        
        MSG_WriteString(&net_buffer, (char *)connectpasshash.c_str());            
        
		NET_SendPacket(net_buffer, serveraddr);
		SZ_Clear(&net_buffer);
	}

	connecttimeout--;
}

EXTERN_CVAR (show_messages)

//
// CL_Print
//
void CL_Print (void)
{
	byte level = MSG_ReadByte();
	const char *str = MSG_ReadString();

	Printf (level, "%s", str);
	
	if (show_messages)
	{
		if (level == PRINT_CHAT)
			S_Sound (CHAN_INTERFACE, gameinfo.chatSound, 1, ATTN_NONE);
		else if (level == PRINT_TEAMCHAT)
			S_Sound (CHAN_INTERFACE, "misc/teamchat", 1, ATTN_NONE);
	}
}

// Print a message in the middle of the screen
void CL_MidPrint (void)
{
    const char *str = MSG_ReadString();
    int msgtime = MSG_ReadShort();
    
    C_MidPrint(str,NULL,msgtime);
}


void CL_UpdatePlayer()
{
	byte who = MSG_ReadByte();
	player_t *p = &idplayer(who);

	// [SL] 2011-11-07 - Read and ignore this for now - it was used with
	// the previous prediction code (sv_gametic)
	MSG_ReadLong();
	
	fixed_t x = MSG_ReadLong();
	fixed_t y = MSG_ReadLong();
	fixed_t z = MSG_ReadLong();
	angle_t angle = MSG_ReadLong();
	byte frame = MSG_ReadByte();
	fixed_t momx = MSG_ReadLong();
	fixed_t momy = MSG_ReadLong();
	fixed_t momz = MSG_ReadLong();

	int invisibility = MSG_ReadLong();

	if	(!validplayer(*p) || !p->mo)
	{
		return;
	}

	// Mark the gametic this update arrived in for prediction code
	p->tic = gametic;
	
	// GhostlyDeath -- Servers will never send updates on spectators
	if (p->spectator && (p != &consoleplayer()))
		p->spectator = 0;

	p->mo->angle = angle;
	CL_MoveThing(p->mo, x, y, z);
	p->mo->momx = momx;
	p->mo->momy = momy;
	p->mo->momz = momz;

	p->real_origin[0] = x;
	p->real_origin[1] = y;
	p->real_origin[2] = z;

	p->real_velocity[0] = momx;
	p->real_velocity[1] = momy;
	p->real_velocity[2] = momz;

    // [Russell] - hack, read and set invisibility flag
    p->powers[pw_invisibility] = invisibility;
    if (p->powers[pw_invisibility])
    {
        p->mo->flags |= MF_SHADOW;
    }
    else
    {
        p->mo->flags &= ~MF_SHADOW;
    }

	p->mo->frame = frame;

	// This is a very bright frame. Looks cool :)
	if (p->mo->frame == PLAYER_FULLBRIGHTFRAME)
		p->mo->frame = 32773;

	// denis - fixme - security
	if(!p->mo->sprite || (p->mo->frame&FF_FRAMEMASK) >= sprites[p->mo->sprite].numframes)
		return;

	p->last_received = gametic;
	last_player_update = gametic;
}

ticcmd_t localcmds[MAXSAVETICS];

void CL_SaveCmd(void)
{
	memcpy (&localcmds[gametic%MAXSAVETICS], &consoleplayer().cmd, sizeof(ticcmd_t));
}

//
// CL_UpdateLocalPlayer
//
void CL_UpdateLocalPlayer(void)
{
	player_t &p = consoleplayer();

	// The server has processed the ticcmd that the local client sent
	// during the the tic referenced below
	p.tic = MSG_ReadLong();

	p.real_origin[0] = MSG_ReadLong();
	p.real_origin[1] = MSG_ReadLong();
	p.real_origin[2] = MSG_ReadLong();

	p.real_velocity[0] = MSG_ReadLong();
	p.real_velocity[1] = MSG_ReadLong();
	p.real_velocity[2] = MSG_ReadLong();

	byte waterlevel = MSG_ReadByte();
	if (p.mo)
		p.mo->waterlevel = waterlevel;

//	real_plats.Clear();
}


//
// CL_SaveSvGametic
// 
// Receives the server's gametic at the time the packet was sent.  It will be
// sent back to the server with the next cmd.
//
// [SL] 2011-05-11
void CL_SaveSvGametic(void)
{
	last_svgametic = MSG_ReadByte();
	#ifdef _UNLAG_DEBUG_
	DPrintf("Unlag (%03d): client-tic %d, received svgametic\n", last_svgametic, gametic);
	#endif	// _UNLAG_DEBUG_
}    

//
// CL_SendPingReply
//
// Replies to a server's ping request
//
// [SL] 2011-05-11 - Changed from CL_ResendSvGametic to CL_SendPingReply
// for clarity since it sends timestamps, not gametics.
//
void CL_SendPingReply(void)
{
	int svtimestamp = MSG_ReadLong();
	MSG_WriteMarker (&net_buffer, clc_pingreply);
	MSG_WriteLong (&net_buffer, svtimestamp);
}

//
// CL_UpdatePing
// Update ping value
//
void CL_UpdatePing(void)
{
	player_t &p = idplayer(MSG_ReadByte());
	p.ping = MSG_ReadLong();
}


//
// CL_UpdateTimeLeft
// Changes the value of level.timeleft
//
void CL_UpdateTimeLeft(void)
{
	level.timeleft = MSG_ReadShort() * TICRATE;	// convert from seconds to tics
}

//
// CL_UpdateIntTimeLeft
// Changes the value of level.inttimeleft
//
void CL_UpdateIntTimeLeft(void)
{
	level.inttimeleft = MSG_ReadShort();	// convert from seconds to tics
}


//
// CL_SpawnMobj
//
void CL_SpawnMobj()
{
	fixed_t  x = 0, y = 0, z = 0;
	AActor  *mo;

	x = MSG_ReadLong();
	y = MSG_ReadLong();
	z = MSG_ReadLong();
	angle_t angle = MSG_ReadLong();

	unsigned short type = MSG_ReadShort();
	unsigned short netid = MSG_ReadShort();
	byte rndindex = MSG_ReadByte();
	SWORD state = MSG_ReadShort();
	byte arg0 = (type == MT_FOUNTAIN) ? MSG_ReadByte() : 0;

	if(type >= NUMMOBJTYPES)
		return;

	CL_ClearID(netid);

	mo = new AActor (x, y, z, (mobjtype_t)type);

	// denis - puff hack
	if(mo->type == MT_PUFF)
	{
		mo->momz = FRACUNIT;
		mo->tics -= M_Random () & 3;
		if (mo->tics < 1)
			mo->tics = 1;
	}

	mo->angle = angle;
	CL_SetThingId(mo, netid);
	mo->rndindex = rndindex;

	if (state < NUMSTATES)
		P_SetMobjState(mo, (statenum_t)state);

	if(mo->flags & MF_MISSILE)
	{
		AActor *target = CL_FindThingById(MSG_ReadShort());
		if(target)
			mo->target = target->ptr();
		CL_SetMobjSpeedAndAngle();
	}

    if (mo->flags & MF_COUNTKILL)
		level.total_monsters++;

	if (connected && (mo->flags & MF_MISSILE ) && mo->info->seesound)
		S_Sound (mo, CHAN_VOICE, mo->info->seesound, 1, ATTN_NORM);

	if (mo->type == MT_IFOG)
		S_Sound (mo, CHAN_VOICE, "misc/spawn", 1, ATTN_IDLE);

	if (mo->type == MT_TFOG)
	{
		if (level.time)	// don't play sound on first tic of the level
			S_Sound (mo, CHAN_VOICE, "misc/teleport", 1, ATTN_NORM);
	}

	mo->effects = arg0 << FX_FOUNTAINSHIFT; 
	mo->args[0] = arg0;
}

//
// CL_Corpse
// Called after killed thing is created.
//
void CL_Corpse(void)
{
	AActor *mo = CL_FindThingById(MSG_ReadShort());
	int frame = MSG_ReadByte();
	int tics = MSG_ReadByte();
	
	if(tics == 0xFF)
		tics = -1;

	// already spawned as gibs?
	if (!mo || mo->state - states == S_GIBS)
		return;

	if((frame&FF_FRAMEMASK) >= sprites[mo->sprite].numframes)
		return;

	mo->frame = frame;
	mo->tics = tics;

	// from P_KillMobj
	mo->flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SKULLFLY);
	mo->flags |= MF_CORPSE|MF_DROPOFF;
	mo->height >>= 2;
	mo->flags &= ~MF_SOLID;

	if (mo->player)
		mo->player->playerstate = PST_DEAD;
		
    if (mo->flags & MF_COUNTKILL)
		level.killed_monsters++;
}

//
// CL_TouchSpecialThing
//
void CL_TouchSpecialThing (void)
{
	AActor *mo = CL_FindThingById(MSG_ReadShort());

	if(!consoleplayer().mo || !mo)
		return;

	P_GiveSpecial(&consoleplayer(), mo);
}


extern fixed_t cl_viewheight[MAXSAVETICS];
extern fixed_t cl_deltaviewheight[MAXSAVETICS];

//
// CL_SpawnPlayer
//
void CL_SpawnPlayer()
{
	byte		playernum;
	player_t	*p;
	AActor		*mobj;
	fixed_t		x = 0, y = 0, z = 0;
	unsigned short netid;
	angle_t		angle = 0;
	int			i;

	playernum = MSG_ReadByte();
	netid = MSG_ReadShort();
	p = &CL_FindPlayer(playernum);

	angle = MSG_ReadLong();
	x = MSG_ReadLong();
	y = MSG_ReadLong();
	z = MSG_ReadLong();
		
	// GhostlyDeath -- reset prediction
	p->real_origin[0] = x;
	p->real_origin[1] = y;
	p->real_origin[2] = z;

	p->real_velocity[0] = 0;
	p->real_velocity[1] = 0;
	p->real_velocity[2] = 0;

	CL_ClearID(netid);

	// first disassociate the corpse
	if (p->mo)
	{
		p->mo->player = NULL;
		p->mo->health = 0;
	}

	G_PlayerReborn (*p);

	mobj = new AActor (x, y, z, MT_PLAYER);

	// set color translations for player sprites
	mobj->translation = translationtables + 256*playernum;
	mobj->angle = angle;
	mobj->pitch = mobj->roll = 0;
	mobj->player = p;
	mobj->health = p->health;
	CL_SetThingId(mobj, netid);

	// [RH] Set player sprite based on skin
	if(p->userinfo.skin >= numskins)
		p->userinfo.skin = 0;

	mobj->sprite = skins[p->userinfo.skin].sprite;

	p->mo = p->camera = mobj->ptr();
	p->fov = 90.0f;
	p->playerstate = PST_LIVE;
	p->refire = 0;
	p->damagecount = 0;
	p->bonuscount = 0;
	p->extralight = 0;
	p->fixedcolormap = 0;

	p->xviewshift = 0;
	p->viewheight = VIEWHEIGHT;
	for (i=0; i<MAXSAVETICS; i++)
	{
		cl_viewheight[i] = VIEWHEIGHT;
		cl_deltaviewheight[i] = 0;
	}

	p->attacker = AActor::AActorPtr();
	p->viewz = z + VIEWHEIGHT;

	// spawn a teleport fog
	// tfog = new AActor (x, y, z, MT_TFOG);

	// setup gun psprite
	P_SetupPsprites (p);

	// give all cards in death match mode
	if(sv_gametype != GM_COOP)
		for (i = 0; i < NUMCARDS; i++)
			p->cards[i] = true;

	// denis - if this concerns the local player, restart the status bar
	if(p->id == consoleplayer_id)
		ST_Start ();
}

//
// CL_PlayerInfo
// denis - your personal arsenal, as supplied by the server
//
void CL_PlayerInfo(void)
{
	player_t *p = &consoleplayer();

	for(size_t j = 0; j < NUMWEAPONS; j++)
		p->weaponowned[j] = MSG_ReadBool();

	for(size_t j = 0; j < NUMAMMO; j++)
	{
		p->maxammo[j] = MSG_ReadShort ();
		p->ammo[j] = MSG_ReadShort ();
	}

	p->health = MSG_ReadByte ();
	p->armorpoints = MSG_ReadByte ();
	p->armortype = MSG_ReadByte ();

	weapontype_t newweapon = static_cast<weapontype_t>(MSG_ReadByte());
	if (newweapon >= NUMWEAPONS)	// bad weapon number, choose something else
		newweapon = wp_fist;

	if (newweapon != p->readyweapon)
		p->pendingweapon = newweapon;
	
	p->backpack = MSG_ReadBool();

	for (size_t j = 0; j < NUMCARDS; j++)
		p->cards[j] = MSG_ReadBool();
}

//
// CL_SetMobjSpeedAndAngle
//
void CL_SetMobjSpeedAndAngle(void)
{
	AActor *mo;
	int     netid;

	netid = MSG_ReadShort();
	mo = CL_FindThingById(netid);

	if (!mo)
	{
		for (int i=0; i<4; i++)
			MSG_ReadLong();
		return;
	}

	mo->angle = MSG_ReadLong();
	mo->momx = MSG_ReadLong();
	mo->momy = MSG_ReadLong();
	mo->momz = MSG_ReadLong();
}

//
// CL_ExplodeMissile
//
void CL_ExplodeMissile(void)
{
    AActor *mo;
	int     netid;

	netid = MSG_ReadShort();
	mo = CL_FindThingById(netid);

	if (!mo)
		return;

	P_ExplodeMissile(mo);
}


//
// CL_UpdateMobjInfo
//
void CL_UpdateMobjInfo(void)
{
	int netid = MSG_ReadShort();
	int flags = MSG_ReadLong();
	//int flags2 = MSG_ReadLong();

	AActor *mo = CL_FindThingById(netid);

	if (!mo)
		return;

	mo->flags = flags;
	//mo->flags2 = flags2;
}


//
// CL_RemoveMobj
//
void CL_RemoveMobj(void)
{
	CL_ClearID(MSG_ReadShort());
}


//
// CL_DamagePlayer
//
void CL_DamagePlayer(void)
{
	player_t *p;
	int       health;
	int       damage;

	p = &idplayer(MSG_ReadByte());

	p->armorpoints = MSG_ReadByte();
	health         = MSG_ReadShort();

	if(!p->mo)
		return;

	damage = p->health - health;
	p->mo->health = p->health = health;

	if (p->health < 0)
		p->health = 0;

	if (damage < 0)  // can't be!
		return;

	if (damage > 0) {
		p->damagecount += damage;

		if (p->damagecount > 100)
			p->damagecount = 100;

		if(p->mo->info->painstate)
			P_SetMobjState(p->mo, p->mo->info->painstate);
	}
}

extern int MeansOfDeath;

//
// CL_KillMobj
//
void CL_KillMobj(void)
{
 	AActor *source = CL_FindThingById (MSG_ReadShort() );
	AActor *target = CL_FindThingById (MSG_ReadShort() );
	AActor *inflictor = CL_FindThingById (MSG_ReadShort() );
	int health = MSG_ReadShort();

	MeansOfDeath = MSG_ReadLong();
	
	bool joinkill = MSG_ReadByte();

	if (!target)
		return;

	target->health = health;

    if (!serverside && target->flags & MF_COUNTKILL)
		level.killed_monsters++;

	P_KillMobj (source, target, inflictor, joinkill);	
}


///////////////////////////////////////////////////////////
///// CL_Fire* called when someone uses a weapon  /////////
///////////////////////////////////////////////////////////

//
// CL_FirePistol
//
void CL_FirePistol(void)
{
	player_t &p = idplayer(MSG_ReadByte());

	if(!validplayer(p))
		return;

	if(!p.mo)
		return;

	if (&p != &consoleplayer())
		S_Sound (p.mo, CHAN_WEAPON, "weapons/pistol", 1, ATTN_NORM);
}

//
// CL_FireShotgun
//
void CL_FireShotgun(void)
{
	player_t &p = idplayer(MSG_ReadByte());

	if(!validplayer(p))
		return;

	if(!p.mo)
		return;

	if (&p != &consoleplayer())
		S_Sound (p.mo, CHAN_WEAPON,  "weapons/shotgf", 1, ATTN_NORM);
}

//
// CL_FireSSG
//
void CL_FireSSG(void)
{
	player_t &p = idplayer(MSG_ReadByte());

	if(!validplayer(p))
		return;

	if(!p.mo)
		return;

	if (&p != &consoleplayer())
		S_Sound (p.mo, CHAN_WEAPON, "weapons/sshotf", 1, ATTN_NORM);
}


//
// CL_FireChainGun
//
void CL_FireChainGun(void)
{
	player_t &p = idplayer(MSG_ReadByte());

	if(!validplayer(p))
		return;

	if(!p.mo)
		return;

	if (&p != &consoleplayer())
		S_Sound (p.mo, CHAN_WEAPON, "weapons/chngun", 1, ATTN_NORM);
}

/////////////////////////////////////////////////////////
/*
void CL_ChangeWeapon (void)
{
player_t* player = &players[consoleplayer];

	player->pendingweapon = (weapontype_t)MSG_ReadByte();

	 // Now set appropriate weapon overlay.
	 P_SetPsprite (player,
	 ps_weapon,
	 weaponinfo[player->readyweapon].downstate);
	 }
*/

//
// CL_ChangeWeapon
// [ML] From Zdaemon .99
//
void CL_ChangeWeapon (void)
{
	player_t *player = &consoleplayer();
	weapontype_t newweapon = (weapontype_t)MSG_ReadByte();

	// ensure that the client has the weapon
	player->weaponowned[newweapon] = true;

	// [SL] 2011-09-22 - Only change the weapon if the client doesn't already
	// have that weapon up.
	if (player->readyweapon != newweapon)
		player->pendingweapon = newweapon;
}



//
// CL_Sound
//
void CL_Sound(void)
{
	int netid = MSG_ReadShort();
	int x = MSG_ReadLong();
	int y = MSG_ReadLong();
	byte channel = MSG_ReadByte();
	byte sfx_id = MSG_ReadByte();
	byte attenuation = MSG_ReadByte();
	byte vol = MSG_ReadByte();

	AActor *mo = CL_FindThingById (netid);

	float volume = vol/(float)255;

	if(volume < 0.25f)
	{
		// so far away that it becomes directionless
		AActor *mo = consoleplayer().mo;

		if(mo)
			S_SoundID (mo->x, mo->y, channel, sfx_id, volume, attenuation);

		return;
	}

	if(mo)
		S_SoundID (mo, channel, sfx_id, volume, attenuation); // play at thing location
	else
		S_SoundID (x, y, channel, sfx_id, volume, attenuation); // play at approximate thing location
}

void CL_SoundOrigin(void)
{
	int x = MSG_ReadLong();
	int y = MSG_ReadLong();
	byte channel = MSG_ReadByte();
	byte sfx_id = MSG_ReadByte();
	byte attenuation = MSG_ReadByte();
	byte vol = MSG_ReadByte();

	float volume = vol/(float)255;

	AActor *mo = consoleplayer().mo;

	if(!mo)
		return;

	if(volume < 0.25f)
	{
		x = mo->x;
		y = mo->y;
		S_SoundID (x, y, channel, sfx_id, volume, attenuation);
		return;
	}

	S_SoundID (x, y, channel, sfx_id, volume, attenuation);
}

//
// CL_UpdateSector
// Updates floorheight and ceilingheight of a sector.
//
void CL_UpdateSector(void)
{
	unsigned short s = (unsigned short)MSG_ReadShort();
	unsigned short fh = MSG_ReadShort();
	unsigned short ch = MSG_ReadShort();

	unsigned short fp = MSG_ReadShort();
	unsigned short cp = MSG_ReadShort();

	if(!sectors || s >= numsectors)
		return;

	sector_t *sec = &sectors[s];
	sec->floorheight = fh << FRACBITS;
	sec->ceilingheight = ch << FRACBITS;

	if(fp >= numflats)
		fp = numflats;

	sec->floorpic = fp;

	if(cp >= numflats)
		cp = numflats;

	sec->ceilingpic = cp;
	sec->moveable = true;

	P_ChangeSector (sec, false);
}

//
// CL_UpdateMovingSector
// Updates floorheight and ceilingheight of a sector.
//
void CL_UpdateMovingSector(void)
{
	int tic = MSG_ReadLong();
	unsigned short s = (unsigned short)MSG_ReadShort();
    fixed_t fh = MSG_ReadLong(); // floor height
    fixed_t ch = MSG_ReadLong(); // ceiling height
    byte Type = MSG_ReadByte();

    // Replaces the data in the corresponding struct
    // 0 = floors, 1 = ceilings, 2 = elevators/pillars 
    byte ReplaceType;

	plat_pred_t pred;
	
	memset(&pred, 0, sizeof(pred));

	pred.secnum = s;
	pred.tic = tic;
	pred.floorheight = fh;
	pred.ceilingheight = ch;

	switch(Type)
	{
        // Floors
        case 0:
        {
            pred.Floor.m_Type = (DFloor::EFloor)MSG_ReadLong();
            pred.Floor.m_Crush = MSG_ReadBool();
            pred.Floor.m_Direction = MSG_ReadLong();
            pred.Floor.m_NewSpecial = MSG_ReadShort();
            pred.Floor.m_Texture = MSG_ReadShort();
            pred.Floor.m_FloorDestHeight = MSG_ReadLong();
            pred.Floor.m_Speed = MSG_ReadLong();
            pred.Floor.m_ResetCount = MSG_ReadLong();
            pred.Floor.m_OrgHeight = MSG_ReadLong();
            pred.Floor.m_Delay = MSG_ReadLong();
            pred.Floor.m_PauseTime = MSG_ReadLong();
            pred.Floor.m_StepTime = MSG_ReadLong(); 
            pred.Floor.m_PerStepTime = MSG_ReadLong();

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->floordata)
                sec->floordata = new DFloor(sec);

            ReplaceType = 0;
        }
        break;

        // Platforms
	    case 1:
	    {
            pred.Floor.m_Speed = MSG_ReadLong();
            pred.Floor.m_Low = MSG_ReadLong();
            pred.Floor.m_High = MSG_ReadLong();
            pred.Floor.m_Wait = MSG_ReadLong();
            pred.Floor.m_Count = MSG_ReadLong();
            pred.Floor.m_Status = MSG_ReadLong();
            pred.Floor.m_OldStatus = MSG_ReadLong();
            pred.Floor.m_Crush = MSG_ReadBool();
            pred.Floor.m_Tag = MSG_ReadLong();
            pred.Floor.m_Type = MSG_ReadLong();

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->floordata)
                sec->floordata = new DPlat(sec);

            ReplaceType = 0;
	    }
	    break;

        // Ceilings
        case 2:
        {
            pred.Ceiling.m_Type = (DCeiling::ECeiling)MSG_ReadLong();
            pred.Ceiling.m_BottomHeight = MSG_ReadLong();
            pred.Ceiling.m_TopHeight = MSG_ReadLong();
            pred.Ceiling.m_Speed = MSG_ReadLong();
            pred.Ceiling.m_Speed1 = MSG_ReadLong();
            pred.Ceiling.m_Speed2 = MSG_ReadLong();
            pred.Ceiling.m_Crush = MSG_ReadBool();
            pred.Ceiling.m_Silent = MSG_ReadLong();
            pred.Ceiling.m_Direction = MSG_ReadLong();
            pred.Ceiling.m_Texture = MSG_ReadLong();
            pred.Ceiling.m_NewSpecial = MSG_ReadLong();
            pred.Ceiling.m_Tag = MSG_ReadLong();
            pred.Ceiling.m_OldDirection = MSG_ReadLong();

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->ceilingdata)
                sec->ceilingdata = new DCeiling(sec);

            ReplaceType = 1;
        }
        break;

        // Doors
        case 3:
        {
            int LineIndex;

            pred.Ceiling.m_Type = (DDoor::EVlDoor)MSG_ReadLong();
            pred.Ceiling.m_TopHeight = MSG_ReadLong();
            pred.Ceiling.m_Speed = MSG_ReadLong();
            pred.Ceiling.m_Direction = MSG_ReadLong();
            pred.Ceiling.m_TopWait = MSG_ReadLong();
            pred.Ceiling.m_TopCountdown = MSG_ReadLong();
			pred.Ceiling.m_Status = MSG_ReadLong();
            LineIndex = MSG_ReadLong();

            if (!lines || LineIndex >= numlines || LineIndex < 0)
                return;

            pred.Ceiling.m_Line = &lines[LineIndex];

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->ceilingdata)
                sec->ceilingdata = new DDoor(sec);

            ReplaceType = 1;
        }
        break;

        // Elevators
        case 4:
        {
            pred.Both.m_Type = (DElevator::EElevator)MSG_ReadLong();
            pred.Both.m_Direction = MSG_ReadLong();
            pred.Both.m_FloorDestHeight = MSG_ReadLong();
            pred.Both.m_CeilingDestHeight = MSG_ReadLong();
            pred.Both.m_Speed = MSG_ReadLong();

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->ceilingdata && !sec->floordata)
                sec->ceilingdata = sec->floordata = new DElevator(sec);

            ReplaceType = 2;
        }
        break;

        // Pillars
        case 5:
        {
            pred.Both.m_Type = (DPillar::EPillar)MSG_ReadLong();
            pred.Both.m_FloorSpeed = MSG_ReadLong();
            pred.Both.m_CeilingSpeed = MSG_ReadLong();
            pred.Both.m_FloorTarget = MSG_ReadLong();
            pred.Both.m_CeilingTarget = MSG_ReadLong();
            pred.Both.m_Crush = MSG_ReadBool();

            if(!sectors || s >= numsectors)
                return;

            sector_t *sec = &sectors[s];

            if(!sec->ceilingdata && !sec->floordata)
                sec->ceilingdata = sec->floordata = new DPillar();

            ReplaceType = 2;
        }
        break;

	    default:
            return;
	}

    size_t i;

	for(i = 0; i < real_plats.Size(); i++)
	{
		if(real_plats[i].secnum == s)
		{
            real_plats[i].tic = pred.tic;

			if (ReplaceType == 0)
            {
                real_plats[i].floorheight = pred.floorheight;
                real_plats[i].Floor = pred.Floor;
            }
            else if (ReplaceType == 1)
            {
                real_plats[i].ceilingheight = pred.ceilingheight;
                real_plats[i].Ceiling = pred.Ceiling;
            }
            else
            {
                real_plats[i].floorheight = pred.floorheight;
                real_plats[i].ceilingheight = pred.ceilingheight;
                real_plats[i].Both = pred.Both;
            }

			break;
		}
	}

	if(i == real_plats.Size())
		real_plats.Push(pred);
}


//
// CL_CheckMissedPacket
//
void CL_CheckMissedPacket(void)
{
	int    sequence;
	short  size;

	sequence = MSG_ReadLong();
	size = MSG_ReadShort();

	for (int n=0; n<256; n++)
	{
		// skip a duplicated packet
		if (packetseq[n] == sequence)
		{
            MSG_ReadChunk(size);

			#ifdef _DEBUG
                Printf (PRINT_LOW, "warning: duplicate packet\n");
			#endif
			return;
		}
	}
}

// Decompress the packet sequence
// [Russell] - reason this was failing is because of huffman routines, so just
// use minilzo for now (cuts a packet size down by roughly 45%), huffman is the
// if 0'd sections
void CL_Decompress(int sequence)
{
	if(!MSG_BytesLeft() || MSG_NextByte() != svc_compressed)
		return;
	else
		MSG_ReadByte();

	byte method = MSG_ReadByte();

#if 0
	if(method & adaptive_mask)
		MSG_DecompressAdaptive(compressor.codec_for_received(method & adaptive_select_mask ? 1 : 0));
	else
	{
		// otherwise compressed packets can still contain codec updates
		compressor.codec_for_received(method & adaptive_select_mask ? 1 : 0);
	}
#endif

	if(method & minilzo_mask)
		MSG_DecompressMinilzo();
#if 0
	if(method & adaptive_record_mask)
		compressor.ack_sent(net_message.ptr(), MSG_BytesLeft());
#endif
}

//
// CL_ReadPacketHeader
//
void CL_ReadPacketHeader(void)
{
	unsigned int sequence = MSG_ReadLong();

	MSG_WriteMarker(&net_buffer, clc_ack);
	MSG_WriteLong(&net_buffer, sequence);

	CL_Decompress(sequence);

	packetseq[packetnum] = sequence;
	packetnum++;
}

void CL_GetServerSettings(void)
{
	cvar_t *var = NULL, *prev = NULL;
		
	while (MSG_ReadByte() != 2)
	{
		std::string CvarName = MSG_ReadString();
		std::string CvarValue = MSG_ReadString();
			
		var = cvar_t::FindCVar (CvarName.c_str(), &prev);
			
		// GhostlyDeath <June 19, 2008> -- Read CVAR or dump it               
		if (var)
		{
			if (var->flags() & CVAR_SERVERINFO)
                var->Set(CvarValue.c_str());
        }
        else
        {
            // [Russell] - create a new "temporary" cvar, CVAR_AUTO marks it
            // for cleanup on program termination
            var = new cvar_t (CvarName.c_str(), NULL, "", CVARTYPE_NONE,
                CVAR_SERVERINFO | CVAR_AUTO | CVAR_UNSETTABLE);
                                  
            var->Set(CvarValue.c_str());
        }
			
    }
    
	// Nes - update the skies in case sv_freelook is changed.
	R_InitSkyMap ();
}

//
// CL_FinishedFullUpdate
//
// Takes care of any business that needs to be done once the client has a full
// view of the game world.
//
void CL_FinishedFullUpdate()
{
	// Write the first map snapshot to a netdemo
	if (netdemo.isRecording())
		netdemo.writeMapChange();
}

//
// CL_SetMobjState
//
void CL_SetMobjState()
{
	AActor *mo = CL_FindThingById (MSG_ReadShort() );
	SWORD s = MSG_ReadShort();

	if (!mo || s >= NUMSTATES)
		return;

	P_SetMobjState (mo, (statenum_t)s);
}

// ---------------------------------------------------------------------------------------------------------
//	CL_ForceSetTeam
//	Allows server to force set a players team setting
// ---------------------------------------------------------------------------------------------------------

void CL_ForceSetTeam (void)
{
	unsigned int t = MSG_ReadShort ();

	if(t < NUMTEAMS || t == TEAM_NONE)
		consoleplayer().userinfo.team = (team_t)t;
}

//
// CL_Actor_Movedir
//
void CL_Actor_Movedir()
{
	AActor *actor = CL_FindThingById (MSG_ReadShort());
	BYTE movedir = MSG_ReadByte();
    SDWORD movecount = MSG_ReadLong();
    
	if (!actor || movedir >= 8)
		return;

	actor->movedir = movedir;
	actor->movecount = movecount;
}

//
// CL_Actor_Target
//
void CL_Actor_Target()
{
	AActor *actor = CL_FindThingById (MSG_ReadShort());
	AActor *target = CL_FindThingById (MSG_ReadShort());

	if (!actor || !target)
		return;

	actor->target = target->ptr();
}

//
// CL_Actor_Tracer
//
void CL_Actor_Tracer()
{
	AActor *actor = CL_FindThingById (MSG_ReadShort());
	AActor *tracer = CL_FindThingById (MSG_ReadShort());

	if (!actor || !tracer)
		return;

	actor->tracer = tracer->ptr();
}

//
// CL_MobjTranslation
//
void CL_MobjTranslation()
{
	AActor *mo = CL_FindThingById(MSG_ReadShort());
	byte table = MSG_ReadByte();

	mo->translation = translationtables + 256 * table;
}


//
// CL_Switch
// denis - switch state and timing
//
void CL_Switch()
{
	unsigned l = MSG_ReadLong();
	byte wastoggled = MSG_ReadByte();
	byte state = MSG_ReadByte();
	unsigned time = MSG_ReadLong();

	if (!lines || l >= (unsigned)numlines || state >= 3)
		return;

	if(!P_SetButtonInfo(&lines[l], state, time)) // denis - fixme - security
		if(wastoggled)
			P_ChangeSwitchTexture(&lines[l], lines[l].flags & ML_REPEAT_SPECIAL);  // denis - fixme - security

	if(wastoggled && !(lines[l].flags & ML_REPEAT_SPECIAL)) // non repeat special
		lines[l].special = 0;
}

void CL_ActivateLine(void)
{
	unsigned l = MSG_ReadLong();
	AActor *mo = CL_FindThingById(MSG_ReadShort());
	byte side = MSG_ReadByte();
	byte activationType = MSG_ReadByte();

	if (!lines || l >= (unsigned)numlines)
		return;

	//if(mo == consoleplayer().mo && activationType != 2)
		//return;

	switch (activationType)
	{
	case 0:
		P_CrossSpecialLine(l, side, mo, true);
		break;
	case 1:
		P_UseSpecialLine(mo, &lines[l], side, true);
		break;
	case 2:
		P_ShootSpecialLine(mo, &lines[l], true);
		break;
    case 3:
        P_PushSpecialLine(mo, &lines[l], side, true);
        break;
	}
}

void CL_ConsolePlayer(void)
{
	displayplayer_id = consoleplayer_id = MSG_ReadByte();
	digest = MSG_ReadString();
}

void CL_LoadMap(void)
{
	const char *mapname = MSG_ReadString ();

	bool splitnetdemo = netdemo.isRecording() && cl_splitnetdemos;
	if (splitnetdemo)
		netdemo.stopRecording();

	if(gamestate == GS_DOWNLOAD)
		return;

	if(gamestate == GS_LEVEL)
		G_ExitLevel (0, 0);

	G_InitNew (mapname);

	real_plats.Clear();

	CTF_CheckFlags(consoleplayer());

	gameaction = ga_nothing;

	// Add a netdemo snapshot at the start of this new map
	if (netdemo.isRecording())
		netdemo.writeMapChange();

	// Autorecord netdemo or continue recording in a new file
	if (!(netdemo.isPlaying() || netdemo.isRecording() || netdemo.isPaused()))
	{
		std::string filename;

		size_t param = Args.CheckParm("-netrecord");
		if (param && Args.GetArg(param + 1))
			filename = Args.GetArg(param + 1);

		if (splitnetdemo || cl_autorecord || param)
		{
			if (filename.empty())
				filename = CL_GenerateNetDemoFileName(default_netdemo_filename);
			else
				filename = CL_GenerateNetDemoFileName(filename);

			netdemo.startRecording(filename);
		}
	}
}


void CL_EndGame()
{
	Host_EndGame ("\nServer disconnected\n");
}

void CL_FullGame()
{
	Host_EndGame ("Server is full\n");
}

void CL_ExitLevel()
{
	if(gamestate != GS_DOWNLOAD) {
		gameaction = ga_completed;

		if (netdemo.isRecording())
			netdemo.writeIntermission();
	}
}

void CL_Noop()
{
	// Nothing to see here. Move along.
}

void CL_Clear()
{
	size_t left = MSG_BytesLeft();
	MSG_ReadChunk(left);
}

EXTERN_CVAR (st_scale)

void CL_Spectate()
{
	player_t &player = CL_FindPlayer(MSG_ReadByte());
	
	player.spectator = MSG_ReadByte();
	
	if (&player == &consoleplayer()) {
		st_scale.Callback (); // refresh status bar size
		if (player.spectator) {
			for (int i=0 ; i<NUMPSPRITES ; i++) // remove all weapon sprites
				(&player)->psprites[i].state = NULL;
			player.playerstate = PST_LIVE; // resurrect dead spectators
			// GhostlyDeath -- Sometimes if the player spectates while he is falling down he squats
			player.deltaviewheight = 1000 << FRACBITS;
		} else {
			displayplayer_id = consoleplayer_id; // get out of spynext
		}
	}
	
	// GhostlyDeath -- If the player matches our display player...
	if (&player == &displayplayer())
		displayplayer_id = consoleplayer_id;
}

// client source (once)
typedef void (*client_callback)();
typedef std::map<svc_t, client_callback> cmdmap;
cmdmap cmds;

//
// CL_AllowPackets
//
void CL_InitCommands(void)
{
	cmds[svc_abort]			= &CL_EndGame;
	cmds[svc_loadmap]			= &CL_LoadMap;
	cmds[svc_playerinfo]		= &CL_PlayerInfo;
	cmds[svc_consoleplayer]		= &CL_ConsolePlayer;
	cmds[svc_updatefrags]		= &CL_UpdateFrags;
	cmds[svc_moveplayer]		= &CL_UpdatePlayer;
	cmds[svc_updatelocalplayer]	= &CL_UpdateLocalPlayer;
	cmds[svc_userinfo]			= &CL_SetupUserInfo;
	cmds[svc_teampoints]		= &CL_TeamPoints;

	cmds[svc_updateping]		= &CL_UpdatePing;
	cmds[svc_spawnmobj]			= &CL_SpawnMobj;
	cmds[svc_mobjspeedangle]	= &CL_SetMobjSpeedAndAngle;
	cmds[svc_mobjinfo]			= &CL_UpdateMobjInfo;
	cmds[svc_explodemissile]	= &CL_ExplodeMissile;
	cmds[svc_removemobj]		= &CL_RemoveMobj;

	cmds[svc_killmobj]			= &CL_KillMobj;
	cmds[svc_movemobj]			= &CL_MoveMobj;
	cmds[svc_damagemobj]		= &CL_DamageMobj;
	cmds[svc_corpse]			= &CL_Corpse;
	cmds[svc_spawnplayer]		= &CL_SpawnPlayer;
//	cmds[svc_spawnhiddenplayer]	= &CL_SpawnHiddenPlayer;
	cmds[svc_damageplayer]		= &CL_DamagePlayer;
	cmds[svc_firepistol]		= &CL_FirePistol;

	cmds[svc_fireshotgun]		= &CL_FireShotgun;
	cmds[svc_firessg]			= &CL_FireSSG;
	cmds[svc_firechaingun]		= &CL_FireChainGun;
	cmds[svc_changeweapon]		= &CL_ChangeWeapon;
	cmds[svc_connectclient]		= &CL_ConnectClient;
	cmds[svc_disconnectclient]	= &CL_DisconnectClient;
	cmds[svc_activateline]		= &CL_ActivateLine;
	cmds[svc_sector]			= &CL_UpdateSector;
	cmds[svc_movingsector]		= &CL_UpdateMovingSector;
	cmds[svc_switch]			= &CL_Switch;
	cmds[svc_print]				= &CL_Print;
    cmds[svc_midprint]          = &CL_MidPrint;
    cmds[svc_pingrequest]       = &CL_SendPingReply;
	cmds[svc_svgametic]			= &CL_SaveSvGametic;
	cmds[svc_mobjtranslation]	= &CL_MobjTranslation;
	cmds[svc_timeleft]			= &CL_UpdateTimeLeft;
	cmds[svc_inttimeleft]		= &CL_UpdateIntTimeLeft;

	cmds[svc_startsound]		= &CL_Sound;
	cmds[svc_soundorigin]		= &CL_SoundOrigin;
	cmds[svc_mobjstate]			= &CL_SetMobjState;
	cmds[svc_actor_movedir]		= &CL_Actor_Movedir;
	cmds[svc_actor_target]		= &CL_Actor_Target;
	cmds[svc_actor_tracer]		= &CL_Actor_Tracer;
	cmds[svc_missedpacket]		= &CL_CheckMissedPacket;
	cmds[svc_forceteam]			= &CL_ForceSetTeam;

	cmds[svc_ctfevent]			= &CL_CTFEvent;
	cmds[svc_serversettings]	= &CL_GetServerSettings;
	cmds[svc_disconnect]		= &CL_EndGame;
	cmds[svc_full]				= &CL_FullGame;
	cmds[svc_reconnect]			= &CL_Reconnect;
	cmds[svc_exitlevel]			= &CL_ExitLevel;

	cmds[svc_wadinfo]			= &CL_DownloadStart;
	cmds[svc_wadchunk]			= &CL_Download;

	cmds[svc_challenge]			= &CL_Clear;
	cmds[svc_launcher_challenge]= &CL_Clear;
	
	cmds[svc_spectate]   		= &CL_Spectate;
	
	cmds[svc_touchspecial]      = &CL_TouchSpecialThing;

	cmds[svc_netdemocap]        = &CL_LocalDemoTic;
	cmds[svc_netdemostop]       = &CL_NetDemoStop;
	cmds[svc_fullupdatedone]	= &CL_FinishedFullUpdate;

	cmds[svc_vote_update] = &CL_VoteUpdate;
	cmds[svc_maplist] = &CL_Maplist;
	cmds[svc_maplist_update] = &CL_MaplistUpdate;
	cmds[svc_maplist_index] = &CL_MaplistIndex;
}

//
// CL_ParseCommands
//
void CL_ParseCommands(void)
{
	std::vector<svc_t>	history;
	svc_t				cmd = svc_abort;

	static bool once = true;
	if(once)CL_InitCommands();
	once = false;

	while(connected)
	{
		cmd = (svc_t)MSG_ReadByte();
		history.push_back(cmd);
		
		if(cmd == (svc_t)-1)
			break;

		cmdmap::iterator i = cmds.find(cmd);
		if(i == cmds.end())
		{
			CL_QuitNetGame();
			Printf(PRINT_HIGH, "CL_ParseCommands: Unknown server message %d following: \n", (int)cmd);

			for(size_t j = 0; j < history.size(); j++)
				Printf(PRINT_HIGH, "CL_ParseCommands: message #%d [%d %s]\n", j, history[j], svc_info[history[j]].getName());
			Printf(PRINT_HIGH, "\n");
			break;
		}

		i->second();

		if (net_message.overflowed)
		{
			CL_QuitNetGame();
			Printf(PRINT_HIGH, "CL_ParseCommands: Bad server message\n");
			Printf(PRINT_HIGH, "CL_ParseCommands: %d(%s) overflowed\n",
					   (int)cmd,
					   svc_info[cmd].getName());
			Printf(PRINT_HIGH, "CL_ParseCommands: It was command number %d in the packet\n",
                                           (int)history.back());
			for(size_t j = 0; j < history.size(); j++)
				Printf(PRINT_HIGH, "CL_ParseCommands: message #%d [%d %s]\n", j, history[j], svc_info[history[j]].getName());
		}
		
	}
}

extern int outrate;

extern int extrapolation_tics;

//
// CL_SendCmd
//
void CL_SendCmd(void)
{
	player_t *p = &consoleplayer();

	if (netdemo.isPlaying())	// we're not really connected to a server
		return;

	if (!p->mo || gametic < 1 )
		return;

	// denis - we know server won't accept two changes per tic, so we shouldn't
	//static int last_sent_tic = 0;
	//if(last_sent_tic == gametic)
	//	return;
	
	// GhostlyDeath -- If we are spectating, tell the server of our new position
	if (p->spectator)
	{
		MSG_WriteMarker(&net_buffer, clc_spectate);
		MSG_WriteByte(&net_buffer, 5);
		MSG_WriteLong(&net_buffer, p->mo->x);
		MSG_WriteLong(&net_buffer, p->mo->y);
		MSG_WriteLong(&net_buffer, p->mo->z);
	}
	// GhostlyDeath -- We just throw it all away down here since we need those buttons!

	ticcmd_t *prevcmd = &localcmds[(gametic-1) % MAXSAVETICS];
	ticcmd_t *curcmd  = &consoleplayer().cmd;

	MSG_WriteMarker(&net_buffer, clc_move);

	// Write current client-tic.  Server later sends this back to client
	// when sending svc_updatelocalplayer so the client knows which ticcmds
	// need to be used for client's positional prediction. 
    MSG_WriteLong(&net_buffer, gametic);
    
    // Send the most recent server-tic.  This indicates to the server which
    // update of player positions the client is basing his actions on.  Used
    // by unlagging calculations.

	// Take extrapolation into account
	MSG_WriteByte(&net_buffer, last_svgametic + extrapolation_tics);

    // send the previous cmds in the message, so if the last packet
    // was dropped, it can be recovered
	MSG_WriteByte(&net_buffer,	prevcmd->ucmd.buttons);
	MSG_WriteShort(&net_buffer,	p->mo->angle >> 16);
	MSG_WriteShort(&net_buffer,	p->mo->pitch >> 16);
	MSG_WriteShort(&net_buffer,	prevcmd->ucmd.forwardmove);
	MSG_WriteShort(&net_buffer,	prevcmd->ucmd.sidemove);
	MSG_WriteShort(&net_buffer, prevcmd->ucmd.upmove);
	MSG_WriteByte(&net_buffer,	prevcmd->ucmd.impulse);

    // send the current cmds in the message
	MSG_WriteByte(&net_buffer,	curcmd->ucmd.buttons);
	if (step_mode) 
		MSG_WriteShort(&net_buffer, curcmd->ucmd.yaw);
	else 
		MSG_WriteShort(&net_buffer, (p->mo->angle + (curcmd->ucmd.yaw << 16)) >> 16);
	MSG_WriteShort(&net_buffer,	(p->mo->pitch + (curcmd->ucmd.pitch << 16)) >> 16);
	MSG_WriteShort(&net_buffer,	curcmd->ucmd.forwardmove);
	MSG_WriteShort(&net_buffer,	curcmd->ucmd.sidemove);
	MSG_WriteShort(&net_buffer,	curcmd->ucmd.upmove);

	// [SL] 2011-11-20 - Player isn't requesting a weapon change
	// send player's weapon to server and server will correct it if wrong
	if (!curcmd->ucmd.impulse && !(curcmd->ucmd.buttons & BT_CHANGE))
	{
		if (p->pendingweapon != wp_nochange)
			MSG_WriteByte(&net_buffer, p->pendingweapon);
		else
			MSG_WriteByte(&net_buffer, p->readyweapon);
	}
	else
		MSG_WriteByte(&net_buffer, curcmd->ucmd.impulse);

#ifdef _UNLAG_DEBUG_
	if 	(player.size() == 2 && 
		(prevcmd->ucmd.buttons & BT_ATTACK || curcmd->ucmd.buttons & BT_ATTACK))
	{
		player_t *enemy;
			
		if (players[0].id != consoleplayer().id)
			enemy = &players[0];
		else
			enemy = &players[1];
			
		int x = enemy->mo->x >> FRACBITS;
		int y = enemy->mo->y >> FRACBITS;
		DPrintf("Unlag: Weapon fired with svgametic = %d, enemy position = (%d, %d)\n",
				last_svgametic + extrapolation_tics, x, y);
	}
#endif	// _UNLAG_DEBUG_
	
	NET_SendPacket(net_buffer, serveraddr);
	outrate += net_buffer.size();
    SZ_Clear(&net_buffer);
}

//
// CL_PlayerTimes
//
void CL_PlayerTimes (void)
{
	for (size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].ingame())
			players[i].GameTime++;
	}
}

//
//	CL_RunTics
//
void CL_RunTics (void)
{
	static char TicCount = 0;

	// Only do this once a second.
	if ( TicCount++ >= 35 )
	{
		CL_PlayerTimes ();
		TicCount = 0;
	}

	if (sv_gametype == GM_CTF)
		CTF_RunTics ();

	Maplist_Runtic();
}

void PickupMessage (AActor *toucher, const char *message)
{
	// Some maps have multiple items stacked on top of each other.
	// It looks odd to display pickup messages for all of them.
	static int lastmessagetic;
	static const char *lastmessage = NULL;

	if (toucher == consoleplayer().camera
		&& (lastmessagetic != gametic || lastmessage != message))
	{
		lastmessagetic = gametic;
		lastmessage = message;
		Printf (PRINT_LOW, "%s\n", message);
	}
}

//
// void WeaponPickupMessage (weapontype_t &Weapon)
//
// This is used for displaying weaponstay messages, it is inevitably a hack
// because weaponstay is a hack
void WeaponPickupMessage (AActor *toucher, weapontype_t &Weapon)
{
    switch (Weapon)
    {
        case wp_shotgun:
        {
            PickupMessage(toucher, GStrings(GOTSHOTGUN));
        }
        break;

        case wp_chaingun:
        {
            PickupMessage(toucher, GStrings(GOTCHAINGUN));
        }
        break;

        case wp_missile:
        {
            PickupMessage(toucher, GStrings(GOTLAUNCHER));
        }
        break;

        case wp_plasma:
        {
            PickupMessage(toucher, GStrings(GOTPLASMA));
        }
        break;

        case wp_bfg:
        {
            PickupMessage(toucher, GStrings(GOTBFG9000));
        }
        break;

        case wp_chainsaw:
        {
            PickupMessage(toucher, GStrings(GOTCHAINSAW));
        }
        break;

        case wp_supershotgun:
        {
            PickupMessage(toucher, GStrings(GOTSHOTGUN2));
        }
        break;
        
        default:
        break;
    }
}

void CL_LocalDemoTic()
{
	player_t* clientPlayer = &consoleplayer();
	fixed_t x, y, z;
	fixed_t momx, momy, momz;
	fixed_t pitch, viewheight, deltaviewheight;
	angle_t angle;
	int jumpTics, reactiontime;
	byte waterlevel;

	memset(&clientPlayer->cmd, 0, sizeof(ticcmd_t));	
	clientPlayer->cmd.ucmd.buttons = MSG_ReadByte();
	clientPlayer->cmd.ucmd.impulse = MSG_ReadByte();	
	clientPlayer->cmd.ucmd.yaw = MSG_ReadShort();
	clientPlayer->cmd.ucmd.forwardmove = MSG_ReadShort();
	clientPlayer->cmd.ucmd.sidemove = MSG_ReadShort();
	clientPlayer->cmd.ucmd.upmove = MSG_ReadShort();
	clientPlayer->cmd.ucmd.pitch = MSG_ReadShort();

	waterlevel = MSG_ReadByte();
	x = MSG_ReadLong();
	y = MSG_ReadLong();
	z = MSG_ReadLong();
	momx = MSG_ReadLong();
	momy = MSG_ReadLong();
	momz = MSG_ReadLong();
	angle = MSG_ReadLong();
	pitch = MSG_ReadLong();
	viewheight = MSG_ReadLong();
	deltaviewheight = MSG_ReadLong();
	jumpTics = MSG_ReadLong();
	reactiontime = MSG_ReadLong();
	clientPlayer->readyweapon = static_cast<weapontype_t>(MSG_ReadByte());
	clientPlayer->pendingweapon = static_cast<weapontype_t>(MSG_ReadByte());

	if(clientPlayer->mo)
	{
		clientPlayer->mo->x = x;
		clientPlayer->mo->y = y;
		clientPlayer->mo->z = z;
		clientPlayer->mo->momx = momx;
		clientPlayer->mo->momy = momy;
		clientPlayer->mo->momz = momz;
		clientPlayer->mo->angle = angle;
		clientPlayer->mo->pitch = pitch;
		clientPlayer->viewheight = viewheight;
		clientPlayer->deltaviewheight = deltaviewheight;
		clientPlayer->jumpTics = jumpTics;
		clientPlayer->mo->reactiontime = reactiontime;
		clientPlayer->mo->waterlevel = waterlevel;
	}

}

void OnChangedSwitchTexture (line_t *line, int useAgain) {}
void OnActivatedLine (line_t *line, AActor *mo, int side, int activationType) {}

VERSION_CONTROL (cl_main_cpp, "$Id$")
