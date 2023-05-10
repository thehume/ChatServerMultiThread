﻿#pragma comment(lib, "winmm.lib" )
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <dbghelp.h>
#include <list>
#include <random>
#include <locale.h>
#include <process.h>
#include <stdlib.h>
#include <iostream>
#include <unordered_map>
#include "log.h"
#include "ringbuffer.h"
#include "MemoryPoolBucket.h"
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CommonProtocol.h"
#include "CNetServer.h"
#include "ChatServer.h"

using namespace std;

CChatServer::CChatServer()
{
	ShutDownFlag = false;
	lastTime = 0;
	pNetServer = NULL;
	hJobEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (hJobEvent == NULL)
	{
		CrashDump::Crash();
	}
}


void CChatServer::CS_CHAT_RES_LOGIN(INT64 SessionID, BYTE Status, INT64 AccountNo)
{
	WORD Type = en_PACKET_CS_CHAT_RES_LOGIN;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << Status;
	*pPacket << AccountNo;

	pNetServer->sendPacket(SessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}
void CChatServer::CS_CHAT_RES_SECTOR_MOVE(INT64 SessionID, INT64 AccountNo, WORD SectorX, WORD SectorY)
{
	WORD Type = en_PACKET_CS_CHAT_RES_SECTOR_MOVE;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << AccountNo;
	*pPacket << SectorX;
	*pPacket << SectorY;
	                  
	pNetServer->sendPacket(SessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}


void CChatServer::CS_CHAT_RES_MESSAGE(CSessionSet* SessionSet, INT64 AccountNo, st_UserName ID, st_UserName Nickname, WORD MessageLen, st_Message& Message)
{
	WORD Type = en_PACKET_CS_CHAT_RES_MESSAGE;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << AccountNo;
	*pPacket << ID;
	*pPacket << Nickname;
	*pPacket << MessageLen;
	*pPacket << Message;
	/*
	FILE* fp;
	if (!fopen_s(&fp, "CHAT_MESSAGE", "at"))
	{
		fwprintf_s(fp, L"send len : %d, chat message : %s\n", Message.len, Message.msg);
		fclose(fp);
	}
	*/

	pNetServer->sendPacket(SessionSet, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}

bool CChatServer::packetProc_CS_CHAT_REQ_LOGIN(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//------------------------------------------------------------
	// 채팅서버 로그인 요청
	//
	//	{
	//		WORD	Type
	//
	//		INT64	AccountNo
	//		WCHAR	ID[20]				// null 포함
	//		WCHAR	Nickname[20]		// null 포함
	//		char	SessionKey[64];		// 인증토큰
	//	}
	//
	//------------------------------------------------------------
	INT64 AccountNo;
	st_UserName ID;
	st_UserName Nickname;
	st_SessionKey SessionKey;
	*pPacket >> AccountNo >> ID >> Nickname >> SessionKey;
	
	pPlayer->AccountNo = AccountNo;
	memcpy(&pPlayer->ID, ID.name, sizeof(st_UserName));
	memcpy(&pPlayer->Nickname, Nickname.name, sizeof(st_UserName));
	memcpy(&pPlayer->sessionKey, SessionKey.sessionKey, sizeof(st_SessionKey));

	//응답패킷 보내기.
	CS_CHAT_RES_LOGIN(pPlayer->sessionID, TRUE, AccountNo);

	return true;
}
bool CChatServer::packetProc_CS_CHAT_REQ_SECTOR_MOVE(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//------------------------------------------------------------
	// 채팅서버 섹터 이동 요청
	//
	//	{
	//		WORD	Type
	//
	//		INT64	AccountNo
	//		WORD	SectorX
	//		WORD	SectorY
	//	}
	//
	//------------------------------------------------------------
	INT64 AccountNo;
	WORD SectorX;
	WORD SectorY;

	*pPacket >> AccountNo >> SectorX >> SectorY;

	if (pPlayer->AccountNo != AccountNo)
	{
		systemLog(L"WRONG ACCOUNT NUM SECTORMOVE", dfLOG_LEVEL_DEBUG, L"AccountNo : %lld", AccountNo);
		return false;
	}

	
	if (SectorX >= dfSECTOR_MAX_X || SectorX < 0 || SectorY >= dfSECTOR_MAX_Y || SectorY < 0)
	{
		systemLog(L"WRONG SECTOR POS", dfLOG_LEVEL_DEBUG, L" X %uh:  Y : %uh", SectorX, SectorY);
		return false;
	}
	


	//현재섹터에서 나 삭제
	if (pPlayer->sectorPos.sectorX >= 0 && pPlayer->sectorPos.sectorX < dfSECTOR_MAX_X && pPlayer->sectorPos.sectorY >= 0 && pPlayer->sectorPos.sectorY < dfSECTOR_MAX_Y)
	{
		sector_RemoveCharacter(pPlayer);
	}

	//나의 섹터정보 업데이트
	pPlayer->sectorPos.sectorX = SectorX;
	pPlayer->sectorPos.sectorY = SectorY;

	//바뀔 섹터에 나 넣어주기
	sector_AddCharacter(pPlayer);

	//업데이트된 섹터 메시지 보내주기
	CS_CHAT_RES_SECTOR_MOVE(pPlayer->sessionID, AccountNo, SectorX, SectorY);
	
	MoveCount++;
	return true;
}


bool CChatServer::packetProc_CS_CHAT_REQ_MESSAGE(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//------------------------------------------------------------
	// 채팅서버 채팅보내기 요청
	//
	//	{
	//		WORD	Type
	//
	//		INT64	AccountNo
	//		WORD	MessageLen
	//		WCHAR	Message[MessageLen / 2]		// null 미포함
	//	}
	//
	//------------------------------------------------------------
	INT64 AccountNo;
	WORD MessageLen;
	st_Message Message;
	*pPacket >> AccountNo >> MessageLen;
	Message.len = MessageLen;
	*pPacket >> Message;

	if (pPlayer->AccountNo != AccountNo)
	{
		systemLog(L"WRONG ACCOUNT NUM MESSAGE", dfLOG_LEVEL_DEBUG, L"AccountNo : %lld", AccountNo);
		return false;
	}

	CSessionSet sessionSet;
	makeSessionSet_AroundMe(pPlayer, &sessionSet);

	CS_CHAT_RES_MESSAGE(&sessionSet, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

	MsgCount++;
	return true;

}
bool CChatServer::packetProc_CS_CHAT_REQ_HEARTBEAT(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//------------------------------------------------------------
	// 하트비트
	//
	//	{
	//		WORD		Type
	//	}
	//
	//
	// 클라이언트는 이를 30초마다 보내줌.
	// 서버는 40초 이상동안 메시지 수신이 없는 클라이언트를 강제로 끊어줘야 함.
	//------------------------------------------------------------	

	return true;

}


bool CChatServer::PacketProc(st_Player* pPlayer, WORD PacketType, CPacket* pPacket, INT64 SessionID)
{
	switch (PacketType)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		return packetProc_CS_CHAT_REQ_LOGIN(pPlayer, pPacket, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		return packetProc_CS_CHAT_REQ_SECTOR_MOVE(pPlayer, pPacket, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		return packetProc_CS_CHAT_REQ_MESSAGE(pPlayer, pPacket, SessionID);
		break;

	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		return packetProc_CS_CHAT_REQ_HEARTBEAT(pPlayer, pPacket, SessionID);
		break;

	default:
		return false;
	}
}


DWORD WINAPI CChatServer::LogicThread(CChatServer* pChatServer)
{
	st_JobItem jobItem;
	WORD packetType;
	while (!pChatServer->ShutDownFlag)
	{
		//시간 쟤서 약 1초마다 모든세션의 lastPacket 확인 -> 40초가 지났다면 그세션끊기
		ULONGLONG curTime = GetTickCount64();
		if (curTime - pChatServer->lastTime > 1000)
		{
			pChatServer->Interval = curTime - pChatServer->lastTime;

			pChatServer->JobCount = pChatServer->Temp_JobCount;
			pChatServer->NumOfWFSO = pChatServer->Temp_NumOfWFSO;

			pChatServer->Temp_JobCount = 0;
			pChatServer->Temp_NumOfWFSO = 0;

			pChatServer->lastTime = curTime;
			st_Session* pSession;
			for (auto iter = pChatServer->PlayerList.begin(); iter != pChatServer->PlayerList.end(); iter++)
			{
				st_Player& player = *iter->second;
				if (player.isValid == FALSE)
				{
					continue;
				}
				if (curTime - player.lastTime > 40000)
				{
					if (pChatServer->pNetServer->findSession(player.sessionID, &pSession) == true)
					{
						systemLog(L"TimeOut", dfLOG_LEVEL_DEBUG, L"over time : %lld", curTime - player.lastTime);
						pChatServer->pNetServer->disconnectSession(pSession);
						if (InterlockedDecrement(&pSession->IOcount) == 0)
						{
							pChatServer->pNetServer->releaseSession(player.sessionID);
						}
					}
				}
			}

		}

		while (pChatServer->JobQueue.Dequeue(&jobItem) == true)
		{
			pChatServer->Temp_JobCount++;
			pChatServer->Temp_JobCountperCycle++;

			INT64 JobType = jobItem.JobType;
			INT64 sessionID = jobItem.SessionID;
			CPacket* pPacket = jobItem.pPacket;

			switch (JobType)
			{
			case en_JOB_ON_CLIENT_JOIN:
			{
				//여기 플레이어리스트에 넣어주는게 필요함
				st_Player* pNewPlayer;
				pChatServer->PlayerPool.mAlloc(&pNewPlayer);
				pNewPlayer->isValid = true;
				pNewPlayer->AccountNo = 0;
				wcscpy_s(pNewPlayer->ID.name, L"NULL");
				wcscpy_s(pNewPlayer->Nickname.name, L"NULL");
				pNewPlayer->sectorPos.sectorX = 65535;
				pNewPlayer->sectorPos.sectorY = 65535;
				pNewPlayer->sessionID = sessionID;
				pNewPlayer->lastTime = GetTickCount64();

				pChatServer->PlayerList.insert(make_pair(sessionID, pNewPlayer));
				break;
			}

			case en_JOB_ON_CLIENT_LEAVE:
			{
				//여기 플레이어리스트에서 지워주는게 필요함
				auto item = pChatServer->PlayerList.find(sessionID);
				if (item != pChatServer->PlayerList.end())
				{
					st_Player* pPlayer = item->second;
					st_SectorPos& sectorPos = item->second->sectorPos;
					if (sectorPos.sectorX < dfSECTOR_MAX_X && sectorPos.sectorX >= 0 && sectorPos.sectorY < dfSECTOR_MAX_Y && sectorPos.sectorY >= 0)
					{
						pChatServer->sector_RemoveCharacter(item->second);
					}
					pChatServer->PlayerList.erase(item);
					pChatServer->PlayerPool.mFree(pPlayer);
				}
				break;
			}

			case en_JOB_ON_RECV:
			{
				*pPacket >> packetType;

				auto item = pChatServer->PlayerList.find(sessionID);
				if (item == pChatServer->PlayerList.end())
				{
					if (pPacket->subRef() == 0)
					{
						CPacket::mFree(pPacket);
					}
					break;
				}

				st_Player& player = *item->second;
				if (player.sessionID != jobItem.SessionID)
				{
					if (pPacket->subRef() == 0)
					{
						CPacket::mFree(pPacket);
					}
					break; 
				}

				if (player.isValid == FALSE)
				{
					if (pPacket->subRef() == 0)
					{
						CPacket::mFree(pPacket);
					}
					break;
				}


				//패킷 프로시져 타기
				player.lastTime = GetTickCount64();
				bool ret = pChatServer->PacketProc(&player, packetType, pPacket, jobItem.SessionID);
				if (ret == false)
				{
					//아래부분 함수로 래핑
					st_Session* pSession;
					if (pChatServer->pNetServer->findSession(player.sessionID, &pSession) == true)
					{
						pChatServer->pNetServer->disconnectSession(pSession);
						if (InterlockedDecrement(&pSession->IOcount) == 0)
						{
							pChatServer->pNetServer->releaseSession(player.sessionID);
						}
					}
				}

				if (pPacket->subRef() == 0)
				{
					CPacket::mFree(pPacket);
				}
				break;
			}

			}
		}
		
		pChatServer->JobCountperCycle = pChatServer->Temp_JobCountperCycle;
		pChatServer->Temp_JobCountperCycle = 0;
		pChatServer->Temp_NumOfWFSO++;
		WaitForSingleObject(pChatServer->hJobEvent, 1000);
	}
	return true;
}

bool CChatServer::Start()
{
	maxPlayer = pNetServer->getMaxSession();
	hLogicThread = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&LogicThread, this, 0, 0);
	if (hLogicThread == NULL)
	{
		wprintf(L"LogicThread init error");
		return false;
	}

	return true;
}

bool CChatServer::Stop()
{
	ShutDownFlag = true;
	WaitForSingleObject(hLogicThread, INFINITE);
	return true;
}

size_t CChatServer::getCharacterNum(void) // 캐릭터수
{
	return PlayerList.size();
}

LONG CChatServer::getJobQueueUseSize(void)
{
	return this->JobQueue.nodeCount;
}

LONG CChatServer::getJobCount(void)
{
	return this->JobCount;
}

LONG CChatServer::getNumOfWFSO(void)
{
	return this->NumOfWFSO;
}

LONG CChatServer::getJobCountperCycle(void)
{
	return this->JobCountperCycle;
}

LONG CChatServer::getPlayerPoolUseSize(void)
{
	return this->PlayerPool.getUseSize();
}

void CChatServer::sector_AddCharacter(st_Player* pPlayer) //섹터에 캐릭터 넣음
{
	short Xpos = pPlayer->sectorPos.sectorX;
	short Ypos = pPlayer->sectorPos.sectorY;
	g_Sector[Ypos][Xpos].push_back(pPlayer->sessionID);
	//카운팅용
	SectorCount[Ypos][Xpos]++;
}
void CChatServer::sector_RemoveCharacter(st_Player* pPlayer) //섹터에서 캐릭터 삭제
{
	short Xpos = pPlayer->sectorPos.sectorX;
	short Ypos = pPlayer->sectorPos.sectorY;

	

	list<INT64>::iterator iter = g_Sector[Ypos][Xpos].begin();
	for (; iter != g_Sector[Ypos][Xpos].end(); )
	{
		if (*iter == pPlayer->sessionID)
		{
			iter = g_Sector[Ypos][Xpos].erase(iter);
			SectorCount[Ypos][Xpos]--;
			return;
		}
		else
		{
			iter++;
		}
	}
}

void CChatServer::getSectorAround(int sectorX, int sectorY, st_SectorAround* pSectorAround)
{
	int Xoffset, Yoffset;

	sectorX--;
	sectorY--;

	pSectorAround->count = 0;
	for (Yoffset = 0; Yoffset < 3; Yoffset++)
	{
		if (sectorY + Yoffset < 0 || sectorY + Yoffset >= dfSECTOR_MAX_Y)
		{
			continue;
		}

		for (Xoffset = 0; Xoffset < 3; Xoffset++)
		{
			if (sectorX + Xoffset < 0 || sectorX + Xoffset >= dfSECTOR_MAX_X)
			{
				continue;
			}
			pSectorAround->around[pSectorAround->count].sectorX = sectorX + Xoffset;
			pSectorAround->around[pSectorAround->count].sectorY = sectorY + Yoffset;
			pSectorAround->count++;
		}
	}
}

void CChatServer::makeSessionSet_AroundMe(st_Player* pPlayer, CSessionSet* InParamSet, bool sendMe)
{
	//CProfiler("makeSessionSet_AroundMe");
	st_SectorAround AroundMe;
	int sectorX, sectorY;
	getSectorAround(pPlayer->sectorPos.sectorX, pPlayer->sectorPos.sectorY, &AroundMe);
	for (int i = 0; i < AroundMe.count; i++)
	{
		sectorX = AroundMe.around[i].sectorX;
		sectorY = AroundMe.around[i].sectorY;

		list<INT64>& targetSector = g_Sector[sectorY][sectorX];
		list<INT64>::iterator sectorIter;

		for (sectorIter = targetSector.begin(); sectorIter != targetSector.end(); sectorIter++)
		{
			if (sendMe == false && *sectorIter == pPlayer->sessionID)
			{
				continue;
			}
			InParamSet->setSession(*sectorIter);
		}

	}
}