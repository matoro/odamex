// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2006-2020 by The Odamex Team.
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
//     Defines needed by the WDL stats logger.
//
//-----------------------------------------------------------------------------

#include "m_wdlstats.h"

#include <string>
#include <vector>

#include "c_dispatch.h"
#include "g_warmup.h"

#define WDLSTATS_VERSION 5

extern Players players;

EXTERN_CVAR(sv_gametype)

// WDL Stats dir - if not empty, we are logging.
static std::string wdlstatdir;

// A single tracked player
struct WDLPlayer
{
	std::string netname;
	team_t team;
};

// WDL Players that we're keeping track of.
static std::vector<WDLPlayer> wdlplayers;

// A single event.
struct WDLEvent
{
	WDLEvents ev;
	std::string activator;
	std::string target;
	int gametic;
	fixed_t apos[3];
	fixed_t tpos[3];
	int arg0;
	int arg1;
	int arg2;
};

// Events that we're keeping track of.
static std::vector<WDLEvent> wdlevents;

// The starting gametic of the most recent log.
static int wdlbegintic;

// Returns true if a player is ingame.
// FIXME: Put this someplace global.
static bool PlayerInGame(const player_t* player)
{
	return (
		player->ingame() &&
		player->spectator == false
	);
}

// Returns true if a player is ingame and on a specific team
// FIXME: Put this someplace global.
static bool PlayerInTeam(const player_t* player, byte team)
{
	return (
		player->ingame() &&
		player->userinfo.team == team &&
		player->spectator == false
	);
}

// Returns the number of players on a team
// FIXME: Put this someplace global.
static int CountTeamPlayers(byte team)
{
	int count = 0;

	Players::const_iterator it = players.begin();
	for (; it != players.end(); ++it)
	{
		const player_t* player = &*it;
		if (PlayerInTeam(player, team))
			count += 1;
	}

	return count;
}

// Generate a log filename based on the current time.
static std::string GenerateLogFilename()
{
	time_t ti = time(NULL);
	struct tm *lt = localtime(&ti);

	char buffer[128];
	if (!strftime(buffer, ARRAY_LENGTH(buffer), "wdl_%Y.%m.%d.%H.%M.%S.log", lt))
		return "";

	return std::string(buffer, ARRAY_LENGTH(buffer));
}

static void WDLStatsHelp()
{
	Printf(PRINT_HIGH,
		"wdlstats - Starts logging WDL statistics to the given directory.  Unless "
		"you are running a WDL server, you probably are not interested in this.\n\n"
		"Usage:\n"
		"  ] wdlstats <DIRNAME>\n"
		"  Starts logging WDL statistics in the directory DIRNAME.\n");
}

BEGIN_COMMAND(wdlstats)
{
	if (argc < 2)
	{
		WDLStatsHelp();
		return;
	}

	// Setting the stats dir tells us that we intend to log.
	wdlstatdir = argv[1];
	Printf(
		PRINT_HIGH, "wdlstats: Enabled and will log to \"%s\".\n", wdlstatdir.c_str()
	);
}
END_COMMAND(wdlstats)

void M_StartWDLLog()
{
	if (::wdlstatdir.empty())
		return;

	// Ensure we're CTF.
	if (sv_gametype != 3)
	{
		Printf(
			PRINT_HIGH,
			"wdlstats: Not logging, incorrect gametype.\n"
		);
		return;
	}

	// Ensure that we're not in an invalid warmup state.
	Warmup::status_t wstatus = ::warmup.get_status();
	if (wstatus != Warmup::DISABLED && wstatus != Warmup::INGAME)
	{
		// [AM] This message can probably be deleted once we're sure the
		//      condition is appropriate.
		Printf(
			PRINT_HIGH,
			"wdlstats: Not logging, not ingame (yet).\n"
		);
		return;
	}

	// Ensure we're 3v3 or more.
	int blueplayers = CountTeamPlayers(TEAM_BLUE);
	int redplayers = CountTeamPlayers(TEAM_RED);
	if (blueplayers < 3 && redplayers < 3 && false)
	{
		Printf(
			PRINT_HIGH,
			"wdlstats: Not logging, too few players on a team (%d vs %d).\n",
			blueplayers, redplayers
		);
		return;
	}

	/// Tally up our ingame players.
	::wdlplayers.clear();
	Players::const_iterator pit = ::players.begin();
	for (; pit != ::players.end(); ++pit)
	{
		WDLPlayer wdlplayer = {
			(*pit).userinfo.netname,
			(*pit).userinfo.team,
		};
		::wdlplayers.push_back(wdlplayer);
	}

	// Start with a fresh slate of events.
	::wdlevents.clear();

	// Set our starting tic.
	::wdlbegintic = ::gametic;

	Printf(PRINT_HIGH, "wdlstats: Log started...\n");
}

/**
 * Log a WDL event.
 * 
 * The particulars of what you pass to this needs to be checked against the document.
 */
void M_LogWDLEvent(
	WDLEvents event, player_t* activator, player_t* target,
	int arg0, int arg1, int arg2
)
{
	if (::wdlstatdir.empty())
		return;

	if (target != NULL)
	{
		// Event has a target.
		WDLEvent ev = {
			event, activator->userinfo.netname, target->userinfo.netname, ::gametic,
			{ activator->mo->x, activator->mo->y, activator->mo->z },
			{ target->mo->x, target->mo->y, target->mo->z },
			arg0, arg1, arg2
		};
		::wdlevents.push_back(ev);
	}
	else
	{
		// Event does not have a target.
		WDLEvent ev = {
			event, activator->userinfo.netname, "", ::gametic,
			{ activator->mo->x, activator->mo->y, activator->mo->z },
			{ 0, 0, 0 },
			arg0, arg1, arg2
		};
		::wdlevents.push_back(ev);
	}
}

void M_CommitWDLLog()
{
	if (::wdlstatdir.empty())
		return;

	std::string filename = GenerateLogFilename();

	Printf(PRINT_HIGH, "wdlstats: Log saved as \"%s\".\n", filename.c_str());
}
