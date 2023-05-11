#pragma once

//채팅서버, 2개 클래스로 존재

using namespace std;

#define dfSECTOR_MAX_X 50
#define dfSECTOR_MAX_Y 50

enum en_JobType
{
    en_JOB_ON_CLIENT_JOIN,
    en_JOB_ON_RECV,
    en_JOB_ON_CLIENT_LEAVE
};

struct st_JobItem
{
    INT64 JobType;
    INT64 SessionID;
    CPacket* pPacket;
};

struct st_Message
{
    DWORD len;
    WCHAR msg[500];
};

struct st_UserName
{
    WCHAR name[20];
};

struct st_SessionKey
{
    char sessionKey[64];
};


inline CPacket& operator << (CPacket& packet, st_UserName& userName)
{

    if (packet.GetLeftUsableSize() >= sizeof(st_UserName))
    {
        memcpy(packet.GetWriteBufferPtr(), userName.name, sizeof(st_UserName));
        packet.MoveWritePos(sizeof(st_UserName));
    }
    return packet;
}

inline CPacket& operator << (CPacket& packet, st_Message& Message)
{

    if (packet.GetLeftUsableSize() >= Message.len)
    {
        memcpy(packet.GetWriteBufferPtr(), Message.msg, Message.len);
        packet.MoveWritePos(Message.len);
    }
    return packet;
}

inline CPacket& operator << (CPacket& packet, st_SessionKey& SessionKey)
{

    if (packet.GetLeftUsableSize() >= sizeof(st_SessionKey))
    {
        memcpy(packet.GetWriteBufferPtr(), SessionKey.sessionKey, sizeof(st_SessionKey));
        packet.MoveWritePos(sizeof(st_SessionKey));
    }
    return packet;
}

inline CPacket& operator >> (CPacket& packet, st_UserName& userName)
{
    if (packet.GetDataSize() >= sizeof(st_UserName))
    {
        memcpy(userName.name, packet.GetReadBufferPtr(), sizeof(st_UserName));
        packet.MoveReadPos(sizeof(st_UserName));
    }
    return packet;
}

inline CPacket& operator >> (CPacket& packet, st_Message& Message)
{
    if (packet.GetDataSize() >= Message.len)
    {
        memcpy(Message.msg, packet.GetReadBufferPtr(), Message.len);
        packet.MoveReadPos(Message.len);
    }
    return packet;
}

inline CPacket& operator >> (CPacket& packet, st_SessionKey& SessionKey)
{
    if (packet.GetDataSize() >= sizeof(st_SessionKey))
    {
        memcpy(SessionKey.sessionKey, packet.GetReadBufferPtr(), sizeof(st_SessionKey));
        packet.MoveReadPos(sizeof(st_SessionKey));
    }
    return packet;
}

class CChatServer
{
    friend class CContentsHandler;

public:

    struct st_SectorPos
    {
        WORD sectorX;
        WORD sectorY;
    };

    struct st_SectorAround
    {
        int count;
        st_SectorPos around[9];
    };

    struct st_Player
    {
        BOOL isValid;
        INT64 AccountNo;
        st_UserName ID;
        st_UserName Nickname;
        st_SectorPos sectorPos;
        INT64 sessionID;
        st_SessionKey sessionKey;
        ULONGLONG lastTime;
    };


    CChatServer();
    void attachServerInstance(CNetServer* networkServer)
    {
        pNetServer = networkServer;
    }
    static DWORD WINAPI LogicThread(CChatServer* pChatServer);
    bool Start();
    bool Stop();

    //패킷 프로시저들!!
    void CS_CHAT_RES_LOGIN(INT64 SessionID, BYTE Status, INT64 AccountNo);
    void CS_CHAT_RES_SECTOR_MOVE(INT64 SessionID, INT64 AccountNo, WORD SectorX, WORD	SectorY);
    void CS_CHAT_RES_MESSAGE(CSessionSet* SessionSet, INT64 AccountNo, st_UserName ID, st_UserName Nickname, WORD MessageLen, st_Message& Message);


    bool packetProc_CS_CHAT_REQ_LOGIN(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID);
    bool packetProc_CS_CHAT_REQ_SECTOR_MOVE(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID);
    bool packetProc_CS_CHAT_REQ_MESSAGE(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID);
    bool packetProc_CS_CHAT_REQ_HEARTBEAT(st_Player* pPlayer, CPacket* pPacket, INT64 SessionID);

    bool PacketProc(st_Player* pPlayer, WORD PacketType, CPacket* pPacket, INT64 SessionID);
    
