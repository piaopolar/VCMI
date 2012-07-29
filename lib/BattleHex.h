#pragma once

#include "GameConstants.h"


/*
 * BattleHex.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// for battle stacks' positions
struct DLL_LINKAGE BattleHex
{
	static const si16 INVALID = -1;
	enum EDir{RIGHT, BOTTOM_RIGHT, BOTTOM_LEFT, LEFT, TOP_LEFT, TOP_RIGHT};

	si16 hex;

	BattleHex() : hex(INVALID) {}
	BattleHex(si16 _hex) : hex(_hex) {}
	
	operator si16() const
	{
		return hex;
	}

	bool isValid() const
	{
		return hex >= 0 && hex < GameConstants::BFIELD_SIZE;
	}

	template<typename inttype>
	BattleHex(inttype x, inttype y)
	{
		setXY(x, y);
	}

	template<typename inttype>
	BattleHex(std::pair<inttype, inttype> xy)
	{
		setXY(xy);
	}

	template<typename inttype>
	void setX(inttype x)
	{
		setXY(x, getY());
	}

	template<typename inttype>
	void setY(inttype y)
	{
		setXY(getX(), y);
	}

	void setXY(si16 x, si16 y, bool hasToBeValid = true)
	{
		if(hasToBeValid)
		{
			assert(x >= 0 && x < GameConstants::BFIELD_WIDTH && y >= 0  && y < GameConstants::BFIELD_HEIGHT);
		}
		hex = x + y * GameConstants::BFIELD_WIDTH;
	}

	template<typename inttype>
	void setXY(std::pair<inttype, inttype> xy)
	{
		setXY(xy.first, xy.second);
	}

	si16 getY() const
	{
		return hex / GameConstants::BFIELD_WIDTH;
	}

	si16 getX() const
	{
		int pos = hex - getY() * GameConstants::BFIELD_WIDTH;
		return pos;
	}

	std::pair<si16, si16> getXY() const
	{
		return std::make_pair(getX(), getY());
	}

	//moving to direction
	BattleHex& moveInDir(EDir dir, bool hasToBeValid = true); 
	void operator+=(EDir dir); //sugar for above

	//generates new BattleHex moved by given dir
	BattleHex operator+(EDir dir) const;

	std::vector<BattleHex> neighbouringTiles() const;

	//returns info about mutual position of given hexes (-1 - they're distant, 0 - left top, 1 - right top, 2 - right, 3 - right bottom, 4 - left bottom, 5 - left)
	static signed char mutualPosition(BattleHex hex1, BattleHex hex2);

	//returns distance between given hexes
	static char getDistance(BattleHex hex1, BattleHex hex2);

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & hex;
	}
	static void checkAndPush(int tile, std::vector<BattleHex> & ret);

	bool isAvailable() const; //valid position not in first or last column
};