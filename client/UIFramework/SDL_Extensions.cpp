#include "StdInc.h"

#include "iconv.h"
#include "SDL_Extensions.h"
#include "SDL_Pixels.h"

#include <SDL_ttf.h>
#include "../CGameInfo.h"
#include "../CMessage.h"
#include "../CDefHandler.h"
#include "../Graphics.h"

/*
 * SDL_Extensions.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


int convert(const char *from, const char *to, char* save, int savelen, char *src, int srclen)  
{  
	iconv_t cd;  
	char   *inbuf = src;  
	char *outbuf = save;  
	size_t outbufsize = savelen;  
	int status = 0;  
	size_t  savesize = 0;  
	size_t inbufsize = srclen;  
	const char* inptr = inbuf;  
	size_t      insize = inbufsize;  
	char* outptr = outbuf;  
	size_t outsize = outbufsize;  
	cd = iconv_open(to, from);  
	iconv(cd,NULL,NULL,NULL,NULL);  
	if (inbufsize == 0) {  
		status = -1;  
		goto done;  
	}  
	while (insize > 0) {  
		size_t res = iconv(cd,(const char**)&inptr,&insize,&outptr,&outsize);  
		if (outptr != outbuf) {  
			int saved_errno = errno;  
			int outsize = outptr - outbuf;  
			strncpy(save+savesize, outbuf, outsize);  
			errno = saved_errno;  
		}  
		if (res == (size_t)(-1)) {  
			if (errno == EILSEQ) {  
				int one = 1;  
				iconvctl(cd,ICONV_SET_DISCARD_ILSEQ,&one);  
				status = -3;  
			} else if (errno == EINVAL) {  
				if (inbufsize == 0) {  
					status = -4;  
					goto done;  
				} else {  
					break;  
				}  
			} else if (errno == E2BIG) {  
				status = -5;  
				goto done;  
			} else {  
				status = -6;  
				goto done;  
			}  
		}  
	}  
	status = strlen(save);  
done:  
	iconv_close(cd);  
	return status;  
} 

void AnsiToUTF8(std::string strAnsi, std::string& rStrUTF8)
{
	std::vector<unsigned char> tobuf(2 * strAnsi.length() + 16); 
	int nlen = convert("gb2312","UTF-8", (char *)&tobuf[0], tobuf.size(), (char *)strAnsi.c_str(), strAnsi.length());  
	if (nlen < 0) {  
		rStrUTF8 = "";
	} else {
		tobuf.resize(nlen);  
		rStrUTF8 = std::string(tobuf.begin(), tobuf.end());  
	}  
}

SDL_Color Colors::createColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a /*= 0*/)
{
	SDL_Color temp = {r, g, b, a};
	return temp;
}

SDL_Surface * CSDL_Ext::newSurface(int w, int h, SDL_Surface * mod) //creates new surface, with flags/format same as in surface given
{
	SDL_Surface * ret = SDL_CreateRGBSurface(mod->flags,w,h,mod->format->BitsPerPixel,mod->format->Rmask,mod->format->Gmask,mod->format->Bmask,mod->format->Amask);
	if (mod->format->palette)
	{
		assert(ret->format->palette);
		assert(ret->format->palette->ncolors == mod->format->palette->ncolors);
		memcpy(ret->format->palette->colors, mod->format->palette->colors, mod->format->palette->ncolors * sizeof(SDL_Color));
	}
	return ret;
}

SDL_Surface * CSDL_Ext::copySurface(SDL_Surface * mod) //returns copy of given surface
{
	//return SDL_DisplayFormat(mod);
	return SDL_ConvertSurface(mod, mod->format, mod->flags);
}

template<int bpp>
SDL_Surface * CSDL_Ext::createSurfaceWithBpp(int width, int height)
{
	int rMask = 0, gMask = 0, bMask = 0, aMask = 0;

	Channels::px<bpp>::r.set((Uint8*)&rMask, 255);
	Channels::px<bpp>::g.set((Uint8*)&gMask, 255);
	Channels::px<bpp>::b.set((Uint8*)&bMask, 255);
	Channels::px<bpp>::a.set((Uint8*)&aMask, 255);

	return SDL_CreateRGBSurface( SDL_SWSURFACE, width, height, bpp * 8, rMask, gMask, bMask, aMask);
}

bool isItIn(const SDL_Rect * rect, int x, int y)
{
	return (x>rect->x && x<rect->x+rect->w) && (y>rect->y && y<rect->y+rect->h);
}

void blitAt(SDL_Surface * src, int x, int y, SDL_Surface * dst)
{
	if(!dst) dst = screen;
	SDL_Rect pom = genRect(src->h,src->w,x,y);
	CSDL_Ext::blitSurface(src,NULL,dst,&pom);
}

void blitAt(SDL_Surface * src, const SDL_Rect & pos, SDL_Surface * dst)
{
	blitAt(src,pos.x,pos.y,dst);
}

SDL_Color genRGB(int r, int g, int b, int a=0)
{
	SDL_Color ret;
	ret.b=b;
	ret.g=g;
	ret.r=r;
	ret.unused=a;
	return ret;
}

void updateRect (SDL_Rect * rect, SDL_Surface * scr)
{
	SDL_UpdateRect(scr,rect->x,rect->y,rect->w,rect->h);
}

void printAtMiddleWB(const std::string & text, int x, int y, TTF_Font * font, int charpr, SDL_Color kolor, SDL_Surface * dst)
{
	std::vector<std::string> ws = CMessage::breakText(text,charpr);
	std::vector<SDL_Surface*> wesu;
	wesu.resize(ws.size());
	for (size_t i=0; i < wesu.size(); ++i)
	{
		std::string strText;
		AnsiToUTF8(ws[i], strText);
		wesu[i]=TTF_RenderUTF8_Blended(font,ws[i].c_str(),kolor);
	}

	int tox=0, toy=0;
	for (size_t i=0; i < wesu.size(); ++i)
	{
		toy+=wesu[i]->h;
		if (tox < wesu[i]->w)
			tox=wesu[i]->w;
	}
	int evx, evy = y - (toy/2);
	for (size_t i=0; i < wesu.size(); ++i)
	{
		evx = (x - (tox/2)) + ((tox-wesu[i]->w)/2);
		blitAt(wesu[i],evx,evy,dst);
		evy+=wesu[i]->h;
	}


	for (size_t i=0; i < wesu.size(); ++i)
		SDL_FreeSurface(wesu[i]);
}

