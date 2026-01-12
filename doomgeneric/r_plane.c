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
//	Here is a core component: drawing the floors and ceilings,
//	 while maintaining a per column clipping list only.
//	Moreover, the sky areas have to be determined.
//


#include <stdio.h>
#include <stdlib.h>

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"

#include "r_local.h"
#include "r_sky.h"



planefunction_t		floorfunc;
planefunction_t		ceilingfunc;

//
// opening
//

//
// PAGER OPTIMIZATION: Adaptive visplane limits
//
// When ADAPTIVE_VISPLANES is defined, we start at the vanilla Doom limit (128)
// and automatically expand when needed, up to 768. This prevents crashes on
// complex maps like SIGIL while maintaining vanilla behavior on simple maps.
//
// Without this flag, we use a fixed 512 limit (increased from vanilla 128).
//
// Source: Custom optimization for SIGIL compatibility
// Expected gain: Crash prevention on complex maps
//
// Compile with: -DADAPTIVE_VISPLANES
//
#ifdef ADAPTIVE_VISPLANES

#define MAXVISPLANES_POOL       768   // Static allocation size
#define MAXVISPLANES_DEFAULT    128   // Start at vanilla Doom limit
#define MAXVISPLANES_STEP1      256
#define MAXVISPLANES_STEP2      512
#define MAXVISPLANES_STEP3      768
#define VISPLANE_EXPAND_THRESHOLD 0.9
#define VISPLANE_MERGE_THRESHOLD  8

visplane_t		visplanes[MAXVISPLANES_POOL];
static int		visplane_limit = MAXVISPLANES_DEFAULT;
static int		visplane_peak = 0;
static int		visplane_merges = 0;
static int		visplane_expansions = 0;

#else

// Fixed limit (increased from vanilla 128 for SIGIL)
#define MAXVISPLANES	512
visplane_t		visplanes[MAXVISPLANES];

#endif

visplane_t*		lastvisplane;
visplane_t*		floorplane;
visplane_t*		ceilingplane;

// ?
#define MAXOPENINGS	SCREENWIDTH*64
short			openings[MAXOPENINGS];
short*			lastopening;


//
// Clip values are the solid pixel bounding the range.
//  floorclip starts out SCREENHEIGHT
//  ceilingclip starts out -1
//
short			floorclip[SCREENWIDTH];
short			ceilingclip[SCREENWIDTH];

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
int			spanstart[SCREENHEIGHT];
int			spanstop[SCREENHEIGHT];

//
// texture mapping
//
lighttable_t**		planezlight;
fixed_t			planeheight;

fixed_t			yslope[SCREENHEIGHT];
fixed_t			distscale[SCREENWIDTH];
fixed_t			basexscale;
fixed_t			baseyscale;

fixed_t			cachedheight[SCREENHEIGHT];
fixed_t			cacheddistance[SCREENHEIGHT];
fixed_t			cachedxstep[SCREENHEIGHT];
fixed_t			cachedystep[SCREENHEIGHT];



//
// R_InitPlanes
// Only at game startup.
//
void R_InitPlanes (void)
{
  // Doh!
}


//
// R_MapPlane
//
// Uses global vars:
//  planeheight
//  ds_source
//  basexscale
//  baseyscale
//  viewx
//  viewy
//
// BASIC PRIMITIVE
//
void
R_MapPlane
( int		y,
  int		x1,
  int		x2 )
{
    angle_t	angle;
    fixed_t	distance;
    fixed_t	length;
    unsigned	index;
	
#ifdef RANGECHECK
    if (x2 < x1
     || x1 < 0
     || x2 >= viewwidth
     || y > viewheight)
    {
	I_Error ("R_MapPlane: %i, %i at %i",x1,x2,y);
    }
#endif

    if (planeheight != cachedheight[y])
    {
	cachedheight[y] = planeheight;
	distance = cacheddistance[y] = FixedMul (planeheight, yslope[y]);
	ds_xstep = cachedxstep[y] = FixedMul (distance,basexscale);
	ds_ystep = cachedystep[y] = FixedMul (distance,baseyscale);
    }
    else
    {
	distance = cacheddistance[y];
	ds_xstep = cachedxstep[y];
	ds_ystep = cachedystep[y];
    }
	
    length = FixedMul (distance,distscale[x1]);
    angle = (viewangle + xtoviewangle[x1])>>ANGLETOFINESHIFT;
    ds_xfrac = viewx + FixedMul(finecosine[angle], length);
    ds_yfrac = -viewy - FixedMul(finesine[angle], length);

    if (fixedcolormap)
	ds_colormap = fixedcolormap;
    else
    {
	index = distance >> LIGHTZSHIFT;
	
	if (index >= MAXLIGHTZ )
	    index = MAXLIGHTZ-1;

	ds_colormap = planezlight[index];
    }
	
    ds_y = y;
    ds_x1 = x1;
    ds_x2 = x2;

    // high or low detail
    spanfunc ();	
}


