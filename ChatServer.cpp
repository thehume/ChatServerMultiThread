#pragma comment(lib, "winmm.lib" )
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
	InitializeSRWLock(&PlayerListLock);
	for (int i = 0; i < dfSECTOR_MAX_Y; i++)
	{
		for (int j = 0; j < dfSECTOR_MAX_X; j++)
		{
			InitializeSRWLock(&SectorLock[i][j]);
		}
	}

}


void CChatServer::CS_CHAT_RES_LOGIN(INT64 SessionID, BYTE Status, INT64 AccountNo)
{
	//CProfiler("CS_CHAT_RES_LOGIN");
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
	//CProfiler("CS_CHAT_RES_SECTOR_MOVE");
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
	//CProfiler("CS_CHAT_RES_MESSAGE");
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
	//CProfiler("PP_REQ_LOGIN");
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
	//CProfiler("PP_REQ_SECTOR_MOVE");
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

	MoveCount++;

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

	//move 메시지 로직
	if (pPlayer->sectorPos.sectorX == SectorX && pPlayer->sectorPos.sectorY == SectorY)
	{
		//systemLog(L"SECTOR POS SAME ", dfLOG_LEVEL_DEBUG, L" X %uh:  Y : %uh", SectorX, SectorY);
	}

	//전체 섹터에 내가 존재하지 않았다면 들어갈 섹터에만 나를 추가함
	else if (pPlayer->sectorPos.sectorX < 0 || pPlayer->sectorPos.sectorX >= dfSECTOR_MAX_X || pPlayer->sectorPos.sectorY < 0 || pPlayer->sectorPos.sectorY >= dfSECTOR_MAX_Y)
	{
		pPlayer->sectorPos.sectorX = SectorX;
		pPlayer->sectorPos.sectorY = SectorY;
		sector_AddCharacter(pPlayer);
	}

	//내가 존재하는 섹터에서 나를 삭제하고 들어갈 섹터에 나를 추가
	else
	{
		sector_RemoveAndAddCharacter(pPlayer, SectorX, SectorY);
	}

	//업데이트된 섹터 메시지 보내주기
	CS_CHAT_RES_SECTOR_MOVE(pPlayer->sessionID, AccountNo, SectorX, SectorY);
	return true;
}