void printAtWB(const std::string & text, int x, int y, TTF_Font * font, int charpr, SDL_Color kolor, SDL_Surface * dst)
{
	std::vector<std::string> ws = CMessage::breakText(text,charpr);
	std::vector<SDL_Surface*> wesu;
	wesu.resize(ws.size());
	for (size_t i=0; i < wesu.size(); ++i) {
		std::string strUTF8;
		AnsiToUTF8(ws[i], strUTF8);
		wesu[i]=TTF_RenderUTF8_Blended(font,strUTF8.c_str(),kolor);
	}

	int evy = y;
	for (size_t i=0; i < wesu.size(); ++i)
	{
		blitAt(wesu[i],x,evy,dst);
		evy+=wesu[i]->h;
	}

	for (size_t i=0; i < wesu.size(); ++i)
		SDL_FreeSurface(wesu[i]);
}

void CSDL_Ext::printAtWB(const std::string & text, int x, int y, EFonts font, int charpr, SDL_Color kolor, SDL_Surface * dst)
{
	if (graphics->fontsTrueType[font])
	{
		printAtWB(text,x, y, graphics->fontsTrueType[font], charpr, kolor, dst);
		return;
	}
	const Font *f = graphics->fonts[font];
	std::vector<std::string> ws = CMessage::breakText(text,charpr);

	int cury = y;
	for (size_t i=0; i < ws.size(); ++i)
	{
		printAt(ws[i], x, cury, font, kolor, dst);
		cury += f->height;
	}
}


void CSDL_Ext::printAtMiddleWB( const std::string & text, int x, int y, EFonts font, int charpr, SDL_Color kolor/*=Colors::Jasmine*/, SDL_Surface * dst/*=screen*/ )
{
	if (graphics->fontsTrueType[font])
	{
		printAtMiddleWB(text,x, y, graphics->fontsTrueType[font], charpr, kolor, dst);
		return;
	}

	const Font *f = graphics->fonts[font];
	std::vector<std::string> ws = CMessage::breakText(text,charpr);
	int totalHeight = ws.size() * f->height;

	int cury = y - totalHeight/2;
	for (size_t i=0; i < ws.size(); ++i)
	{
		printAt(ws[i], x - f->getWidth(ws[i].c_str())/2, cury, font, kolor, dst);
		cury += f->height;
	}
}

void printAtMiddle(const std::string & textOrg, int x, int y, TTF_Font * font, SDL_Color kolor, SDL_Surface * dst, ui8 quality=2)
{
	if(textOrg.length()==0) return;
	
	std::string text;
	AnsiToUTF8(textOrg, text);

	SDL_Surface * temp;
	switch (quality)
	{
	case 0:
		temp = TTF_RenderUTF8_Solid(font,text.c_str(),kolor);
		break;
	case 1:
		SDL_Color tem;
		tem.b = 0xff-kolor.b;
		tem.g = 0xff-kolor.g;
		tem.r = 0xff-kolor.r;
		tem.unused = 0xff-kolor.unused;
		temp = TTF_RenderUTF8_Shaded(font,text.c_str(),kolor,tem);
		break;
	case 2:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	default:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	}
	SDL_Rect dstRect = genRect(temp->h, temp->w, x-(temp->w/2), y-(temp->h/2));
	CSDL_Ext::blitSurface(temp, NULL, dst, &dstRect);
	SDL_FreeSurface(temp);
}

void CSDL_Ext::printAtMiddle( const std::string & text, int x, int y, EFonts font, SDL_Color kolor/*=Colors::Cornsilk*/, SDL_Surface * dst/*=screen*/ )
{
	if (graphics->fontsTrueType[font])
	{
		printAtMiddle(text,x, y, graphics->fontsTrueType[font], kolor, dst);
		return;
	}
	const Font *f = graphics->fonts[font];
	int nx = x - f->getWidth(text.c_str())/2,
		ny = y - f->height/2;

	printAt(text, nx, ny, font, kolor, dst);
}

void printAt(const std::string & textOrg, int x, int y, TTF_Font * font, SDL_Color kolor, SDL_Surface * dst, ui8 quality=2, bool refresh=false)
{
	std::string text;
	AnsiToUTF8(textOrg, text);

	if (text.length()==0)
		return;
	SDL_Surface * temp;
	switch (quality)
	{
	case 0:
		temp = TTF_RenderUTF8_Solid(font,text.c_str(),kolor);
		break;
	case 1:
		SDL_Color tem;
		tem.b = 0xff-kolor.b;
		tem.g = 0xff-kolor.g;
		tem.r = 0xff-kolor.r;
		tem.unused = 0xff-kolor.unused;
		temp = TTF_RenderUTF8_Shaded(font,text.c_str(),kolor,tem);
		break;
	case 2:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	default:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	}
	SDL_Rect dstRect = genRect(temp->h,temp->w,x,y);
	CSDL_Ext::blitSurface(temp,NULL,dst,&dstRect);
	if(refresh)
		SDL_UpdateRect(dst,x,y,temp->w,temp->h);
	SDL_FreeSurface(temp);
}

