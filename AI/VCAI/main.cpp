#include "StdInc.h"
#include "VCAI.h"

#ifdef __GNUC__
#define strcpy_s(a, b, c) strncpy(a, c, b)
#endif

const char *g_cszAiName = "VCAI";

extern "C" DLL_EXPORT int GetGlobalAiVersion()
{
	return AI_INTERFACE_VER;
}

extern "C" DLL_EXPORT void GetAiName(char* name)
{
	strcpy_s(name, strlen(g_cszAiName) + 1, g_cszAiName);
}

extern "C" DLL_EXPORT CGlobalAI* GetNewAI()
{
	return new VCAI();
}