#pragma once

#define df_PDH_ETHERNET_MAX 8

struct st_ETHERNET
{
	bool _bUse;
	WCHAR _szName[128];
	PDH_HCOUNTER _pdh_Counter_Network_RecvBytes;
	PDH_HCOUNTER _pdh_Counter_Network_SendBytes;
};

class CHardwareMonitor
{
public:
	CHardwareMonitor();

	void Update(void);

	float getProcessorTotal(void) { return ProcessorTotal; }
	float getProcessorUser(void) { return ProcessorUser; }
	float getProcessorKernel(void) { return ProcessorKernel; }

	double getAvailableMemory(void) { return AvailableMemoryCounterVal.doubleValue; }
	double getNonpagedMemory(void) { return NonpagedMemoryCounterVal.doubleValue; }

	LONG getRecvBytes(void) { return _pdh_value_Network_RecvBytes; }
	LONG getSendBytes(void) { return _pdh_value_Network_SendBytes; }

private:
	int NumberOfProcessors;

	float ProcessorTotal;
	float ProcessorUser;
	float ProcessorKernel;

	ULARGE_INTEGER Processor_LastKernel;
	ULARGE_INTEGER Processor_LastUser;
	ULARGE_INTEGER Processor_LastIdle;

	PDH_HQUERY HardwareQuery;
	PDH_HCOUNTER AvailableMemory;
	PDH_HCOUNTER NonpagedMemory;

	PDH_FMT_COUNTERVALUE AvailableMemoryCounterVal;
	PDH_FMT_COUNTERVALUE NonpagedMemoryCounterVal;
	PDH_FMT_COUNTERVALUE EthernetCounterVal;

	st_ETHERNET _EthernetStruct[df_PDH_ETHERNET_MAX]; // ��ī�� �� PDH ����
	double _pdh_value_Network_RecvBytes; // �� Recv Bytes ��� �̴����� Recv ��ġ �ջ�
	double _pdh_value_Network_SendBytes; // �� Send Bytes ��� �̴����� Send ��ġ �ջ�

	int iCnt = 0;
	bool bErr = false;
	WCHAR* szCur = NULL;
	WCHAR* szCounters = NULL;
	WCHAR* szInterfaces = NULL;
	DWORD dwCounterSize = 0, dwInterfaceSize = 0;
	WCHAR szQuery[1024] = { 0, };


};