void CSDL_Ext::printAt( const std::string & text, int dstX, int dstY, EFonts font, SDL_Color color, SDL_Surface * dst)
{
	if(!text.size())
		return;

	if (graphics->fontsTrueType[font])
	{
		printAt(text,dstX, dstY, graphics->fontsTrueType[font], color, dst);
		return;
	}

	assert(dst);
	assert(font < Graphics::FONTS_NUMBER);

	Rect clipRect;
	SDL_GetClipRect(dst, &clipRect);

	const Font *f = graphics->fonts[font];
	const Uint8 bpp = dst->format->BytesPerPixel;

	TColorPutter colorPutter = getPutterFor(dst, 0);

	//if text is in {} braces, we'll ommit them
	const int textBegin = (text[0] == '{' ? 1 : 0);
	const int textEnd = (text[text.size()-1] == '}' ? text.size()-1 : text.size());

	SDL_LockSurface(dst);
	// for each symbol
	for(int index = textBegin; index < textEnd; index++)
	{
		const ui8 symbol = text[index];
		dstX += f->chars[symbol].leftOffset;

		int lineBegin = std::max<int>(0, clipRect.y - dstY);
		int lineEnd   = std::min<int>(f->height, clipRect.y + clipRect.h - dstY - 1);

		int rowBegin = std::max(0, clipRect.x - dstX);
		int rowEnd   = std::min(f->chars[symbol].width, clipRect.x + clipRect.w - dstX - 1);

		//for each line in symbol
		for(int dy = lineBegin; dy <lineEnd; dy++)
		{
			Uint8 *dstLine = (Uint8*)dst->pixels;
			Uint8 *srcLine = f->chars[symbol].pixels;

			dstLine += (dstY+dy) * dst->pitch + dstX * bpp;
			srcLine += dy * f->chars[symbol].width;

			//for each column in line
			for(int dx = rowBegin; dx < rowEnd; dx++)
			{
				Uint8* dstPixel = dstLine + dx*bpp;
				switch(*(srcLine + dx))
				{
				case 1: //black "shadow"
					memset(dstPixel, 0, bpp);
					break;
				case 255: //text colour
					colorPutter(dstPixel, color.r, color.g, color.b);
					break;
				}
			}
		}

		dstX += f->chars[symbol].width;
		dstX += f->chars[symbol].rightOffset;
	}
	SDL_UnlockSurface(dst);
}

void printTo(const std::string & textOrg, int x, int y, TTF_Font * font, SDL_Color kolor, SDL_Surface * dst, ui8 quality=2)
{
	if (textOrg.length()==0)
		return;

	std::string text;
	AnsiToUTF8(textOrg, text);

	SDL_Surface * temp;
	switch (quality)
	{
	case 0:
		temp = TTF_RenderUTF8_Solid(font,text.c_str(),kolor);
		break;
	case 1:
		SDL_Color tem;
		tem.b = 0xff-kolor.b;
		tem.g = 0xff-kolor.g;
		tem.r = 0xff-kolor.r;
		tem.unused = 0xff-kolor.unused;
		temp = TTF_RenderUTF8_Shaded(font,text.c_str(),kolor,tem);
		break;
	case 2:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	default:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	}
	SDL_Rect dstRect = genRect(temp->h,temp->w,x-temp->w,y-temp->h);
	CSDL_Ext::blitSurface(temp,NULL,dst,&dstRect);
	SDL_UpdateRect(dst,x-temp->w,y-temp->h,temp->w,temp->h);
	SDL_FreeSurface(temp);
}

void CSDL_Ext::printTo( const std::string & text, int x, int y, EFonts font, SDL_Color kolor/*=Colors::Cornsilk*/, SDL_Surface * dst/*=screen*/ )
{
	if (graphics->fontsTrueType[font])
	{
		printTo(text,x, y, graphics->fontsTrueType[font], kolor, dst);
		return;
	}
	const Font *f = graphics->fonts[font];
	printAt(text, x - f->getWidth(text.c_str()), y - f->height, font, kolor, dst);
}

void printToWR(const std::string & textOrg, int x, int y, TTF_Font * font, SDL_Color kolor, SDL_Surface * dst, ui8 quality=2)
{
	if (textOrg.length()==0)
		return;

	std::string text;
	AnsiToUTF8(textOrg, text);

	SDL_Surface * temp;
	switch (quality)
	{
	case 0:
		temp = TTF_RenderUTF8_Solid(font,text.c_str(),kolor);
		break;
	case 1:
		SDL_Color tem;
		tem.b = 0xff-kolor.b;
		tem.g = 0xff-kolor.g;
		tem.r = 0xff-kolor.r;
		tem.unused = 0xff-kolor.unused;
		temp = TTF_RenderUTF8_Shaded(font,text.c_str(),kolor,tem);
		break;
	case 2:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	default:
		temp = TTF_RenderUTF8_Blended(font,text.c_str(),kolor);
		break;
	}
	SDL_Rect dstRect = genRect(temp->h,temp->w,x-temp->w,y-temp->h);
	CSDL_Ext::blitSurface(temp,NULL,dst,&dstRect);
	SDL_FreeSurface(temp);
}

// Vertical flip
SDL_Surface * CSDL_Ext::rotate01(SDL_Surface * toRot)
{
	SDL_Surface * ret = SDL_ConvertSurface(toRot, toRot->format, toRot->flags);
	const int bpl = ret->pitch;
	const int bpp = ret->format->BytesPerPixel;

	for(int i=0; i<ret->h; i++) {
		char *src = (char *)toRot->pixels + i*bpl;
		char *dst = (char *)ret->pixels + i*bpl;
		for(int j=0; j<ret->w; j++) {
			for (int k=0; k<bpp; k++) {
				dst[j*bpp + k] = src[(ret->w-j-1)*bpp + k];
			}
		}
	}

	return ret;
}

// Horizontal flip
SDL_Surface * CSDL_Ext::hFlip(SDL_Surface * toRot)
{
	SDL_Surface * ret = SDL_ConvertSurface(toRot, toRot->format, toRot->flags);
	int bpl = ret->pitch;

	for(int i=0; i<ret->h; i++) {
		memcpy((char *)ret->pixels + i*bpl, (char *)toRot->pixels + (ret->h-i-1)*bpl, bpl);
	}

	return ret;
};

///**************/
///Rotates toRot surface by 90 degrees left
///**************/
SDL_Surface * CSDL_Ext::rotate02(SDL_Surface * toRot)
{
	SDL_Surface * ret = SDL_ConvertSurface(toRot, toRot->format, toRot->flags);
	//SDL_SetColorKey(ret, SDL_SRCCOLORKEY, toRot->format->colorkey);
	for(int i=0; i<ret->w; ++i)
	{
		for(int j=0; j<ret->h; ++j)
		{
			{
				Uint8 *p = (Uint8 *)toRot->pixels + i * toRot->pitch + j * toRot->format->BytesPerPixel;
				SDL_PutPixelWithoutRefresh(ret, i, j, p[2], p[1], p[0]);
			}
		}
	}
	return ret;
}

