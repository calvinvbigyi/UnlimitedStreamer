#include "SpotifyEmbeddedDomain.h"
#include "../xavier/SpotifyDomain/appkey.c"
#include "../xavier/SpotifyDomain/jukebox/playtrack.c"
#include "../xavier/SpotifyDomain/jukebox/audio.h"
#include "../xavier/SpotifyDomain/jukebox/alsa-audio.c"
#include "../xavier/SpotifyDomain/jukebox/audio.c"
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

using namespace std;

struct SEDInfo
{
	char * _userName, * _passWord, * _trackUriStr;
	bool _repeatFlag;
	Task * pTask;
};

struct SEDListInfo
{
	vector<string> _trackURIs;
	char * _userName, * _passWord;
	bool _repeatFlag, _shuffleFlag;
};

SpotifyEmbeddedDomain::SpotifyEmbeddedDomain(string userName, string passWord)
{
	_userName = userName;
	_passWord = passWord;
}

SpotifyEmbeddedDomain::~SpotifyEmbeddedDomain()
{

}

void SpotifyEmbeddedDomain::taskProcPlay(Task * pTask)
{
	struct SEDInfo sedInfo, * pSEDInfo;
	pSEDInfo = (struct SEDInfo * ) pTask->getParameters();
	sedInfo = * pSEDInfo;
	delete pSEDInfo;

	libspotify_play(sedInfo._userName, sedInfo._passWord, sedInfo._trackUriStr, pTask);
}

void SpotifyEmbeddedDomain::taskProcPlayList(Task * pTask)
{
	struct SEDListInfo sedListInfo, * pSEDListInfo;
	pSEDListInfo = (struct SEDListInfo *) pTask->getParameters();
	sedListInfo = * pSEDListInfo;
	delete pSEDListInfo;

	bool repeatFlag = sedListInfo._repeatFlag;
	bool shuffleFlag = sedListInfo._shuffleFlag;
	vector<string> tracks = sedListInfo._trackURIs;
	printf("trackSize: %lu\n", tracks.size());
	int songIndex = 0;
	int playStatus = 1;
	while (1)
	{
		printf("songIndex: %d\n", songIndex);
		if ((songIndex == (int) tracks.size() - 1) && !repeatFlag)
		{
			break;
		}
		if (shuffleFlag)
		{
			tracks = shuffleTrackList(tracks);
		}
		if (playStatus != 0)
		{
			playStatus = libspotify_play(sedListInfo._userName, sedListInfo._passWord, (char *) tracks.at(songIndex).c_str(), pTask);
		}
		if (repeatFlag)
		{
			songIndex = (songIndex + 1) % tracks.size();
		}
		else
		{
			songIndex++;
		}

	}

}


void SpotifyEmbeddedDomain::playFullTrack(string trackUri, bool repeatFlag)
{
	if (_spPlayTask != NULL)
	{
		_spPlayTask->stop(true);
		delete _spPlayTask;
		_spPlayTask = NULL;
	}

	struct SEDInfo * pSEDInfo;

	pSEDInfo = new SEDInfo();
	pSEDInfo->_userName = (char *) _userName.c_str();
	pSEDInfo->_passWord = (char *) _passWord.c_str();
	pSEDInfo->_trackUriStr = (char *) trackUri.c_str();
	pSEDInfo->_repeatFlag = repeatFlag;

	_spPlayTask = new Task();
	_spPlayTask->start(taskProcPlay, pSEDInfo);
}

void SpotifyEmbeddedDomain::playListOfTracks(vector<string>& trackList, bool repeatFlag, bool shuffleFlag)
{
	if (_spListTask != NULL)
	{
		_spListTask->stop(true);
		delete _spListTask;
		_spListTask = NULL;
	}

	struct SEDListInfo * pSEDListInfo;

	pSEDListInfo = new SEDListInfo();
	pSEDListInfo->_trackURIs = trackList;
	pSEDListInfo->_repeatFlag = repeatFlag;
	pSEDListInfo->_shuffleFlag = shuffleFlag;
	pSEDListInfo->_userName = (char *) _userName.c_str();
	pSEDListInfo->_passWord = (char *) _passWord.c_str();

	_spListTask = new Task();
	_spListTask->start(taskProcPlayList, pSEDListInfo);

}

void SpotifyEmbeddedDomain::pauseTrack()
{
	pause_track();
}

void SpotifyEmbeddedDomain::resumeTrack()
{
	resume_track();
}

void SpotifyEmbeddedDomain::stopTrack()
{
	if (_spPlayTask != NULL)
	{
		_spPlayTask->stop(true);
		delete _spPlayTask;
		_spPlayTask = NULL;
	}

	if (_spListTask != NULL)
	{
		_spListTask->stop(true);
		delete _spListTask;
		_spListTask = NULL;
	}
}


vector<string> SpotifyEmbeddedDomain::shuffleTrackList(vector<string> &trackList)
{
	vector<string> newTrackList;
	for (vector<string>::iterator iter = trackList.begin(); iter != trackList.end(); iter++)
	{
		cout << *iter << endl;
	}
	random_shuffle ( trackList.begin(), trackList.end() );
	newTrackList = trackList;
	for (vector<string>::iterator iter = trackList.begin(); iter != trackList.end(); iter++)
	{
		cout << *iter << endl;
	}
	return newTrackList;
}
