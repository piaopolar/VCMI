#include "StdInc.h"

#include "SDL.h"
#include "SDL_image.h"
#include "CBitmapHandler.h"
#include "CDefHandler.h"
#include "../lib/CLodHandler.h"
#include "../lib/vcmi_endian.h"

/*
 * CBitmapHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

extern DLL_LINKAGE CLodHandler *bitmaph;
extern DLL_LINKAGE CLodHandler *bitmaph_ab;
extern DLL_LINKAGE CLodHandler *spriteh;

void CPCXConv::openPCX(char * PCX, int len)
{
	pcxs=len;
	pcx=(ui8*)PCX;
}
void CPCXConv::fromFile(std::string path)
{
	std::ifstream is;
	is.open(path.c_str(),std::ios::binary);
	is.seekg(0,std::ios::end); // to the end
	pcxs = is.tellg();  // read length
	is.seekg(0,std::ios::beg); // wracamy na poczatek
	pcx = new ui8[pcxs]; // allocate memory 
	is.read((char*)pcx, pcxs); // read map file to buffer
	is.close();
}

void CPCXConv::saveBMP(std::string path) const
{
	std::ofstream os;
	os.open(path.c_str(), std::ios::binary);
	os.write(reinterpret_cast<const char*>(bmp), bmps);
	os.close();
}

SDL_Surface * CPCXConv::getSurface() const
{
	SDL_Surface * ret;

	int width = -1, height = -1;
	Epcxformat format;
	int fSize;
	int it=0;

	fSize = read_le_u32(pcx + it); it+=4;
	width = read_le_u32(pcx + it); it+=4;
	height = read_le_u32(pcx + it); it+=4;
	
	if (fSize==width*height*3)
		format=PCX24B;
	else if (fSize==width*height)
		format=PCX8B;
	else 
		return NULL;

	if (format==PCX8B)
	{
		ret = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);

		it = 0xC;
		for (int i=0; i<height; i++)
		{
			memcpy((char*)ret->pixels + ret->pitch * i, pcx + it, width);
			it+= width;
		}

		it = pcxs-256*3;
		for (int i=0;i<256;i++)
		{
			SDL_Color tp;
			tp.r = pcx[it++];
			tp.g = pcx[it++];
			tp.b = pcx[it++];
			tp.unused = 255;
			ret->format->palette->colors[i] = tp;
		}
	}
	else
	{
#if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
		int bmask = 0xff0000;
		int gmask = 0x00ff00;
		int rmask = 0x0000ff;
#else
		int bmask = 0x0000ff;
		int gmask = 0x00ff00;
		int rmask = 0xff0000;
#endif
		ret = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 24, rmask, gmask, bmask, 0);

		it = 0xC;
		for (int i=0; i<height; i++)
		{
			memcpy((char*)ret->pixels + ret->pitch * i, pcx + it, width*3);
			it+= width*3;
		}

	}
	return ret;
}

bool isPCX(const ui8 *header)//check whether file can be PCX according to 1st 12 bytes
{
	int fSize  = read_le_u32(header + 0);
	int width  = read_le_u32(header + 4);
	int height = read_le_u32(header + 8);
	return fSize == width*height || fSize == width*height*3;
}

SDL_Surface * BitmapHandler::loadBitmapFromLod(CLodHandler *lod, std::string fname, bool setKey)
{
	if(!fname.size())
	{
		tlog2 << "Call to loadBitmap with void fname!\n";
		return NULL;
	}
	if (!lod->haveFile(fname, FILE_GRAPHICS))
	{
		//it is possible that this image will be found in another lod archive. Disabling this warning
		//tlog2<<"Entry for file "<<fname<<" was not found"<<std::endl;
		return NULL;
	}

	SDL_Surface * ret=NULL;
	int size;
	ui8 * file = 0;
	file = lod->giveFile(fname, FILE_GRAPHICS, &size);
	
	if (isPCX(file))
	{//H3-style PCX
		CPCXConv cp;
		cp.openPCX((char*)file,size);
		ret = cp.getSurface();
		if (!ret)
			tlog1<<"Failed to open "<<fname<<" as H3 PCX!\n";
		if(ret->format->BytesPerPixel == 1  &&  setKey)
		{
			const SDL_Color &c = ret->format->palette->colors[0];
			SDL_SetColorKey(ret,SDL_SRCCOLORKEY,SDL_MapRGB(ret->format, c.r, c.g, c.b));
		}
	}
	else
	{ //loading via SDL_Image
		std::string filename = lod->getFileName(fname, FILE_GRAPHICS);
		std::string ext;
		lod->convertName(filename, &ext);

		if (ext == ".TGA")//Special case - targa can't be loaded by IMG_Load_RW (no magic constants in header)
		{
			SDL_RWops *rw = SDL_RWFromMem((void*)file, size);
			ret = IMG_LoadTGA_RW( rw );
			SDL_FreeRW(rw);
		}
		else
			ret = IMG_Load_RW( SDL_RWFromMem((void*)file, size), 1);

		if (!ret)
			tlog1<<"Failed to open "<<fname<<" via SDL_Image\n";
		delete [] file;
		if (ret->format->palette)
		{
			//set correct value for alpha\unused channel
			for (int i=0; i< ret->format->palette->ncolors; i++)
				ret->format->palette->colors[i].unused = 255;
		}
	}
	return ret;
}

SDL_Surface * BitmapHandler::loadBitmap(std::string fname, bool setKey)
{
	SDL_Surface *bitmap;

	if (!(bitmap = loadBitmapFromLod(bitmaph, fname, setKey)) &&
		!(bitmap = loadBitmapFromLod(bitmaph_ab, fname, setKey)) &&
		!(bitmap = loadBitmapFromLod(spriteh, fname, setKey)))
		tlog0<<"Error: Failed to find file "<<fname<<"\n";

	return bitmap;
}