///*************/
///Rotates toRot surface by 180 degrees
///*************/
SDL_Surface * CSDL_Ext::rotate03(SDL_Surface * toRot)
{
	SDL_Surface * ret = SDL_ConvertSurface(toRot, toRot->format, toRot->flags);
	if(ret->format->BytesPerPixel!=1)
	{
		for(int i=0; i<ret->w; ++i)
		{
			for(int j=0; j<ret->h; ++j)
			{
				{
					Uint8 *p = (Uint8 *)toRot->pixels + (ret->h - j - 1) * toRot->pitch + (ret->w - i - 1) * toRot->format->BytesPerPixel+2;
					SDL_PutPixelWithoutRefresh(ret, i, j, p[2], p[1], p[0], 0);
				}
			}
		}
	}
	else
	{
		for(int i=0; i<ret->w; ++i)
		{
			for(int j=0; j<ret->h; ++j)
			{
				Uint8 *p = (Uint8 *)toRot->pixels + (ret->h - j - 1) * toRot->pitch + (ret->w - i - 1) * toRot->format->BytesPerPixel;
				(*((Uint8*)ret->pixels + j*ret->pitch + i*ret->format->BytesPerPixel)) = *p;
			}
		}
	}
	return ret;
}
Uint32 CSDL_Ext::SDL_GetPixel(SDL_Surface *surface, const int & x, const int & y, bool colorByte)
{
	int bpp = surface->format->BytesPerPixel;
	/* Here p is the address to the pixel we want to retrieve */
	Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

	switch(bpp)
	{
	case 1:
		if(colorByte)
		{
			return colorToUint32(surface->format->palette->colors+(*p));
		}
		else
			return *p;

	case 2:
		return *(Uint16 *)p;

	case 3:
			return p[0] | p[1] << 8 | p[2] << 16;

	case 4:
		return *(Uint32 *)p;

	default:
		return 0;       // shouldn't happen, but avoids warnings
	}
}

void CSDL_Ext::alphaTransform(SDL_Surface *src)
{
	//NOTE: colors #7 & #8 used in some of WoG objects. Don't know how they're handled by H3
	assert(src->format->BitsPerPixel == 8);
	SDL_Color colors[] = {{0,0,0,0}, {0,0,0,32}, {0,0,0,64}, {0,0,0,128}, {0,0,0,128},
						{255,255,255,0}, {255,255,255,0}, {255,255,255,0}, {0,0,0,192}, {0,0,0,192}};

	SDL_SetColors(src, colors, 0, ARRAY_COUNT(colors));
	SDL_SetColorKey(src, SDL_SRCCOLORKEY, SDL_MapRGBA(src->format, 0, 0, 0, 255));
}

static void prepareOutRect(SDL_Rect *src, SDL_Rect *dst, const SDL_Rect & clip_rect)
{
	const int xoffset = std::max(clip_rect.x - dst->x, 0),
		yoffset = std::max(clip_rect.y - dst->y, 0);

	src->x += xoffset;
	src->y += yoffset;
	dst->x += xoffset;
	dst->y += yoffset;

	src->w = dst->w = std::max(0,std::min(dst->w - xoffset, clip_rect.x + clip_rect.w - dst->x));
	src->h = dst->h = std::max(0,std::min(dst->h - yoffset, clip_rect.y + clip_rect.h - dst->y));
}

