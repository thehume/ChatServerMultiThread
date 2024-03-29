#pragma once

#define dfMAX_SESSION 30000
#define dfSESSION_SETSIZE 1000

#define dfMAX_PACKET 950
#define dfDATA_SIZE 8

#define DELFLAG_OFF 0
#define DELFLAG_ON -1

#define SESSION_NORMAL_STATE 0
#define SESSION_SENDPACKET_LAST 1
#define SESSION_SENDPOST_LAST 2
#define SESSION_DISCONNECT 3

#define dfSENDPOST_REQ 0xFFFF
#define dfRELEASE_REQ 0x00FF

struct st_MyOverlapped
{
	WSAOVERLAPPED overlapped;
	int flag;
};

struct alignas(4096) st_Session
{
	INT64 sessionID;
	SOCKET sock;
	volatile LONG isValid; // 세션 유효여부, 1(할당완료) 0(미사용중)
	volatile LONG releaseFlag; // release 로직 실행여부 확인, DELFLAG_OFF(정상사용) DELFLAG_ON(삭제중 or 삭제 완료)
	volatile LONG IOcount; //0이면 삭제
	volatile LONG sendFlag; //1(send중) 0(send안할때)
	volatile LONG disconnectStep; // SESSION_NORMAL_STATE(평시), SESSION_SENDPACKET_LAST(보내고끊기 요청시), SESSION_SENDPOST_LAST(최후의 SENDPOST 호출시), SESSION_DISCONNECT(끊어내야하는 상태) 
	st_MyOverlapped RecvOverlapped;
	st_MyOverlapped SendOverlapped;
	CRingBuffer recvQueue;
	DWORD sendPacketCount;
	DWORD sendCount; //이하 TPS체크용 멤버(로직과무관)
	DWORD recvCount;
	DWORD disconnectCount;
	LockFreeQueue<CPacket*> sendQueue;
	CPacket* sentPacketArray[dfMAX_PACKET];
};

class CSessionSet
{
public:

	CSessionSet()
	{
		Session_Count = 0;
	}

	void setClear()
	{
		Session_Count = 0;
	}

	BOOL setSession(INT64 sessionID)
	{
		if (Session_Count >= dfSESSION_SETSIZE)
		{
			return FALSE;
		}
		Session_Array[Session_Count++] = sessionID;
		return TRUE;
	}

	SHORT Session_Count;
	INT64 Session_Array[dfSESSION_SETSIZE];
};

class CInitParam
{
public:
	CInitParam(WCHAR* openIP, int openPort, int maxThreadNum, int concurrentThreadNum, bool Nagle, int maxSession)
	{
		wcscpy_s(this->openIP, openIP);
		this->openPort = openPort;
		this->maxThreadNum = maxThreadNum;
		this->concurrentThreadNum = concurrentThreadNum;
		this->Nagle = Nagle;
		this->maxSession = maxSession;
	}

	WCHAR openIP[20];
	int openPort;
	int maxThreadNum;
	int concurrentThreadNum;
	bool Nagle;
	int maxSession;
};

class CNetServerHandler;

class CNetServer
{
public:
	CNetServer(CInitParam* pParam);
	~CNetServer();

	bool Start();
	void Stop();
	void recvPost(st_Session* pSession);
	void sendPost(st_Session* pSession);
	bool findSession(INT64 SessionID, st_Session** ppSession);
	void disconnectSession(st_Session* pSession);
	void sendPacket(INT64 SessionID, CPacket* packet, BOOL LastPacket = FALSE);
	void sendPacket(CSessionSet* pSessionSet, CPacket* pPacket, BOOL LastPacket = FALSE);
	void releaseSession(INT64 SessionID);
	void releaseRequest(st_Session* pSession);

	int getMaxSession();
	INT64 getAcceptSum();
	int getSessionCount();
	int getAcceptTPS();
	int getDisconnectTPS();
	int getRecvMessageTPS();
	int getSendMessageTPS();
	INT64 getTotalTPS();

	void attachHandler(CNetServerHandler* pHandler);

	static DWORD WINAPI ControlThread(CNetServer* ptr);
	static DWORD WINAPI AcceptThread(CNetServer* ptr);
	static DWORD WINAPI WorkerThread(CNetServer* ptr);

private:
	CNetServerHandler* pHandler;

	CNetServer(const CNetServer& other) = delete;
	const CNetServer& operator=(const CNetServer& other) = delete;

	WCHAR openIP[20];
	int openPort;
	int maxThreadNum;
	int concurrentThreadNum;
	bool Nagle;
	int maxSession;

	st_Session sessionList[dfMAX_SESSION];
	LockFreeStack<int> emptyIndexStack;

	BOOL InitFlag;
	DWORD InitErrorNum;
	DWORD InitErrorCode;
	BOOL Shutdown;

	INT64 sessionAllocNum;

	INT64 acceptSum = 0;
	DWORD acceptCount = 0;
	DWORD disconnectCount = 0;
	DWORD sendCount = 0;
	DWORD recvCount = 0;

	INT64 TPSTotal = 0;

	DWORD acceptTPS = 0;
	DWORD disconnectTPS = 0;
	DWORD sendTPS = 0;
	DWORD recvTPS = 0;

	DWORD Temp_disconnectTPS = 0;
	DWORD Temp_sendTPS = 0;
	DWORD Temp_recvTPS = 0;

	DWORD Temp_sessionNum = 0;
	DWORD sessionNum = 0;

	HANDLE hcp;
	SOCKET listenSock;
	HANDLE hWorkerThread[100];
	HANDLE hAcceptThread;
	HANDLE hControlThread;
};

class CNetServerHandler
{
public:
	virtual bool OnConnectionRequest() = 0;
	virtual void OnClientJoin(long long sessionID) = 0;
	virtual void OnClientLeave(long long sessionID) = 0;
	virtual void OnError(int errorCode) = 0;
	virtual bool OnRecv(long long sessionID, CPacket* pPacket) = 0;
};
