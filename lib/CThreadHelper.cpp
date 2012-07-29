#include "StdInc.h"
#include "CThreadHelper.h"

#ifdef _WIN32
	#include <windows.h>
#else
	#include <sys/prctl.h>
#endif
/*
 * CThreadHelper.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

CThreadHelper::CThreadHelper(std::vector<boost::function<void()> > *Tasks, int Threads)
{
	currentTask = 0; amount = Tasks->size();
	tasks = Tasks;
	threads = Threads;
}
void CThreadHelper::run()
{
	boost::thread_group grupa;
	for(int i=0;i<threads;i++)
		grupa.create_thread(boost::bind(&CThreadHelper::processTasks,this));
	grupa.join_all();
}
void CThreadHelper::processTasks()
{
	int pom;
	while(true)
	{
		{
			boost::unique_lock<boost::mutex> lock(rtinm); 
			if((pom = currentTask) >= amount)
				break;
			else
				++currentTask;
		}
		(*tasks)[pom]();
	}
}

// set name for this thread.
// NOTE: on *nix string will be trimmed to 16 symbols
void setThreadName(const std::string &name)
{
#ifdef _WIN32
	//follows http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
	const DWORD MS_VC_EXCEPTION=0x406D1388;
#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
#pragma pack(pop)
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = name.c_str();
	info.dwThreadID = -1;
	info.dwFlags = 0;

	__try
	{
		RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}
#else
	 prctl(PR_SET_NAME, name.c_str(), 0, 0, 0);
#endif
}