//
// R_ClearPlanes
// At begining of frame.
//
void R_ClearPlanes (void)
{
    int		i;
    angle_t	angle;
    
    // opening / clipping determination
    for (i=0 ; i<viewwidth ; i++)
    {
	floorclip[i] = viewheight;
	ceilingclip[i] = -1;
    }

    lastvisplane = visplanes;
    lastopening = openings;
    
    // texture calculation
    memset (cachedheight, 0, sizeof(cachedheight));

    // left to right mapping
    angle = (viewangle-ANG90)>>ANGLETOFINESHIFT;
	
    // scale will be unit scale at SCREENWIDTH/2 distance
    basexscale = FixedDiv (finecosine[angle],centerxfrac);
    baseyscale = -FixedDiv (finesine[angle],centerxfrac);
}




#ifdef ADAPTIVE_VISPLANES
// Auto-expand visplane limit when approaching threshold
static void R_ExpandVisplaneLimit(void)
{
    int new_limit = visplane_limit;
    
    if (visplane_limit < MAXVISPLANES_STEP1)
        new_limit = MAXVISPLANES_STEP1;
    else if (visplane_limit < MAXVISPLANES_STEP2)
        new_limit = MAXVISPLANES_STEP2;
    else if (visplane_limit < MAXVISPLANES_STEP3)
        new_limit = MAXVISPLANES_STEP3;
    
    if (new_limit > visplane_limit) {
        visplane_limit = new_limit;
        visplane_expansions++;
    }
}
#endif

//
// R_FindPlane
//
visplane_t*
R_FindPlane
( fixed_t	height,
  int		picnum,
  int		lightlevel )
{
    visplane_t*	check;
#ifdef ADAPTIVE_VISPLANES
    visplane_t* bestmatch = NULL;
    int bestdiff = VISPLANE_MERGE_THRESHOLD + 1;
    int current_count;
#endif
	
    if (picnum == skyflatnum)
    {
	height = 0;			// all skys map together
	lightlevel = 0;
    }
	
    for (check=visplanes; check<lastvisplane; check++)
    {
	if (height == check->height
	    && picnum == check->picnum
	    && lightlevel == check->lightlevel)
	{
#ifdef ADAPTIVE_VISPLANES
	    return check;
#else
	    break;
#endif
	}
    }
    
#ifdef ADAPTIVE_VISPLANES
    // No exact match - need to allocate
    current_count = lastvisplane - visplanes;
    
    // Auto-expand if approaching limit
    if (current_count >= (int)(visplane_limit * VISPLANE_EXPAND_THRESHOLD))
        R_ExpandVisplaneLimit();
    
    // At hard limit - graceful degradation
    if (current_count >= visplane_limit && visplane_limit >= MAXVISPLANES_STEP3)
    {
        // Find closest match for merging
        for (check=visplanes; check<lastvisplane; check++)
        {
            if (picnum == check->picnum)
            {
                int heightdiff = abs((height >> FRACBITS) - (check->height >> FRACBITS));
                if (heightdiff < bestdiff)
                {
                    bestdiff = heightdiff;
                    bestmatch = check;
                }
            }
        }
        
        if (bestmatch && bestdiff <= VISPLANE_MERGE_THRESHOLD)
        {
            visplane_merges++;
            return bestmatch;
        }
        
        // Emergency: return any plane with same texture
        for (check=visplanes; check<lastvisplane; check++)
        {
            if (picnum == check->picnum)
            {
                visplane_merges++;
                return check;
            }
        }
        visplane_merges++;
        return visplanes;  // Last resort
    }
    
    // Normal allocation
    check = lastvisplane++;
    if ((lastvisplane - visplanes) > visplane_peak)
        visplane_peak = lastvisplane - visplanes;
#else
    if (check < lastvisplane)
	return check;
		
    if (lastvisplane - visplanes == MAXVISPLANES)
	I_Error ("R_FindPlane: no more visplanes");
		
    lastvisplane++;
#endif

    check->height = height;
    check->picnum = picnum;
    check->lightlevel = lightlevel;
    check->minx = SCREENWIDTH;
    check->maxx = -1;
    
    memset (check->top,0xff,sizeof(check->top));
		
    return check;
}