bool CChatServer::packetProc_CS_CHAT_REQ_MESSAGE(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//CProfiler("PP_REQ_MESSAGE");
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

	//chat 메시지 처리 로직
	//채팅메시지를 보내줘야 하는 세션에 대한 set을 얻는다
	CSessionSet sessionSet;
	makeSessionSet_AroundMe(pPlayer, &sessionSet);

	CS_CHAT_RES_MESSAGE(&sessionSet, AccountNo, pPlayer->ID, pPlayer->Nickname, MessageLen, Message);

	MsgCount++;
	return true;

}
bool CChatServer::packetProc_CS_CHAT_REQ_HEARTBEAT(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID)
{
	//CProfiler("PP_REQ_HEARTBEAT");
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
	while (!pChatServer->ShutDownFlag)
	{
	//시간 쟤서 모든세션의 lastPacket 확인 -> 40초가 지났다면 그세션끊기
		AcquireSRWLockShared(&pChatServer->PlayerListLock);
		ULONGLONG curTime = GetTickCount64();
		pChatServer->Interval = curTime - pChatServer->lastTime;
		pChatServer->lastTime = curTime;
		st_Session* pSession;
		for (auto iter = pChatServer->PlayerList.begin(); iter != pChatServer->PlayerList.end(); iter++)
		{
			st_Player& player = *iter->second;
			if (player.isValid == FALSE)
			{
				continue;
			}
			if (curTime > player.lastTime + 50000 )
			{
				if (pChatServer->pNetServer->findSession(player.sessionID, &pSession) == true)
				{
					systemLog(L"TimeOut", dfLOG_LEVEL_DEBUG, L"over time : %lld", curTime - player.lastTime);
					pChatServer->pNetServer->disconnectSession(pSession);
					if (InterlockedDecrement(&pSession->IOcount) == 0)
					{
						pChatServer->pNetServer->releaseRequest(pSession);
					}
				}
			}
		}
		ReleaseSRWLockShared(&pChatServer->PlayerListLock);
				
	Sleep(10000);
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

LONG CChatServer::getPlayerPoolUseSize(void)
{
	return this->PlayerPool.getUseSize();
}

void CChatServer::sector_AddCharacter(st_Player* pPlayer) //섹터에 캐릭터 넣음
{
	//CProfiler("sector_AddCharacter");
	short Xpos = pPlayer->sectorPos.sectorX;
	short Ypos = pPlayer->sectorPos.sectorY;
	AcquireSRWLockExclusive(&SectorLock[Ypos][Xpos]);
	Sector[Ypos][Xpos].push_back(pPlayer->sessionID);
	ReleaseSRWLockExclusive(&SectorLock[Ypos][Xpos]);
}

void CChatServer::sector_RemoveCharacter(st_Player* pPlayer) //섹터에서 캐릭터 삭제
{
	//CProfiler("sector_RemoveCharac");
	short Xpos = pPlayer->sectorPos.sectorX;
	short Ypos = pPlayer->sectorPos.sectorY;

	if (Xpos < 0 || Xpos >= dfSECTOR_MAX_X || Ypos<0 || Ypos >= dfSECTOR_MAX_Y)
	{
		return;
	}


	AcquireSRWLockExclusive(&SectorLock[Ypos][Xpos]);
	list<INT64>::iterator iter = Sector[Ypos][Xpos].begin();
	for (; iter != Sector[Ypos][Xpos].end(); )
	{
		if (*iter == pPlayer->sessionID)
		{
			iter = Sector[Ypos][Xpos].erase(iter);
			ReleaseSRWLockExclusive(&SectorLock[Ypos][Xpos]);
			return;
		}
		else
		{
			iter++;
		}
	}
	ReleaseSRWLockExclusive(&SectorLock[Ypos][Xpos]);
}

void CChatServer::sector_RemoveAndAddCharacter(st_Player* pPlayer, int newX, int newY)
{
	//CProfiler("sector_RemoveAndAdd");
	int oldX, oldY;
	oldX = pPlayer->sectorPos.sectorX;
	oldY = pPlayer->sectorPos.sectorY;
	bool flag = true;
	if (oldY < newY || (oldY == newY && oldX < newX))
	{
		AcquireSRWLockExclusive(&SectorLock[oldY][oldX]);
		AcquireSRWLockExclusive(&SectorLock[newY][newX]);
	}

	else
	{
		flag = false;
		AcquireSRWLockExclusive(&SectorLock[newY][newX]);
		AcquireSRWLockExclusive(&SectorLock[oldY][oldX]);
	}

	//현재섹터에서 나 삭제
	list<INT64>::iterator iter = Sector[oldY][oldX].begin();
	for (; iter != Sector[oldY][oldX].end(); )
	{
		if (*iter == pPlayer->sessionID)
		{
			iter = Sector[oldY][oldX].erase(iter);
			break;
		}
		else
		{
			iter++;
		}
	}

	//나의 섹터정보 업데이트
	pPlayer->sectorPos.sectorX = newX;
	pPlayer->sectorPos.sectorY = newY;

	//바뀔 섹터에 나 넣어주기
	Sector[newY][newX].push_back(pPlayer->sessionID);

	if (flag == true)
	{
		ReleaseSRWLockExclusive(&SectorLock[oldY][oldX]);
		ReleaseSRWLockExclusive(&SectorLock[newY][newX]);
	}

	else
	{
		ReleaseSRWLockExclusive(&SectorLock[newY][newX]);
		ReleaseSRWLockExclusive(&SectorLock[oldY][oldX]);
	}
}

void CChatServer::getSectorAround(int sectorX, int sectorY, st_SectorAround* pSectorAround)
{
	//CProfiler("getSectorAround");
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

	//락획득
	for (int i = 0; i < AroundMe.count; i++)
	{
		sectorX = AroundMe.around[i].sectorX;
		sectorY = AroundMe.around[i].sectorY;

		AcquireSRWLockShared(&SectorLock[sectorY][sectorX]);
	}

	for (int i = 0; i < AroundMe.count; i++)
	{
		sectorX = AroundMe.around[i].sectorX;
		sectorY = AroundMe.around[i].sectorY;

		list<INT64>& targetSector =	Sector[sectorY][sectorX];
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

	//락해제
	for (int i = 0; i < AroundMe.count; i++)
	{
		sectorX = AroundMe.around[i].sectorX;
		sectorY = AroundMe.around[i].sectorY;

		ReleaseSRWLockShared(&SectorLock[sectorY][sectorX]);
	}

}