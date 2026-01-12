//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//	Archiving: SaveGame I/O.
//	Thinker, Ticker.
//


#include "z_zone.h"
#include "p_local.h"

#include "doomstat.h"


int	leveltime;

//
// THINKERS
// All thinkers should be allocated by Z_Malloc
// so they can be operated on uniformly.
// The actual structures will vary in size,
// but the first element must be thinker_t.
//



// Both the head and tail of the thinker list.
thinker_t	thinkercap;


//
// P_InitThinkers
//
void P_InitThinkers (void)
{
    thinkercap.prev = thinkercap.next  = &thinkercap;
}




//
// P_AddThinker
// Adds a new thinker at the end of the list.
//
void P_AddThinker (thinker_t* thinker)
{
    thinkercap.prev->next = thinker;
    thinker->next = &thinkercap;
    thinker->prev = thinkercap.prev;
    thinkercap.prev = thinker;
}



//
// P_RemoveThinker
// Deallocation is lazy -- it will not actually be freed
// until its thinking turn comes up.
//
void P_RemoveThinker (thinker_t* thinker)
{
  // FIXME: NOP.
  thinker->function.acv = (actionf_v)(-1);
}



//
// P_AllocateThinker
// Allocates memory and adds a new thinker at the end of the list.
//
void P_AllocateThinker (thinker_t*	thinker)
{
}



//
// P_RunThinkers
//
// PAGER OPTIMIZATION: Prefetch next thinker(s) while processing current one.
//
// When THINKER_PREFETCH_ENABLED is defined, we prefetch the next thinkers
// in the linked list to hide memory latency.
//
// The thinker list is a linked list scattered in heap memory. Each ->next
// dereference is a potential cache miss (~100+ cycles on MIPS 24KEc).
// By prefetching 2 thinkers ahead, we hide memory latency.
//
// Prefetch strategy:
// - Prefetch next->next to hide L1 miss latency
// - Each thinker is at least sizeof(thinker_t) but usually mobj_t (~200 bytes)
// - MIPS 24KEc has 32KB L1 D-cache with 32-byte lines
//
// Source: Cache optimization analysis (perf/CACHE_OPTIMIZATION.md)
// Expected gain: 5-10% game logic reduction
//
// Compile with: -DTHINKER_PREFETCH_ENABLED
//
void P_RunThinkers (void)
{
    thinker_t*	currentthinker;
#ifdef THINKER_PREFETCH_ENABLED
    thinker_t*	nextthinker;
#endif

    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
#ifdef THINKER_PREFETCH_ENABLED
	// Save next pointer before potential Z_Free
	nextthinker = currentthinker->next;
	
	// Prefetch 2 thinkers ahead to hide memory latency
	// Level 0 = no temporal locality (won't be reused soon)
	if (nextthinker != &thinkercap) {
	    __builtin_prefetch(nextthinker, 0, 0);
	    if (nextthinker->next != &thinkercap) {
		__builtin_prefetch(nextthinker->next, 0, 0);
	    }
	}
#endif

	if ( currentthinker->function.acv == (actionf_v)(-1) )
	{
	    // time to remove it
	    currentthinker->next->prev = currentthinker->prev;
	    currentthinker->prev->next = currentthinker->next;
	    Z_Free (currentthinker);
	}
	else
	{
	    if (currentthinker->function.acp1)
		currentthinker->function.acp1 (currentthinker);
	}
#ifdef THINKER_PREFETCH_ENABLED
	currentthinker = nextthinker;
#else
	currentthinker = currentthinker->next;
#endif
    }
}



//
// P_Ticker
//

void P_Ticker (void)
{
    int		i;
    
    // run the tic
    if (paused)
	return;
		
    // pause if in menu and at least one tic has been run
    if ( !netgame
	 && menuactive
	 && !demoplayback
	 && players[consoleplayer].viewz != 1)
    {
	return;
    }
    
		
    for (i=0 ; i<MAXPLAYERS ; i++)
	if (playeringame[i])
	    P_PlayerThink (&players[i]);
			
    P_RunThinkers ();
    P_UpdateSpecials ();
    P_RespawnSpecials ();

    // for par times
    leveltime++;	
}