//
// R_CheckPlane
//
visplane_t*
R_CheckPlane
( visplane_t*	pl,
  int		start,
  int		stop )
{
    int		intrl;
    int		intrh;
    int		unionl;
    int		unionh;
    int		x;
#ifdef ADAPTIVE_VISPLANES
    int		current_count;
#endif
	
    if (start < pl->minx)
    {
	intrl = pl->minx;
	unionl = start;
    }
    else
    {
	unionl = pl->minx;
	intrl = start;
    }
	
    if (stop > pl->maxx)
    {
	intrh = pl->maxx;
	unionh = stop;
    }
    else
    {
	unionh = pl->maxx;
	intrh = stop;
    }

    for (x=intrl ; x<= intrh ; x++)
	if (pl->top[x] != 0xff)
	    break;

    if (x > intrh)
    {
	pl->minx = unionl;
	pl->maxx = unionh;

	// use the same one
	return pl;		
    }

#ifdef ADAPTIVE_VISPLANES
    current_count = lastvisplane - visplanes;
    
    // Auto-expand if approaching limit
    if (current_count >= (int)(visplane_limit * VISPLANE_EXPAND_THRESHOLD))
        R_ExpandVisplaneLimit();
    
    // At hard limit: reuse existing plane
    if (current_count >= visplane_limit && visplane_limit >= MAXVISPLANES_STEP3)
    {
        visplane_merges++;
        pl->minx = unionl;
        pl->maxx = unionh;
        return pl;
    }
#endif
	
    // make a new visplane
    lastvisplane->height = pl->height;
    lastvisplane->picnum = pl->picnum;
    lastvisplane->lightlevel = pl->lightlevel;
    
    pl = lastvisplane++;
#ifdef ADAPTIVE_VISPLANES
    if ((lastvisplane - visplanes) > visplane_peak)
        visplane_peak = lastvisplane - visplanes;
#endif
    pl->minx = start;
    pl->maxx = stop;

    memset (pl->top,0xff,sizeof(pl->top));
		
    return pl;
}


//
// R_MakeSpans
//
void
R_MakeSpans
( int		x,
  int		t1,
  int		b1,
  int		t2,
  int		b2 )
{
    while (t1 < t2 && t1<=b1)
    {
	R_MapPlane (t1,spanstart[t1],x-1);
	t1++;
    }
    while (b1 > b2 && b1>=t1)
    {
	R_MapPlane (b1,spanstart[b1],x-1);
	b1--;
    }
	
    while (t2 < t1 && t2<=b2)
    {
	spanstart[t2] = x;
	t2++;
    }
    while (b2 > b1 && b2>=t2)
    {
	spanstart[b2] = x;
	b2--;
    }
}



//
// R_DrawPlanes
// At the end of each frame.
//
void R_DrawPlanes (void)
{
    visplane_t*		pl;
    int			light;
    int			x;
    int			stop;
    int			angle;
    int                 lumpnum;
				
#ifdef RANGECHECK
    if (ds_p - drawsegs > MAXDRAWSEGS)
	I_Error ("R_DrawPlanes: drawsegs overflow (%i)",
		 ds_p - drawsegs);
    
#ifdef ADAPTIVE_VISPLANES
    if (lastvisplane - visplanes > MAXVISPLANES_POOL)
	I_Error ("R_DrawPlanes: visplane overflow (%i)",
		 lastvisplane - visplanes);
#else
    if (lastvisplane - visplanes > MAXVISPLANES)
	I_Error ("R_DrawPlanes: visplane overflow (%i)",
		 lastvisplane - visplanes);
#endif
    
    if (lastopening - openings > MAXOPENINGS)
	I_Error ("R_DrawPlanes: opening overflow (%i)",
		 lastopening - openings);
#endif

    for (pl = visplanes ; pl < lastvisplane ; pl++)
    {
	if (pl->minx > pl->maxx)
	    continue;

	
	// sky flat
	if (pl->picnum == skyflatnum)
	{
	    dc_iscale = pspriteiscale>>detailshift;
	    
	    // Sky is allways drawn full bright,
	    //  i.e. colormaps[0] is used.
	    // Because of this hack, sky is not affected
	    //  by INVUL inverse mapping.
	    dc_colormap = colormaps;
	    dc_texturemid = skytexturemid;
	    for (x=pl->minx ; x <= pl->maxx ; x++)
	    {
		dc_yl = pl->top[x];
		dc_yh = pl->bottom[x];

		if (dc_yl <= dc_yh)
		{
		    angle = (viewangle + xtoviewangle[x])>>ANGLETOSKYSHIFT;
		    dc_x = x;
		    dc_source = R_GetColumn(skytexture, angle);
		    colfunc ();
		}
	    }
	    continue;
	}
	
	// regular flat
        lumpnum = firstflat + flattranslation[pl->picnum];
	ds_source = W_CacheLumpNum(lumpnum, PU_STATIC);
	
	planeheight = abs(pl->height-viewz);
	light = (pl->lightlevel >> LIGHTSEGSHIFT)+extralight;

	if (light >= LIGHTLEVELS)
	    light = LIGHTLEVELS-1;

	if (light < 0)
	    light = 0;

	planezlight = zlight[light];

	pl->top[pl->maxx+1] = 0xff;
	pl->top[pl->minx-1] = 0xff;
		
	stop = pl->maxx + 1;

	for (x=pl->minx ; x<= stop ; x++)
	{
	    R_MakeSpans(x,pl->top[x-1],
			pl->bottom[x-1],
			pl->top[x],
			pl->bottom[x]);
	}
	
        W_ReleaseLumpNum(lumpnum);
    }
}
