#include "StdInc.h"
#include "CGeneralTextHandler.h"

#include "../lib/VCMI_Lib.h"
#include "CLodHandler.h"
#include "GameConstants.h"

/*
 * CGeneralTextHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

std::string readTo(const std::string &in, int &it, char end)
{
	int pom = it;
	int last = in.find_first_of(end,it);
	it+=(1+last-it);
	return in.substr(pom,last-pom);
}

void trimQuotation(std::string &op)
{
	if(op.length() && op[0] == '\"' && op[op.size()-1] == '\"')
		op = op.substr(1,op.size()-2);
}

void CGeneralTextHandler::load()
{
	std::string buf1 = bitmaph->getTextFile("ZELP.TXT");
	int itr=0, eol=-1, eolnext=-1, pom;
	eolnext = buf1.find_first_of('\r',itr);
	while(itr<buf1.size())
	{
		eol = eolnext; //end of this line
		eolnext = buf1.find_first_of('\r',eol+1); //end of the next line
		pom=buf1.find_first_of('\t',itr); //upcoming tab
		if(eol<0 || pom<0)
			break;
		if(pom>eol) //in current line there is not tab
			zelp.push_back(std::pair<std::string,std::string>());
		else
		{
			zelp.push_back
				(std::pair<std::string,std::string>
				(buf1.substr(itr,pom-itr),
				buf1.substr(pom+1,eol-pom-1)));
			boost::algorithm::replace_all(zelp[zelp.size()-1].first,"\t","");
			boost::algorithm::replace_all(zelp[zelp.size()-1].second,"\t","");
			trimQuotation(zelp.back().second);
		}
		itr=eol+2;
	}
	std::string buf = bitmaph->getTextFile("VCDESC.TXT");
	int andame = buf.size();
	int i=0; //buf iterator
	for(int gg=0; gg<14; ++gg)
	{
		int befi=i;
		for(; i<andame; ++i)
		{
			if(buf[i]=='\r')
				break;
		}
		victoryConditions[gg] = buf.substr(befi, i-befi);
		i+=2;
	}
	buf = bitmaph->getTextFile("LCDESC.TXT");
	andame = buf.size();
	i=0; //buf iterator
	for(int gg=0; gg<4; ++gg)
	{
		int befi=i;
		for(; i<andame; ++i)
		{
			if(buf[i]=='\r')
				break;
		}
		lossCondtions[gg] = buf.substr(befi, i-befi);
		i+=2;
	}

	hTxts.resize(GameConstants::HEROES_QUANTITY);

	buf = bitmaph->getTextFile("HEROSPEC.TXT");
	i=0;
	std::string dump;
	for(int iii=0; iii<2; ++iii)
	{
		loadToIt(dump,buf,i,3);
	}
	for (int iii=0;iii<hTxts.size();iii++)
	{
		loadToIt(hTxts[iii].bonusName,buf,i,4);
		loadToIt(hTxts[iii].shortBonus,buf,i,4);
		loadToIt(hTxts[iii].longBonus,buf,i,3);
		trimQuotation(hTxts[iii].longBonus);
	}

	buf = bitmaph->getTextFile("HEROBIOS.TXT");
	i=0;
	for (int iii=0;iii<hTxts.size();iii++)
	{
		loadToIt(hTxts[iii].biography,buf,i,3);
		trimQuotation(hTxts[iii].biography);
	}

	int it;
	buf = bitmaph->getTextFile("BLDGNEUT.TXT");
	andame = buf.size(), it=0;

	for(int b=0;b<15;b++)
	{
		std::string name = readTo(buf,it,'\t'),
			description = readTo(buf,it,'\n');
		for(int fi=0;fi<GameConstants::F_NUMBER;fi++)
		{
			buildings[fi][b].first = name;
			buildings[fi][b].second = description;
		}
	}
	buf1 = readTo(buf,it,'\n');buf1 = readTo(buf,it,'\n');buf1 = readTo(buf,it,'\n');//silo,blacksmith,moat - useless???
	//shipyard with the ship
	std::string name = readTo(buf,it,'\t'),
		description = readTo(buf,it,'\n');
	for(int fi=0;fi<GameConstants::F_NUMBER;fi++)
	{
		buildings[fi][20].first = name;
		buildings[fi][20].second = description;
	}

	for(int fi=0;fi<GameConstants::F_NUMBER;fi++)
	{
		buildings[fi][16].first = readTo(buf,it,'\t'),
			buildings[fi][16].second = readTo(buf,it,'\n');
	}
	/////done reading "BLDGNEUT.TXT"******************************

	buf = bitmaph->getTextFile("BLDGSPEC.TXT");
	andame = buf.size(), it=0;
	for(int f=0;f<GameConstants::F_NUMBER;f++)
	{
		for(int b=0;b<9;b++)
		{
			buildings[f][17+b].first = readTo(buf,it,'\t');
			buildings[f][17+b].second = readTo(buf,it,'\n');
		}
		buildings[f][26].first = readTo(buf,it,'\t');
		buildings[f][26].second = readTo(buf,it,'\n');
		buildings[f][15].first = readTo(buf,it,'\t'); //resource silo
		buildings[f][15].second = readTo(buf,it,'\n');//resource silo
	}
	/////done reading BLDGSPEC.TXT*********************************

	buf = bitmaph->getTextFile("DWELLING.TXT");
	andame = buf.size(), it=0;
	for(int f=0;f<GameConstants::F_NUMBER;f++)
	{
		for(int b=0;b<14;b++)
		{
			buildings[f][30+b].first = readTo(buf,it,'\t');
			buildings[f][30+b].second = readTo(buf,it,'\n');
		}
	}

	//remove prceeding / trailing whitespaces nad quoation marks from buildings descriptions
	for(std::map<int, std::map<int, std::pair<std::string, std::string> > >::iterator i = buildings.begin(); i != buildings.end(); i++)
	{
		for(std::map<int, std::pair<std::string, std::string> >::iterator j = i->second.begin(); j != i->second.end(); j++)
		{
			std::string &str = j->second.second;
			boost::algorithm::trim(str);
			trimQuotation(str);
		}
	}

	buf = bitmaph->getTextFile("TCOMMAND.TXT");
	itr=0;
	while(itr<buf.length()-1)
	{
		std::string tmp;
		loadToIt(tmp, buf, itr, 3);
		tcommands.push_back(tmp);
	}

	buf = bitmaph->getTextFile("HALLINFO.TXT");
	itr=0;
	while(itr<buf.length()-1)
	{
		std::string tmp;
		loadToIt(tmp, buf, itr, 3);
		hcommands.push_back(tmp);
	}

	buf = bitmaph->getTextFile("CASTINFO.TXT");
	itr=0;
	while(itr<buf.length()-1)
	{
		std::string tmp;
		loadToIt(tmp, buf, itr, 3);
		fcommands.push_back(tmp);
	}

	std::istringstream ins, namess;
	ins.str(bitmaph->getTextFile("TOWNTYPE.TXT"));
	namess.str(bitmaph->getTextFile("TOWNNAME.TXT"));
	int si=0;
	char bufname[75];
	while (!ins.eof())
	{
		ins.getline(bufname,50);
		townTypes.push_back(std::string(bufname).substr(0,strlen(bufname)-1));
		townNames.resize(si+1);

		for (int i=0; i<GameConstants::NAMES_PER_TOWN; i++)
		{
			namess.getline(bufname,50);
			townNames[si].push_back(std::string(bufname).substr(0,strlen(bufname)-1));
		}
		si++;
	}

	tlog5 << "\t\tReading OBJNAMES \n";
	buf = bitmaph->getTextFile("OBJNAMES.TXT");
	it=0; //hope that -1 will not break this
	while (it<buf.length()-1)
	{
		std::string nobj;
		loadToIt(nobj, buf, it, 3);
		if(nobj.size() && (nobj[nobj.size()-1]==(char)10 || nobj[nobj.size()-1]==(char)13 || nobj[nobj.size()-1]==(char)9))
		{
			nobj = nobj.substr(0, nobj.size()-1);
		}
		names.push_back(nobj);
	}

	tlog5 << "\t\tReading ADVEVENT \n";
	buf = bitmaph->getTextFile("ADVEVENT.TXT");
	it=0;
	std::string temp;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		if (temp[0]=='\"')
		{
			temp = temp.substr(1,temp.length()-2);
		}
		boost::algorithm::replace_all(temp,"\"\"","\"");
		advobtxt.push_back(temp);
	}

	tlog5 << "\t\tReading XTRAINFO \n";
	buf = bitmaph->getTextFile("XTRAINFO.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		xtrainfo.push_back(temp);
	}

	tlog5 << "\t\tReading MINENAME \n";
	buf = bitmaph->getTextFile("MINENAME.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		mines.push_back(std::pair<std::string,std::string>(temp,""));
	}

	tlog5 << "\t\tReading MINEEVNT \n";
	buf = bitmaph->getTextFile("MINEEVNT.TXT");
	it=0;
	i=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		temp = temp.substr(1,temp.length()-2);
		if(i < mines.size())
			mines[i++].second = temp;
		else
			tlog2 << "Warning - too much entries in MINEEVNT. Omitting this one: " << temp << std::endl;
	}

	tlog5 << "\t\tReading RESTYPES \n";
	buf = bitmaph->getTextFile("RESTYPES.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		restypes.push_back(temp);
	}

	tlog5 << "\t\tReading TERRNAME \n";
	buf = bitmaph->getTextFile("TERRNAME.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		terrainNames.push_back(temp);
	}

	tlog5 << "\t\tReading RANDSIGN \n";
	buf = bitmaph->getTextFile("RANDSIGN.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		randsign.push_back(temp);
	}	

	tlog5 << "\t\tReading ZCRGN1 \n";
	buf = bitmaph->getTextFile("ZCRGN1.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		creGens.push_back(temp);
	}

	tlog5 << "\t\tReading CRGN4 \n";
	buf = bitmaph->getTextFile("CRGEN4.TXT");
	it=0;
	while (it<buf.length()-1)
	{
		loadToIt(temp,buf,it,3);
		creGens4.push_back(temp);
	}

	buf = bitmaph->getTextFile("GENRLTXT.TXT");
	std::string tmp;
	andame = buf.size();
	i=0; //buf iterator
	for(; i<andame; ++i)
	{
		if(buf[i]=='\r')
			break;
	}

	i+=2;
	std::string buflet;
	for(int jj=0; jj<764; ++jj)
	{
		loadToIt(buflet, buf, i, 2);
		trimQuotation(buflet);
		boost::algorithm::replace_all(buflet,"\"\"","\"");
		allTexts.push_back(buflet);
	}

	std::string  stro = bitmaph->getTextFile("Overview.txt");
	itr=0;
	while(itr<stro.length()-1)
	{
		loadToIt(tmp, stro, itr, 3);
		trimQuotation(tmp);
		overview.push_back(tmp);
	}

	std::string  strc = bitmaph->getTextFile("PLCOLORS.TXT");
	itr=0;
	while(itr<strc.length()-1)
	{
		loadToIt(tmp, strc, itr, 3);
		colors.push_back(tmp);
		tmp[0] = toupper(tmp[0]);
		capColors.push_back(tmp);
	}

	std::string  strs = bitmaph->getTextFile("ARRAYTXT.TXT");

	itr=0;
	while(itr<strs.length()-1)
	{
		loadToIt(tmp, strs, itr, 3);
		trimQuotation(tmp);
		arraytxt.push_back(tmp);
	}

	itr = 0;
	std::string strin = bitmaph->getTextFile("PRISKILL.TXT");
	for(int hh=0; hh<4; ++hh)
	{
		loadToIt(tmp, strin, itr, 3);
		primarySkillNames.push_back(tmp);
	}

	itr = 0;
	strin = bitmaph->getTextFile("JKTEXT.TXT");
	for(int hh=0; hh<45; ++hh)
	{
		loadToIt(tmp, strin, itr, 3);
		trimQuotation(tmp); 
		jktexts.push_back(tmp);
	}

	itr = 0;
	strin = bitmaph->getTextFile("TVRNINFO.TXT");
	for(int hh=0; hh<8; ++hh)
	{
		loadToIt(tmp, strin, itr, 3);
		tavernInfo.push_back(tmp);
	}

	itr = 0;
	strin = bitmaph->getTextFile("TURNDUR.TXT");
	for(int hh=0; hh<11; ++hh)
	{
		loadToIt(tmp, strin, itr, 3);
		turnDurations.push_back(tmp);
	}

	itr = 0;
	strin = bitmaph->getTextFile("HEROSCRN.TXT");
	for(int hh=0; hh<33; ++hh)
	{
		loadToIt(tmp, strin, itr, 3);
		heroscrn.push_back(tmp);
	}

	itr = 0;
	strin = bitmaph->getTextFile("ARTEVENT.TXT");
	for(; itr<strin.size();)
	{
		loadToIt(tmp, strin, itr, 2);
	//	boost::algorithm::trim(tmp);
		trimQuotation(tmp);
		boost::algorithm::replace_all(tmp,"\"\"","\"");
		artifEvents.push_back(tmp);
	}

	buf = bitmaph->getTextFile("SSTRAITS.TXT");
	it=0;

	for(int i=0; i<2; ++i)
		loadToIt(dump,buf,it,3);

	skillName.resize(GameConstants::SKILL_QUANTITY);
	skillInfoTexts.resize(GameConstants::SKILL_QUANTITY);
	for (int i=0; i<GameConstants::SKILL_QUANTITY; i++)
	{
		skillInfoTexts[i].resize(3);
		loadToIt(skillName[i],buf,it,4);
		loadToIt(skillInfoTexts[i][0],buf,it,4);
		loadToIt(skillInfoTexts[i][1],buf,it,4);
		loadToIt(skillInfoTexts[i][2],buf,it,3);
		for(int j = 0; j < 3; j++)
			trimQuotation(skillInfoTexts[i][j]);
	}
	buf = bitmaph->getTextFile("SKILLLEV.TXT");
	it=0;
	for(int i=0; i<6; ++i)
	{
		std::string buffo;
		loadToIt(buffo,buf,it,3);
		levels.push_back(buffo);
	}

	buf = bitmaph->getTextFile ("SEERHUT.TXT");
	it = 0;
	loadToIt (dump, buf, it, 3);
	loadToIt (dump, buf, it, 4); //dump description
	seerEmpty.resize(6);
	for (i = 0; i < 5; ++i)
	{
		loadToIt(seerEmpty[i], buf, it, 4);
		trimQuotation (seerEmpty[i]);
	}
	loadToIt (seerEmpty[5], buf, it, 3);
	trimQuotation (seerEmpty[5]);
	int j,k;
	quests.resize(10);
	for (i = 0; i < 9; ++i) //9 types of quests
	{
		quests[i].resize(5);
		for (j = 0; j < 5; ++j)
		{
			loadToIt (dump, buf, it, 4); //front description
			quests[i][j].resize(6);
			for (k = 0; k < 5; ++k)
			{
				loadToIt (quests[i][j][k], buf, it, 4);
				trimQuotation (quests[i][j][k]);
			}
			loadToIt (quests[i][j][5], buf, it, 3);
			trimQuotation (quests[i][j][5]);
		}
	}
	quests[9].resize(1);
	quests[9][0].resize(6);

	for (k = 0; k < 5; ++k) //Time limit
	{
		loadToIt (quests[9][0][k], buf, it, 4);
	}
	loadToIt (quests[9][0][k], buf, it, 3);
	for (i = 0; i < 2; ++i) //gap description
		loadToIt(dump,buf,it,3);
	seerNames.resize(48);
	for (i = 0; i < 48; ++i)
		loadToIt(seerNames[i], buf, it, 3);

	buf = bitmaph->getTextFile("TENTCOLR.TXT");
	itr=0;
	while(itr<buf.length()-1)
	{
		std::string tmp;
		loadToIt(tmp, buf, itr, 3);
		tentColors.push_back(tmp);
	}

	//campaigns
	buf = bitmaph->getTextFile ("CAMPTEXT.TXT");
	it = 0;
	loadToIt (dump, buf, it, 3); //comment
	std::string nameBuf;
	do //map names
	{
		loadToIt(nameBuf, buf, it, 3);
		if(nameBuf.size())
		{
			campaignMapNames.push_back(nameBuf);
		}
	} while (nameBuf.size());

	campaignRegionNames.resize(campaignMapNames.size()); //allocating space
	for(int g=0; g<campaignMapNames.size(); ++g) //region names
	{
		do //dump comments and empty lines
		{
			loadToIt(nameBuf, buf, it, 3);
		} while (!nameBuf.size() || nameBuf[0] != '/');
		do //actual names
		{
			loadToIt(nameBuf, buf, it, 3);
			if(nameBuf.size())
			{
				campaignRegionNames[g].push_back(nameBuf);
			}
		} while (nameBuf.size());
	}

	buf = bitmaph->getTextFile ("ZCREXP.TXT");
	it = 0;
	loadToIt (dump, buf, it, 3); //comment
	for (int i = 0; i < 459; ++i) //some texts seem to be empty
	{
		loadToIt(dump, buf, it, 4); //description, usually useless
		loadToIt(nameBuf, buf, it, 3);
		zcrexp.push_back(nameBuf);
	}

	std::string threatLevelDir = GameConstants::DATA_DIR + "/config/threatlevel.txt";
	std::ifstream ifs(threatLevelDir.c_str(), std::ios::in | std::ios::binary);
	getline(ifs, buf); //skip 1st line
	for (int i = 0; i < 13; ++i)
	{
		getline(ifs, buf);
		threat.push_back(buf);
	}
}


std::string CGeneralTextHandler::getTitle(const std::string & text)
{
	std::string ret;
	int i=0;
	while ((text[i++]!='{'));
	while ((text[i]!='}') && (i<text.length()))
		ret+=text[i++];
	return ret;
}

std::string CGeneralTextHandler::getDescr(const std::string & text)
{
	std::string ret;
	int i=0;
	while ((text[i++]!='}'));
	i+=2;
	while ((text[i]!='"') && (i<text.length()))
		ret+=text[i++];
	return ret;
}

CGeneralTextHandler::CGeneralTextHandler()
{

}