    size_t getCharacterNum(void); // 캐릭터수
    LONG getPlayerPoolUseSize(void);
    void sector_AddCharacter(st_Player* pPlayer); //섹터에 캐릭터 넣음
    void sector_RemoveCharacter(st_Player* pPlayer); //섹터에서 캐릭터 삭제
    void sector_RemoveAndAddCharacter(st_Player* pPlayer, int newX, int newY); //섹터에서 캐릭터 삭제후 넣어줌
    void getSectorAround(int sectorX, int sectorY, st_SectorAround* pSectorAround); //현재섹터 기준으로 9개섹터
    void makeSessionSet_AroundMe(st_Player* pPlayer, CSessionSet* InParamSet, bool sendMe = true); //"나" 기준으로 주위섹터의 세션 셋 가져옴

    ULONGLONG Interval = 0;

    INT64 MoveCount = 0;
    INT64 MsgCount = 0;

private:
    HANDLE hLogicThread;
    volatile bool ShutDownFlag;
    int maxPlayer;

    ULONGLONG lastTime;

    CNetServer* pNetServer;

    alignas(64) unordered_map<INT64, st_Player*> PlayerList;
    alignas(64) SRWLOCK PlayerListLock;

    alignas(64) list<INT64> Sector[dfSECTOR_MAX_Y][dfSECTOR_MAX_X];
    alignas(64) SRWLOCK SectorLock[dfSECTOR_MAX_Y][dfSECTOR_MAX_X];
 
    alignas(64) CMemoryPool<CChatServer::st_Player> PlayerPool;
};

class CContentsHandler : public CNetServerHandler
{
public:
    void attachServerInstance(CNetServer* networkServer, CChatServer* contentsServer)
    {
        pNetServer = networkServer;
        pChatServer = contentsServer;
    }
    virtual bool OnConnectionRequest() { return true; }
    virtual void OnClientJoin(INT64 sessionID)
    {
        CChatServer::st_Player* pNewPlayer;
        pChatServer->PlayerPool.mAlloc(&pNewPlayer);
        pNewPlayer->isValid = true;
        pNewPlayer->AccountNo = 0;
        wcscpy_s(pNewPlayer->ID.name, L"NULL");
        wcscpy_s(pNewPlayer->Nickname.name, L"NULL");
        pNewPlayer->sectorPos.sectorX = 65535;
        pNewPlayer->sectorPos.sectorY = 65535;
        pNewPlayer->sessionID = sessionID;
        pNewPlayer->lastTime = GetTickCount64();

        AcquireSRWLockExclusive(&pChatServer->PlayerListLock);
        pChatServer->PlayerList.insert(make_pair(sessionID, pNewPlayer));
        ReleaseSRWLockExclusive(&pChatServer->PlayerListLock);
    }

    virtual void OnClientLeave(INT64 sessionID)
    {
        AcquireSRWLockExclusive(&pChatServer->PlayerListLock);
        auto item = pChatServer->PlayerList.find(sessionID);
        if (item == pChatServer->PlayerList.end())
        {
            ReleaseSRWLockExclusive(&pChatServer->PlayerListLock);
            return;
        }
        else
        {
            CChatServer::st_Player* pPlayer = item->second;
            CChatServer::st_SectorPos& sectorPos = item->second->sectorPos;
            pChatServer->PlayerList.erase(item);
            ReleaseSRWLockExclusive(&pChatServer->PlayerListLock);
            if (sectorPos.sectorX < dfSECTOR_MAX_X && sectorPos.sectorX >= 0 && sectorPos.sectorY < dfSECTOR_MAX_Y && sectorPos.sectorY >= 0)
            {
                pChatServer->sector_RemoveCharacter(item->second);
            }
            pChatServer->PlayerPool.mFree(pPlayer);
        }
    }

    virtual bool OnRecv(INT64 SessionID, CPacket* pPacket)
    {
        pPacket->addRef(1);
        WORD packetType;
        *pPacket >> packetType;

        AcquireSRWLockShared(&pChatServer->PlayerListLock);
        auto item = pChatServer->PlayerList.find(SessionID);
        if (item == pChatServer->PlayerList.end())
        {
            ReleaseSRWLockShared(&pChatServer->PlayerListLock);
            if (pPacket->subRef() == 0)
            {
                CPacket::mFree(pPacket);
            }
            return false;
        }

        CChatServer::st_Player& player = *item->second;
        if (player.isValid == FALSE)
        {
            ReleaseSRWLockShared(&pChatServer->PlayerListLock);
            if (pPacket->subRef() == 0)
            {
                CPacket::mFree(pPacket);
            }
            return false;
        }
        ReleaseSRWLockShared(&pChatServer->PlayerListLock);

        //패킷 프로시져 타기
        player.lastTime = GetTickCount64();
        bool ret = pChatServer->PacketProc(&player, packetType, pPacket, SessionID);
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


        return true;
    }

    virtual void OnError(int errorCode)
    {

    }

private:
    CNetServer* pNetServer;
    CChatServer* pChatServer;
};