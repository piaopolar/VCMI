#pragma once

/*
 * CondSh.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

/// Used for multithreading, wraps boost functions
template <typename T> struct CondSh
{
	T data;
	boost::condition_variable cond;
	boost::mutex mx;

	CondSh()
	{}

	CondSh(T t)
	{
		data = t;
	}

	void set(T t)
	{
		boost::unique_lock<boost::mutex> lock(mx); 
		data=t;
	} 

	void setn(T t) //set data and notify
	{
		{
			boost::unique_lock<boost::mutex> lock(mx); 
			data=t;
		}
		cond.notify_all();
	};

	T get() //get stored value
	{
		boost::unique_lock<boost::mutex> lock(mx); 
		return data;
	}

	void waitWhileTrue() //waits until data is set to false
	{
		boost::unique_lock<boost::mutex> un(mx);
		while(data)
			cond.wait(un);
	}

	void waitWhile(const T &t) //waits while data is set to arg
	{
		boost::unique_lock<boost::mutex> un(mx);
		while(data == t)
			cond.wait(un);
	}

	void waitUntil(const T &t) //waits until data is set to arg
	{
		boost::unique_lock<boost::mutex> un(mx);
		while(data != t)
			cond.wait(un);
	}
};
