#pragma once

/*
 * SDL_framerate.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


/// A fps manager which holds game updates at a constant rate
class FPSManager
{
private:
	double rateticks;
	ui32 lastticks;
	int rate;

public:
	int fps; // the actual fps value

	FPSManager(int rate); // initializes the manager with a given fps rate
	void init(); // needs to be called directly before the main game loop to reset the internal timer
	void framerateDelay(); // needs to be called every game update cycle
};
