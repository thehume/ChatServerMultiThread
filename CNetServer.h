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

struct st_MyOverlapped
{
	WSAOVERLAPPED overlapped;
	int flag;
};

struct alignas(4096) st_Session
{
	INT64 sessionID;
	SOCKET sock;
	volatile LONG isValid; // ���� ��ȿ����, 1(�Ҵ�Ϸ�) 0(�̻����)
	volatile LONG releaseFlag; // release ���� ���࿩�� Ȯ��, DELFLAG_OFF(������) DELFLAG_ON(������ or ���� �Ϸ�)
	volatile LONG IOcount; //0�̸� ����
	volatile LONG sendFlag; //1(send��) 0(send���Ҷ�)
	volatile LONG disconnectStep; // SESSION_NORMAL_STATE(���), SESSION_SENDPACKET_LAST(���������� ��û��), SESSION_SENDPOST_LAST(������ SENDPOST ȣ���), SESSION_DISCONNECT(������ϴ� ����) 
	st_MyOverlapped RecvOverlapped;
	st_MyOverlapped SendOverlapped;
	CRingBuffer recvQueue;
	DWORD sendPacketCount;
	DWORD sendCount; //���� TPSüũ�� ���(����������)
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

	int getMaxSession();
	INT64 getAcceptSum();
	int getSessionCount();
	int getAcceptTPS();
	int getDisconnectTPS();
	int getRecvMessageTPS();
	int getSendMessageTPS();

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