template<int bpp>
void CSDL_Ext::blitWithRotateClip(SDL_Surface *src,SDL_Rect * srcRect, SDL_Surface * dst, SDL_Rect * dstRect, ui8 rotation)//srcRect is not used, works with 8bpp sources and 24bpp dests
{
	static void (*blitWithRotate[])(const SDL_Surface *, const SDL_Rect *, SDL_Surface *, const SDL_Rect *) = {blitWithRotate1<bpp>, blitWithRotate2<bpp>, blitWithRotate3<bpp>};
	if(!rotation)
	{
		CSDL_Ext::blitSurface(src, srcRect, dst, dstRect);
	}
	else
	{
		prepareOutRect(srcRect, dstRect, dst->clip_rect);
		blitWithRotate[rotation-1](src, srcRect, dst, dstRect);
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotateClipVal( SDL_Surface *src,SDL_Rect srcRect, SDL_Surface * dst, SDL_Rect dstRect, ui8 rotation )
{
	blitWithRotateClip<bpp>(src, &srcRect, dst, &dstRect, rotation);
}

template<int bpp>
void CSDL_Ext::blitWithRotateClipWithAlpha(SDL_Surface *src,SDL_Rect * srcRect, SDL_Surface * dst, SDL_Rect * dstRect, ui8 rotation)//srcRect is not used, works with 8bpp sources and 24bpp dests
{
	static void (*blitWithRotate[])(const SDL_Surface *, const SDL_Rect *, SDL_Surface *, const SDL_Rect *) = {blitWithRotate1WithAlpha<bpp>, blitWithRotate2WithAlpha<bpp>, blitWithRotate3WithAlpha<bpp>};
	if(!rotation)
	{
		blit8bppAlphaTo24bpp(src, srcRect, dst, dstRect);
	}
	else
	{
		prepareOutRect(srcRect, dstRect, dst->clip_rect);
		blitWithRotate[rotation-1](src, srcRect, dst, dstRect);
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotateClipValWithAlpha( SDL_Surface *src,SDL_Rect srcRect, SDL_Surface * dst, SDL_Rect dstRect, ui8 rotation )
{
	blitWithRotateClipWithAlpha<bpp>(src, &srcRect, dst, &dstRect, rotation);
}

template<int bpp>
void CSDL_Ext::blitWithRotate1(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
	Uint8 *sp = getPxPtr(src, src->w - srcRect->w - srcRect->x, srcRect->y);
	Uint8 *dporg = (Uint8 *)dst->pixels + dstRect->y*dst->pitch + (dstRect->x+dstRect->w)*bpp;
	const SDL_Color * const colors = src->format->palette->colors;

	for(int i=dstRect->h; i>0; i--, dporg += dst->pitch)
	{
		Uint8 *dp = dporg;
		for(int j=dstRect->w; j>0; j--, sp++)
			ColorPutter<bpp, -1>::PutColor(dp, colors[*sp]);

		sp += src->w - dstRect->w;
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotate2(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
	Uint8 *sp = getPxPtr(src, srcRect->x, src->h - srcRect->h - srcRect->y);
	Uint8 *dporg = (Uint8 *)dst->pixels + (dstRect->y + dstRect->h - 1)*dst->pitch + dstRect->x*bpp;
	const SDL_Color * const colors = src->format->palette->colors;

	for(int i=dstRect->h; i>0; i--, dporg -= dst->pitch)
	{
		Uint8 *dp = dporg;

		for(int j=dstRect->w; j>0; j--, sp++)
			ColorPutter<bpp, 1>::PutColor(dp, colors[*sp]);

		sp += src->w - dstRect->w;
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotate3(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
 	Uint8 *sp = (Uint8 *)src->pixels + (src->h - srcRect->h - srcRect->y)*src->pitch + (src->w - srcRect->w - srcRect->x);
 	Uint8 *dporg = (Uint8 *)dst->pixels +(dstRect->y + dstRect->h - 1)*dst->pitch + (dstRect->x+dstRect->w)*bpp;
 	const SDL_Color * const colors = src->format->palette->colors;

 	for(int i=dstRect->h; i>0; i--, dporg -= dst->pitch)
 	{
 		Uint8 *dp = dporg;
 		for(int j=dstRect->w; j>0; j--, sp++)
			ColorPutter<bpp, -1>::PutColor(dp, colors[*sp]);

		sp += src->w - dstRect->w;
 	}
}

template<int bpp>
void CSDL_Ext::blitWithRotate1WithAlpha(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
	Uint8 *sp = (Uint8 *)src->pixels + srcRect->y*src->pitch + (src->w - srcRect->w - srcRect->x);
	Uint8 *dporg = (Uint8 *)dst->pixels + dstRect->y*dst->pitch + (dstRect->x+dstRect->w)*bpp;
	const SDL_Color * const colors = src->format->palette->colors;

	for(int i=dstRect->h; i>0; i--, dporg += dst->pitch)
	{
		Uint8 *dp = dporg;
		for(int j=dstRect->w; j>0; j--, sp++)
		{
			if(*sp)
				ColorPutter<bpp, -1>::PutColor(dp, colors[*sp]);
			else
				dp -= bpp;
		}

		sp += src->w - dstRect->w;
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotate2WithAlpha(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
	Uint8 *sp = (Uint8 *)src->pixels + (src->h - srcRect->h - srcRect->y)*src->pitch + srcRect->x;
	Uint8 *dporg = (Uint8 *)dst->pixels + (dstRect->y + dstRect->h - 1)*dst->pitch + dstRect->x*bpp;
	const SDL_Color * const colors = src->format->palette->colors;

	for(int i=dstRect->h; i>0; i--, dporg -= dst->pitch)
	{
		Uint8 *dp = dporg;

		for(int j=dstRect->w; j>0; j--, sp++)
		{
			if(*sp)
				ColorPutter<bpp, 1>::PutColor(dp, colors[*sp]);
			else
				dp += bpp;
		}

		sp += src->w - dstRect->w;
	}
}

template<int bpp>
void CSDL_Ext::blitWithRotate3WithAlpha(const SDL_Surface *src, const SDL_Rect * srcRect, SDL_Surface * dst, const SDL_Rect * dstRect)//srcRect is not used, works with 8bpp sources and 24/32 bpp dests
{
	Uint8 *sp = (Uint8 *)src->pixels + (src->h - srcRect->h - srcRect->y)*src->pitch + (src->w - srcRect->w - srcRect->x);
	Uint8 *dporg = (Uint8 *)dst->pixels +(dstRect->y + dstRect->h - 1)*dst->pitch + (dstRect->x+dstRect->w)*bpp;
	const SDL_Color * const colors = src->format->palette->colors;

	for(int i=dstRect->h; i>0; i--, dporg -= dst->pitch)
	{
		Uint8 *dp = dporg;
		for(int j=dstRect->w; j>0; j--, sp++)
		{
			if(*sp)
				ColorPutter<bpp, -1>::PutColor(dp, colors[*sp]);
			else
				dp -= bpp;
		}

		sp += src->w - dstRect->w;
	}
}
template<int bpp>
int CSDL_Ext::blit8bppAlphaTo24bppT(const SDL_Surface * src, const SDL_Rect * srcRect, SDL_Surface * dst, SDL_Rect * dstRect)
{
	if (src && src->format->BytesPerPixel==1 && dst && (bpp==3 || bpp==4 || bpp==2)) //everything's ok
	{
		SDL_Rect fulldst;
		int srcx, srcy, w, h;

		/* Make sure the surfaces aren't locked */
		if ( ! src || ! dst )
		{
			SDL_SetError("SDL_UpperBlit: passed a NULL surface");
			return -1;
		}
		if ( src->locked || dst->locked )
		{
			SDL_SetError("Surfaces must not be locked during blit");
			return -1;
		}

		/* If the destination rectangle is NULL, use the entire dest surface */
		if ( dstRect == NULL )
		{
			fulldst.x = fulldst.y = 0;
			dstRect = &fulldst;
		}

		/* clip the source rectangle to the source surface */
		if(srcRect)
		{
			int maxw, maxh;

			srcx = srcRect->x;
			w = srcRect->w;
			if(srcx < 0)
			{
				w += srcx;
				dstRect->x -= srcx;
				srcx = 0;
			}
			maxw = src->w - srcx;
			if(maxw < w)
				w = maxw;

			srcy = srcRect->y;
			h = srcRect->h;
			if(srcy < 0)
			{
					h += srcy;
				dstRect->y -= srcy;
				srcy = 0;
			}
			maxh = src->h - srcy;
			if(maxh < h)
				h = maxh;

		}
		else
		{
			srcx = srcy = 0;
			w = src->w;
			h = src->h;
		}

		/* clip the destination rectangle against the clip rectangle */
		{
			SDL_Rect *clip = &dst->clip_rect;
			int dx, dy;

			dx = clip->x - dstRect->x;
			if(dx > 0)
			{
				w -= dx;
				dstRect->x += dx;
				srcx += dx;
			}
			dx = dstRect->x + w - clip->x - clip->w;
			if(dx > 0)
				w -= dx;

			dy = clip->y - dstRect->y;
			if(dy > 0)
			{
				h -= dy;
				dstRect->y += dy;
				srcy += dy;
			}
			dy = dstRect->y + h - clip->y - clip->h;
			if(dy > 0)
				h -= dy;
		}

		if(w > 0 && h > 0)
		{
			dstRect->w = w;
			dstRect->h = h;

			if(SDL_LockSurface(dst))
				return -1; //if we cannot lock the surface

			const SDL_Color *colors = src->format->palette->colors;
			Uint8 *colory = (Uint8*)src->pixels + srcy*src->pitch + srcx;
			Uint8 *py = (Uint8*)dst->pixels + dstRect->y*dst->pitch + dstRect->x*bpp;

			for(int y=h; y; y--, colory+=src->pitch, py+=dst->pitch)
			{
				Uint8 *color = colory;
				Uint8 *p = py;

				for(int x = w; x; x--)
				{
					const SDL_Color &tbc = colors[*color++]; //color to blit
					ColorPutter<bpp, +1>::PutColorAlphaSwitch(p, tbc.r, tbc.g, tbc.b, tbc.unused);
				}
			}
			SDL_UnlockSurface(dst);
		}
	}
	return 0;
}

int CSDL_Ext::blit8bppAlphaTo24bpp(const SDL_Surface * src, const SDL_Rect * srcRect, SDL_Surface * dst, SDL_Rect * dstRect)
{
	switch(dst->format->BytesPerPixel)
	{
	case 2: return blit8bppAlphaTo24bppT<2>(src, srcRect, dst, dstRect);
	case 3: return blit8bppAlphaTo24bppT<3>(src, srcRect, dst, dstRect);
	case 4: return blit8bppAlphaTo24bppT<4>(src, srcRect, dst, dstRect);
	default:
		tlog1 << (int)dst->format->BitsPerPixel << " bpp is not supported!!!\n";
		return -1;
	}
}

Uint32 CSDL_Ext::colorToUint32(const SDL_Color * color)
{
	Uint32 ret = 0;
	ret+=color->unused;
	ret<<=8; //*=256
	ret+=color->b;
	ret<<=8; //*=256
	ret+=color->g;
	ret<<=8; //*=256
	ret+=color->r;
	return ret;
}

void CSDL_Ext::update(SDL_Surface * what)
{
	if(what)
		SDL_UpdateRect(what, 0, 0, what->w, what->h);
}
void CSDL_Ext::drawBorder(SDL_Surface * sur, int x, int y, int w, int h, const int3 &color)
{
	for(int i = 0; i < w; i++)
	{
		SDL_PutPixelWithoutRefreshIfInSurf(sur,x+i,y,color.x,color.y,color.z);
		SDL_PutPixelWithoutRefreshIfInSurf(sur,x+i,y+h-1,color.x,color.y,color.z);
	}
	for(int i = 0; i < h; i++)
	{
		SDL_PutPixelWithoutRefreshIfInSurf(sur,x,y+i,color.x,color.y,color.z);
		SDL_PutPixelWithoutRefreshIfInSurf(sur,x+w-1,y+i,color.x,color.y,color.z);
	}
}

void CSDL_Ext::drawBorder( SDL_Surface * sur, const SDL_Rect &r, const int3 &color )
{
	drawBorder(sur, r.x, r.y, r.w, r.h, color);
}

void CSDL_Ext::drawDashedBorder(SDL_Surface * sur, const Rect &r, const int3 &color)
{
	const int y1 = r.y, y2 = r.y + r.h-1;
	for (int i=0; i<r.w; i++)
	{
		const int x = r.x + i;
		if (i%4 || (i==0))
		{
			SDL_PutPixelWithoutRefreshIfInSurf(sur, x, y1, color.x, color.y, color.z);
			SDL_PutPixelWithoutRefreshIfInSurf(sur, x, y2, color.x, color.y, color.z);
		}
	}

	const int x1 = r.x, x2 = r.x + r.w-1;
	for (int i=0; i<r.h; i++)
	{
		const int y = r.y + i;
		if ((i%4) || (i==0))
		{
			SDL_PutPixelWithoutRefreshIfInSurf(sur, x1, y, color.x, color.y, color.z);
			SDL_PutPixelWithoutRefreshIfInSurf(sur, x2, y, color.x, color.y, color.z);
		}
	}
}

void CSDL_Ext::setPlayerColor(SDL_Surface * sur, ui8 player)
{
	if(player==254)
		return;
	if(sur->format->BitsPerPixel==8)
	{
		SDL_Color *color = (player == 255
							? graphics->neutralColor
							: &graphics->playerColors[player]);
		SDL_SetColors(sur, color, 5, 1);
	}
	else
		tlog3 << "Warning, setPlayerColor called on not 8bpp surface!\n";
}

TColorPutter CSDL_Ext::getPutterFor(SDL_Surface * const &dest, int incrementing)
{
#define CASE_BPP(BytesPerPixel)							\
case BytesPerPixel:									\
	if(incrementing > 0)								\
		return ColorPutter<BytesPerPixel, 1>::PutColor;	\
	else if(incrementing == 0)							\
		return ColorPutter<BytesPerPixel, 0>::PutColor;	\
	else												\
		return ColorPutter<BytesPerPixel, -1>::PutColor;\
	break;

	switch(dest->format->BytesPerPixel)
	{
		CASE_BPP(2)
		CASE_BPP(3)
		CASE_BPP(4)
	default:
		tlog1 << (int)dest->format->BitsPerPixel << "bpp is not supported!\n";
		return NULL;
	}

}

TColorPutterAlpha CSDL_Ext::getPutterAlphaFor(SDL_Surface * const &dest, int incrementing)
{
	switch(dest->format->BytesPerPixel)
	{
		CASE_BPP(2)
		CASE_BPP(3)
		CASE_BPP(4)
	default:
		tlog1 << (int)dest->format->BitsPerPixel << "bpp is not supported!\n";
		return NULL;
	}
#undef CASE_BPP
}

Uint8 * CSDL_Ext::getPxPtr(const SDL_Surface * const &srf, const int x, const int y)
{
	return (Uint8 *)srf->pixels + y * srf->pitch + x * srf->format->BytesPerPixel;
}

std::string CSDL_Ext::processStr(std::string str, std::vector<std::string> & tor)
{
	for (size_t i=0; (i<tor.size())&&(boost::find_first(str,"%s")); ++i)
	{
		boost::replace_first(str,"%s",tor[i]);
	}
	return str;
}

bool CSDL_Ext::isTransparent( SDL_Surface * srf, int x, int y )
{
	if (x < 0 || y < 0 || x >= srf->w || y >= srf->h)
		return true;

	if(srf->format->BytesPerPixel == 1)
	{
		return ((ui8*)srf->pixels)[x + srf->pitch * y]  == 0;
	}
	else
	{
		assert(!"isTransparent called with non-8bpp surface!");
	}
	return false;
}

void CSDL_Ext::VflipSurf(SDL_Surface * surf)
{
	char buf[4]; //buffer
	int bpp = surf->format->BytesPerPixel;
	for (int y=0; y<surf->h; ++y)
	{
		char * base = (char*)surf->pixels + y * surf->pitch;
		for (int x=0; x<surf->w/2; ++x)
		{
			memcpy(buf, base  + x * bpp, bpp);
			memcpy(base + x * bpp, base + (surf->w - x - 1) * bpp, bpp);
			memcpy(base + (surf->w - x - 1) * bpp, buf, bpp);
		}
	}
}

void CSDL_Ext::SDL_PutPixelWithoutRefresh(SDL_Surface *ekran, const int & x, const int & y, const Uint8 & R, const Uint8 & G, const Uint8 & B, Uint8 A /*= 255*/)
{
	Uint8 *p = getPxPtr(ekran, x, y);
	getPutterFor(ekran, false)(p, R, G, B);

	switch(ekran->format->BytesPerPixel)
	{
	case 2: Channels::px<2>::a.set(p, A); break;
	case 3: Channels::px<3>::a.set(p, A); break;
	case 4: Channels::px<4>::a.set(p, A); break;
	}
}

void CSDL_Ext::SDL_PutPixelWithoutRefreshIfInSurf(SDL_Surface *ekran, const int & x, const int & y, const Uint8 & R, const Uint8 & G, const Uint8 & B, Uint8 A /*= 255*/)
{
	const SDL_Rect & rect = ekran->clip_rect;

	if(x >= rect.x  &&  x < rect.w + rect.x
	&& y >= rect.y  &&  y < rect.h + rect.y)
		SDL_PutPixelWithoutRefresh(ekran, x, y, R, G, B, A);
}

BlitterWithRotationVal CSDL_Ext::getBlitterWithRotation(SDL_Surface *dest)
{
	switch(dest->format->BytesPerPixel)
	{
	case 2: return blitWithRotateClipVal<2>;
	case 3: return blitWithRotateClipVal<3>;
	case 4: return blitWithRotateClipVal<4>;
	default:
		tlog1 << (int)dest->format->BitsPerPixel << " bpp is not supported!!!\n";
		break;
	}

	assert(0);
	return NULL;
}

BlitterWithRotationVal CSDL_Ext::getBlitterWithRotationAndAlpha(SDL_Surface *dest)
{
	switch(dest->format->BytesPerPixel)
	{
	case 2: return blitWithRotateClipValWithAlpha<2>;
	case 3: return blitWithRotateClipValWithAlpha<3>;
	case 4: return blitWithRotateClipValWithAlpha<4>;
	default:
		tlog1 << (int)dest->format->BitsPerPixel << " bpp is not supported!!!\n";
		break;
	}

	assert(0);
	return NULL;
}

template<int bpp>
void CSDL_Ext::applyEffectBpp( SDL_Surface * surf, const SDL_Rect * rect, int mode )
{
	switch(mode)
	{
	case 0: //sepia
		{
			const int sepiaDepth = 20;
			const int sepiaIntensity = 30;

			for(int xp = rect->x; xp < rect->x + rect->w; ++xp)
			{
				for(int yp = rect->y; yp < rect->y + rect->h; ++yp)
				{
					Uint8 * pixel = (ui8*)surf->pixels + yp * surf->pitch + xp * surf->format->BytesPerPixel;
					int r = Channels::px<bpp>::r.get(pixel);
					int g = Channels::px<bpp>::g.get(pixel);
					int b = Channels::px<bpp>::b.get(pixel);
					int gray = 0.299 * r + 0.587 * g + 0.114 *b;

					r = g = b = gray;
					r = r + (sepiaDepth * 2);
					g = g + sepiaDepth;

					if (r>255) r=255;
					if (g>255) g=255;
					if (b>255) b=255;

					// Darken blue color to increase sepia effect
					b -= sepiaIntensity;

					// normalize if out of bounds
					if (b<0) b=0;

					Channels::px<bpp>::r.set(pixel, r);
					Channels::px<bpp>::g.set(pixel, g);
					Channels::px<bpp>::b.set(pixel, b);
				}
			}
		}
		break;
	case 1: //grayscale
		{
			for(int xp = rect->x; xp < rect->x + rect->w; ++xp)
			{
				for(int yp = rect->y; yp < rect->y + rect->h; ++yp)
				{
					Uint8 * pixel = (ui8*)surf->pixels + yp * surf->pitch + xp * surf->format->BytesPerPixel;

					int r = Channels::px<bpp>::r.get(pixel);
					int g = Channels::px<bpp>::g.get(pixel);
					int b = Channels::px<bpp>::b.get(pixel);

					int gray = 0.299 * r + 0.587 * g + 0.114 *b;
					vstd::amax(gray, 255);

					Channels::px<bpp>::r.set(pixel, gray);
					Channels::px<bpp>::g.set(pixel, gray);
					Channels::px<bpp>::b.set(pixel, gray);
				}
			}
		}
		break;
	default:
		throw std::runtime_error("Unsupported effect!");
	}
}

void CSDL_Ext::applyEffect( SDL_Surface * surf, const SDL_Rect * rect, int mode )
{
	switch(surf->format->BytesPerPixel)
	{
		case 2: applyEffectBpp<2>(surf, rect, mode); break;
		case 3: applyEffectBpp<3>(surf, rect, mode); break;
		case 4: applyEffectBpp<4>(surf, rect, mode); break;
	}
}

template<int bpp>
void scaleSurfaceFastInternal(SDL_Surface *surf, SDL_Surface *ret)
{
	const float factorX = float(surf->w) / float(ret->w),
	            factorY = float(surf->h) / float(ret->h);

	for(int y = 0; y < ret->h; y++)
	{
		for(int x = 0; x < ret->w; x++)
		{
			//coordinates we want to calculate
			int origX = floor(factorX * x),
			    origY = floor(factorY * y);

			// Get pointers to source pixels
			Uint8 *srcPtr = (Uint8*)surf->pixels + origY * surf->pitch + origX * bpp;
			Uint8 *destPtr = (Uint8*)ret->pixels + y * ret->pitch + x * bpp;

			memcpy(destPtr, srcPtr, bpp);
		}
	}
}

SDL_Surface * CSDL_Ext::scaleSurfaceFast(SDL_Surface *surf, int width, int height)
{
	if (!surf || !width || !height)
		return nullptr;

	//Same size? return copy - this should more be faster
	if (width == surf->w && height == surf->h)
		return copySurface(surf);

	SDL_Surface *ret = newSurface(width, height, surf);

	switch(surf->format->BytesPerPixel)
	{
		case 1: scaleSurfaceFastInternal<1>(surf, ret); break;
		case 2: scaleSurfaceFastInternal<2>(surf, ret); break;
		case 3: scaleSurfaceFastInternal<3>(surf, ret); break;
		case 4: scaleSurfaceFastInternal<4>(surf, ret); break;
	}
	return ret;
}

template<int bpp>
void scaleSurfaceInternal(SDL_Surface *surf, SDL_Surface *ret)
{
	const float factorX = float(surf->w - 1) / float(ret->w),
	            factorY = float(surf->h - 1) / float(ret->h);

	for(int y = 0; y < ret->h; y++)
	{
		for(int x = 0; x < ret->w; x++)
		{
			//coordinates we want to interpolate
			float origX = factorX * x,
			      origY = factorY * y;

			float x1 = floor(origX), x2 = floor(origX+1),
			      y1 = floor(origY), y2 = floor(origY+1);
			//assert( x1 >= 0 && y1 >= 0 && x2 < surf->w && y2 < surf->h);//All pixels are in range

			// Calculate weights of each source pixel
			float w11 = ((origX - x1) * (origY - y1));
			float w12 = ((origX - x1) * (y2 - origY));
			float w21 = ((x2 - origX) * (origY - y1));
			float w22 = ((x2 - origX) * (y2 - origY));
			//assert( w11 + w12 + w21 + w22 > 0.99 && w11 + w12 + w21 + w22 < 1.01);//total weight is ~1.0

			// Get pointers to source pixels
			Uint8 *p11 = (Uint8*)surf->pixels + int(y1) * surf->pitch + int(x1) * bpp;
			Uint8 *p12 = p11 + bpp;
			Uint8 *p21 = p11 + surf->pitch;
			Uint8 *p22 = p21 + bpp;
			// Calculate resulting channels
#define PX(X, PTR) Channels::px<bpp>::X.get(PTR)
			int resR = PX(r, p11) * w11 + PX(r, p12) * w12 + PX(r, p21) * w21 + PX(r, p22) * w22;
			int resG = PX(g, p11) * w11 + PX(g, p12) * w12 + PX(g, p21) * w21 + PX(g, p22) * w22;
			int resB = PX(b, p11) * w11 + PX(b, p12) * w12 + PX(b, p21) * w21 + PX(b, p22) * w22;
			int resA = PX(a, p11) * w11 + PX(a, p12) * w12 + PX(a, p21) * w21 + PX(a, p22) * w22;
			//assert(resR < 256 && resG < 256 && resB < 256 && resA < 256);
#undef PX
			Uint8 *dest = (Uint8*)ret->pixels + y * ret->pitch + x * bpp;
			Channels::px<bpp>::r.set(dest, resR);
			Channels::px<bpp>::g.set(dest, resG);
			Channels::px<bpp>::b.set(dest, resB);
			Channels::px<bpp>::a.set(dest, resA);
		}
	}
}

// scaling via bilinear interpolation algorithm.
// NOTE: best results are for scaling in range 50%...200%.
// And upscaling looks awful right now - should be fixed somehow
SDL_Surface * CSDL_Ext::scaleSurface(SDL_Surface *surf, int width, int height)
{
	if (!surf || !width || !height)
		return nullptr;

	if (surf->format->palette)
		return scaleSurfaceFast(surf, width, height);

	//Same size? return copy - this should more be faster
	if (width == surf->w && height == surf->h)
		return copySurface(surf);

	SDL_Surface *ret = newSurface(width, height, surf);

	switch(surf->format->BytesPerPixel)
	{
	case 2: scaleSurfaceInternal<2>(surf, ret); break;
	case 3: scaleSurfaceInternal<3>(surf, ret); break;
	case 4: scaleSurfaceInternal<4>(surf, ret); break;
	}

	return ret;
}

void CSDL_Ext::blitSurface( SDL_Surface * src, SDL_Rect * srcRect, SDL_Surface * dst, SDL_Rect * dstRect )
{
	if (dst != screen)
	{
		SDL_BlitSurface(src, srcRect, dst, dstRect);
	}
	else
	{
		SDL_Rect betterDst;
		if (dstRect)
		{
			betterDst = *dstRect;
		}
		else
		{
			betterDst = Rect(0, 0, dst->w, dst->h);
		}

		SDL_BlitSurface(src, srcRect, dst, &betterDst);
	}
}

void CSDL_Ext::fillRect( SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color )
{
	SDL_Rect newRect;
	if (dstrect)
	{
		newRect = *dstrect;
	}
	else
	{
		newRect = Rect(0, 0, dst->w, dst->h);
	}
	SDL_FillRect(dst, &newRect, color);
}

void CSDL_Ext::fillTexture(SDL_Surface *dst, SDL_Surface * src)
{
	SDL_Rect srcRect;
	SDL_Rect dstRect;

	SDL_GetClipRect(src, &srcRect);
	SDL_GetClipRect(dst, &dstRect);

	for (int y=dstRect.y; y<dstRect.h; y+=srcRect.h)
	{
		for (int x=dstRect.x; x<dstRect.w; x+=srcRect.w)
		{
			Rect currentDest(x, y, srcRect.w, srcRect.h);
			SDL_BlitSurface(src, &srcRect, dst, &currentDest);
		}
	}
}

std::string CSDL_Ext::trimToFit(std::string text, int widthLimit, EFonts font)
{
	int widthSoFar = 0;
	for(auto i = text.begin(); i != text.end(); i++)
	{
		widthSoFar += graphics->fonts[font]->getCharWidth(*i);
		if(widthSoFar > widthLimit)
		{
			//remove all characteres past limit
			text.erase(i, text.end());
			break;
		}
	}

	return text;
}

template SDL_Surface * CSDL_Ext::createSurfaceWithBpp<2>(int, int);
template SDL_Surface * CSDL_Ext::createSurfaceWithBpp<3>(int, int);
template SDL_Surface * CSDL_Ext::createSurfaceWithBpp<4>(int, int);