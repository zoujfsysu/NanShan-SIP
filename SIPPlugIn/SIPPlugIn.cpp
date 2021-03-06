// SIPPlugIn.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "SIPPlugIn.h"
#include "SIPRuner.h"
#include "Reconnect.h"

static HMODULE hDecModule = NULL;
static acl::locker	__lockerInit;
//SipService& sip = acl::singleton2<SipService>::get_instance();
#define sip SipRuner::m_sip

SIPHandler::SIPHandler(void)
{
	memset(m_szTerminalID, 0, sizeof(m_szTerminalID));
	memset(m_szDeviceID, 0, sizeof(m_szDeviceID));
	memset(m_szConnectKey, 0, sizeof(m_szConnectKey));
	m_szFrameData = NULL;
	//m_szFrameData = new unsigned char[FRAME_BUF_SIZE];
	//memset(m_szFrameData, 0, FRAME_BUF_SIZE);
	m_nFrameSize = 0;
	m_nTimestamp = 0;
	m_nStreamType = 0;
	m_nChannel = 0;
	m_nPlayOpenTime = 0;
	m_nPlayCurTime = 0;
	m_nPlayBackTime = 0;
	m_nPlayBackCurTime = 0;
	m_nPlayBackIndex = 0;
	m_bPlayOver = false;
	m_cPTZCmd = 0;
	m_nInviteId = 0;
	memset(m_szFilePath, 0, sizeof(m_szFilePath));
	m_pStramCallback = NULL;
	m_pStreamUser = NULL;
	m_pAnalyzeThrd = NULL;
	m_fRtpRec = NULL;
	m_fRecord = NULL;
	m_fDownload = NULL;
	m_bCloseSream = true;
	m_bMakeKeyFrame = false;
	m_bFirstPacket = true;
	m_nWindlt = 0;
	m_nWindrb = 0;
	m_nSrcWidth = 0;
	m_nSrcHight = 0;
	m_nXl = 0;
	m_nYt = 0;
	m_nXr = 0;
	m_nYb = 0;
	m_hWnd = NULL;
#ifdef _PLAY_PLUGIN
	m_pDecCallback = NULL;
	m_nDecUser = NULL;
	m_pDrawCallback = NULL;
	m_pDrawUser = NULL;
	m_hDecModule = NULL;
	m_pDecHandle = NULL;
	m_getHandler = NULL;
#endif
}

SIPHandler::~SIPHandler(void)
{
	if (m_szFrameData)
	{
		delete[] m_szFrameData;
		m_szFrameData = NULL;
	}
	disconnectDVR();
}


bool SIPHandler::connectDVR(char* sIP, int nPort, char* sUser, char* sPwd, int nChannel)
{
	static bool __bInit = false;
	__lockerInit.lock();
	if (!__bInit)
	{
		__bInit = true;
		SipRuner *init_thd = new SipRuner;
		init_thd->set_detachable(true);
		init_thd->start();
		sip_sleep(100);
	}
	__lockerInit.unlock();

	sip.send_register(sIP, nPort, sUser, sPwd);
	sprintf(m_szConnectKey, "%s:%d", sIP, nPort);
	return true;
}

void SIPHandler::disconnectDVR()
{
#ifdef _PLAY_PLUGIN
	stopRecord();
	if (m_hWnd)
	{
		stopPlayerByCamera(m_hWnd, m_nChannel);
		m_hWnd = NULL;
	}
#else
	stopCaptureStream();
#endif
}

void SIPHandler::setCurrentCameraCode(char* sCameraCode)
{
	if (sCameraCode && strlen(sCameraCode) == 20)
	{
		int nType = sip_get_user_role(sCameraCode);
		if (nType == SIP_ROLE_CHANNEL)
		{
			memset(m_szDeviceID, 0, sizeof(m_szDeviceID));
			strcpy(m_szDeviceID, sCameraCode);
		}
		else if (nType == SIP_ROLE_MONITOR)
		{
			memset(m_szTerminalID, 0, sizeof(m_szTerminalID));
			strcpy(m_szTerminalID, sCameraCode);
		}
	}
}

bool SIPHandler::captureStream(LPCAPTURESTREAMCALLBACK lpCallBack, void* pUser, int nGetVideoType)
{
	m_pStramCallback = lpCallBack;
	m_pStreamUser = pUser;

	invite_info info;
	memset(&info, 0, sizeof(invite_info));
	strcpy(info.addr, m_szConnectKey);
	strcpy(info.deviceid, m_szDeviceID);
	if (m_szTerminalID[0] != '\0')
		strcpy(info.terminalid, m_szTerminalID);
	info.invite_type = SIP_INVITE_PLAY;
	info.carrysdp = true;
	sip.build_ssrc(info.ssrc, sizeof(info.ssrc), SIP_INVITE_PLAY);
	m_nInviteId = sip.send_invite(&info, stream_cb, this);
	return m_nInviteId>0?true:false;
}

bool SIPHandler::stopCaptureStream()
{
	if (m_nInviteId > 0)
	{
		sip.m_conf.log(LOG_INFO, "发送关闭请求![%d]\n", m_nInviteId);
		sip.send_bye(m_nInviteId, m_bCloseSream);
		m_nInviteId = 0;
	}
#ifdef _PS_ANALYZE_USE
	if (m_pAnalyzeThrd)
		((PSStreamAnalyze *)m_pAnalyzeThrd)->stop();
	m_pAnalyzeThrd = NULL;
#endif
	m_pStramCallback = NULL;
	m_pStreamUser = NULL;
	m_bMakeKeyFrame = false;
	m_bFirstPacket = true;
	m_nFrameSize = 0;
	m_nTimestamp = 0;
	m_nStreamType = 0;
	return true;
}

bool SIPHandler::controlPTZSpeedWithChannel(int nCmd, bool bEnable, int nSpeed, int nChannel)
{
	unsigned char nParam1 = nSpeed&0xFF;
	unsigned short nParam2 = nSpeed >> 8;
	SIP_PTZ_RECT rect;
	memset(&rect, 0, sizeof(SIP_PTZ_RECT));
	unsigned char cPtzCmd = SIP_PTZCMD_STOP;
	switch (nCmd)
	{
	case CAMERA_PAN_UP:			//云台向上
		cPtzCmd = SIP_PTZCMD_UP;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_DOWN:		//云台向下
		cPtzCmd = SIP_PTZCMD_DOWN;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_LEFT:		//云台向左
		cPtzCmd = SIP_PTZCMD_LEFT;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_RIGHT:		//云台向右
		cPtzCmd = SIP_PTZCMD_RIGHT;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_FOCUS_IN:		//焦点前调
		cPtzCmd = SIP_PTZCMD_FI_FOCUS_IN;
		m_cPTZCmd = SIP_PTZCMD_FI_STOP;
		break;
	case CAMERA_FOCUS_OUT:		//焦点后调
		cPtzCmd = SIP_PTZCMD_FI_FOCUS_OUT;
		m_cPTZCmd = SIP_PTZCMD_FI_STOP;
		break;
	case CAMERA_IRIS_IN:        //光圈调大
		cPtzCmd = SIP_PTZCMD_FI_IRIS_IN;
		m_cPTZCmd = SIP_PTZCMD_FI_STOP;
		break;
	case CAMERA_IRIS_OUT:       //光圈调小
		cPtzCmd = SIP_PTZCMD_FI_IRIS_OUT;
		m_cPTZCmd = SIP_PTZCMD_FI_STOP;
		break;
	case CAMERA_ZOOM_IN:        //倍率调大
		cPtzCmd = SIP_PTZCMD_ZOOM_IN;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_ZOOM_OUT:       //倍率调小
		cPtzCmd = SIP_PTZCMD_ZOOM_OUT;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_BRUSH_CTRL:     //雨刷控制
		nParam1 = 1;
		cPtzCmd = SIP_PTZCMD_AUXIOPEN;
		m_cPTZCmd = SIP_PTZCMD_AUXICLOSE;
		break;
	case CAMERA_AUTO_TURN:      //自动旋转
		cPtzCmd = SIP_PTZCMD_AUTO_RUN;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_LU:			//云台左上
		cPtzCmd = SIP_PTZCMD_LU;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_LD:			//云台左下
		cPtzCmd = SIP_PTZCMD_LD;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_RU:			//云台右上
		cPtzCmd = SIP_PTZCMD_RU;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_PAN_RD:			//云台右下
		cPtzCmd = SIP_PTZCMD_RD;
		m_cPTZCmd = SIP_PTZCMD_STOP;
		break;
	case CAMERA_INITIAL:		//看守位开启
		cPtzCmd = SIP_PTZCMD_INITIAL;
		m_cPTZCmd = SIP_PTZCMD_NONE;
		break;
	case CAMERA_UNINITIAL:		//看守位关闭
		cPtzCmd = SIP_PTZCMD_UNINITIAL;
		m_cPTZCmd = SIP_PTZCMD_NONE;
		break;
	case CAMERA_LOCK:			//锁定云台
		cPtzCmd = SIP_PTZCMD_LOCK;
		m_cPTZCmd = SIP_PTZCMD_NONE;
		break;
	case CAMERA_UNLOCK:			//解锁云台
		cPtzCmd = SIP_PTZCMD_UNLOCK;
		m_cPTZCmd = SIP_PTZCMD_NONE;
		break;	
#ifdef _PLAY_PLUGIN
	case CAMERA_SETP:			//电子放大				
	case CAMERA_SETN:			//电子放大
		if (m_pDecHandle)
		{
			m_pDecHandle->controlPTZSpeedWithChannel(nCmd, bEnable, nSpeed, nChannel);
		}
		return true;
	case CAMERA_3D_FIRST:		//3D放大
		m_nWindlt = nSpeed;
		return true;
	case CAMERA_3D_SECOND:		//3D放大
		{
			if (m_nWindlt == 0)
			{
				return false;
			}
			unsigned char prec[4] = { 0 };
			unsigned char nrec[4] = { 0 };
			memcpy(prec, &m_nWindlt, sizeof(m_nWindlt));
			memcpy(nrec, &nSpeed, sizeof(nSpeed));
			int pl = 0, pt = 0, pr = 0, pb = 0;//原来窗口坐标
			int nl = 0, nt = 0, nr = 0, nb = 0;//框选窗口坐标
			pl = prec[0] * 10;
			pt = prec[1] * 10;
			pr = prec[2] * 10;
			pb = prec[3] * 10;
			nl = nrec[0] * 10;
			nt = nrec[1] * 10;
			nr = nrec[2] * 10;
			nb = nrec[3] * 10;
			if (m_nSrcWidth == 0 && m_nSrcHight == 0 || (nSpeed == m_nWindlt))
			{
				if (m_pDecHandle == NULL)
					return false;
				char szVideoOutCfg[32] = { 0 };
				DWORD dwOutSize = 0;
				if (!m_pDecHandle->getDVRConfig(CONFIG_GET_VIDEOOUTCFG, 0, szVideoOutCfg, sizeof(szVideoOutCfg), &dwOutSize))
					return false;
				sscanf(szVideoOutCfg, "%d,%d", &m_nSrcWidth, &m_nSrcHight);
				if (nSpeed == m_nWindlt)
				{
					cPtzCmd = SIP_PTZCMD_DRAGZOOM_OUT;
					m_nXl = 0;
					m_nYt = 0;
					m_nXr = m_nSrcWidth;
					m_nYb = m_nSrcHight;
				}
				else
				{
					cPtzCmd = SIP_PTZCMD_DRAGZOOM_IN;
					m_nXl = m_nSrcWidth*nl / (pr - pl);
					m_nYt = m_nSrcHight*nt / (pb - pt);
					m_nXr = m_nSrcWidth*nr / (pr - pl);
					m_nYb = m_nSrcHight*nb / (pb - pt);
				}
			}
			else
			{
				cPtzCmd = SIP_PTZCMD_DRAGZOOM_IN;
				int x = m_nXl;
				int y = m_nYt;
				int nWidth = m_nXr - m_nXl;
				int nHight = m_nYb - m_nYt;
				m_nXl = (nWidth*nl / (pr - pl)) + x;
				m_nYt = (nHight*nt / (pb - pt)) + y;
				m_nXr = (nWidth*nr / (pr - pl)) + x;
				m_nYb = (nHight*nb / (pb - pt)) + y;
			}
			rect.width = m_nSrcWidth;
			rect.height = m_nSrcHight;
			rect.left = m_nXl;
			rect.top = m_nYt;
			rect.right = m_nXr;
			rect.bottom = m_nYb;
		}
		break;
#endif
	default:
		break;
	}
	if (bEnable)	
	{
		cPtzCmd = m_cPTZCmd;
	}
	sip.send_ptzcmd(m_szConnectKey, m_szDeviceID, cPtzCmd, nParam1, nParam2, &rect);
	return true;
}

bool SIPHandler::controlPTZSpeedWithChannelEx(PCONTROL_PARAM pParam)
{
	return controlPTZSpeedWithChannel(pParam->nCmd, pParam->bEnable, pParam->nSpeed, pParam->nChannel);
}

bool SIPHandler::controlPTZSpeed(int nCmd, bool bEnable, int nSpeed)
{
	return controlPTZSpeedWithChannel(nCmd, bEnable, nSpeed, m_nChannel);
}

bool SIPHandler::presetPTZWithChannel(int nCmd, int nIndex, int nChannel)
{
	unsigned char nParam1 = nIndex & 0xFF;
	unsigned short nParam2 = nIndex >> 8;
	SIP_PTZ_RECT rect;
	memset(&rect, 0, sizeof(SIP_PTZ_RECT));
	unsigned char cPtzCmd = SIP_PTZCMD_STOP;
	switch (nCmd)
	{
	case PTZ_PRESET_SET:		//设置预置位
		cPtzCmd = SIP_PTZCMD_SET;
		break;
	case PTZ_PRESET_DELETE:		//删除预置位
		cPtzCmd = SIP_PTZCMD_DELETE;
		break;
	case PTZ_PRESET_GOTO:		//跳转预置位
		cPtzCmd = SIP_PTZCMD_GOTO;
		break;
	case PTZ_CRUISE_ADD:		//加入巡航点
		cPtzCmd = SIP_PTZCMD_CRUISE_ADD;
		break;
	case PTZ_CRUISE_DEL:		//删除巡航点
		cPtzCmd = SIP_PTZCMD_CRUISE_DEL;
		break;
	case PTZ_CRUISE_TIME:		//设置巡航停顿时间
		cPtzCmd = SIP_PTZCMD_CRUISE_DTIME;
		break;
	case PTZ_CRUISE_LOOP:        //开始巡航
		cPtzCmd = SIP_PTZCMD_CRUISE_RUN;
		break;
	case PTZ_CRUISE_STOPL:       //停止巡航
		cPtzCmd = SIP_PTZCMD_STOP;
		break;
	default:
		break;
	}
	sip.send_ptzcmd(m_szConnectKey, m_szDeviceID, cPtzCmd, nParam1,nParam2);
	return true;
}

bool SIPHandler::presetPTZ(int nCmd, int nIndex)
{
	return presetPTZWithChannel(nCmd, nIndex, m_nChannel);
}


RECORDFILE* SIPHandler::getRecordFile(int nType, char* startTime, char* endTime, int* nFileCount)
{

	return NULL;
}

int SIPHandler::getRecordFileEx(int nType, char* startTime, char* endTime, RECORDFILE pRecordFile[], int nMaxFileCount)
{
	return sip.send_recordinfo(m_szConnectKey, m_szDeviceID, nType, startTime, endTime, (record_info *)pRecordFile, nMaxFileCount);
}

LLONG SIPHandler::captureFileStream(LPCAPTURESTREAMCALLBACK lpCallBack, void* pUser, int nStreamType, RECORDFILE FileInfo, char* sType)
{
	m_nPlayBackTime = (int)sip_get_totalseconds(FileInfo.endTime) - (int)sip_get_totalseconds(FileInfo.startTime);
	if (m_nPlayBackTime <= 0)
		return 0;
	RECORDFILE recFile[30] = { 0 };
	int nCount = getRecordFileEx(SIP_RECORDTYPE_ALL, FileInfo.startTime, FileInfo.endTime, recFile, 30);
	if (nCount <= 0)
		printf("获取录像文件没有失败!\n");

	m_pStramCallback = lpCallBack;
	m_pStreamUser = pUser;

	char szTime[SIP_HEADER_LEN] = { 0 };
	sip_build_sdp_time(szTime, sizeof(szTime), FileInfo.startTime, FileInfo.endTime);
	invite_info info;
	memset(&info, 0, sizeof(invite_info));
	strcpy(info.addr, m_szConnectKey);
	strcpy(info.deviceid, m_szDeviceID);
	if (m_szTerminalID[0] != '\0')
		strcpy(info.terminalid, m_szTerminalID);
	strcpy(info.invite_time, szTime);
	info.invite_type = SIP_INVITE_PLAYBACK;
	info.carrysdp = true;
	sip.build_ssrc(info.ssrc, sizeof(info.ssrc), SIP_INVITE_PLAYBACK);
	m_nInviteId = sip.send_invite(&info, stream_cb, this);
	return (LLONG)m_nInviteId;
}



bool SIPHandler::controlReplayRecordFile(LLONG lReplayHandle, int nCmd, int nInValue, int* outValue)
{
	if (m_nInviteId <= 0)
		return false;
	if (outValue)
		*outValue = m_nPlayBackCurTime;
	unsigned char cmd = 0;
	int param = 0;
	int value = 0;
	switch (nCmd)
	{
	case REPLAY_CTRL_PLAYSTART:             //开始播放
		return true;
	case REPLAY_CTRL_PLAYSTOP:              //停止播放
		cmd = SIP_PLAYBACK_TEARDOWN;
		break;
	case REPLAY_CTRL_PLAYPAUSE:             //暂停播放
		cmd = SIP_PLAYBACK_PAUSE;
#ifdef _PLAY_PLUGIN
		if (m_pDecHandle)
			m_pDecHandle->pusePlayDecode(true);
#endif
		break;
	case REPLAY_CTRL_PLAYRESTART:           //恢复播放
		cmd = SIP_PLAYBACK_RESUME;
#ifdef _PLAY_PLUGIN
		if (m_pDecHandle)
			m_pDecHandle->pusePlayDecode(false);
#endif
		break;
	case REPLAY_CTRL_PLAYFAST:              //快放
		cmd = SIP_PLAYBACE_FAST;
#ifdef _PLAY_PLUGIN
		if (m_pDecHandle)
			m_pDecHandle->locPlay_Control(LOCPLAY_CTRL_PLAYFAST, 0, &value);
#endif
		break;
	case REPLAY_CTRL_PLAYSLOW:              //慢放
		cmd = SIP_PLAYBACK_SLOW;
#ifdef _PLAY_PLUGIN
		if (m_pDecHandle)
			m_pDecHandle->locPlay_Control(LOCPLAY_CTRL_PLAYSLOW, 0, &value);
#endif
		break;
	case REPLAY_CTRL_PLAYNORMAL:            //正常速度
		cmd = SIP_PLAYBACK_NORMAL;
#ifdef _PLAY_PLUGIN
		if (m_pDecHandle)
			m_pDecHandle->locPlay_Control(LOCPLAY_CTRL_PLAYNORMAL, 0, &value);
#endif
		break;
	case REPLAY_CTRL_PLAYFRAME:             //单帧放
	case REPLAY_CTRL_PLAYSTARTAUDIO:        //打开声音
	case REPLAY_CTRL_PLAYSTOPAUDIO:         //关闭声音
	case REPLAY_CTRL_PLAYAUDIOVOLUME:       //调节音量
		return false;
	case REPLAY_CTRL_PLAYGETPOS:            //获取文件回放的进度
		if (m_nPlayOpenTime > 0)
		{
			*outValue = ((m_nPlayCurTime - m_nPlayOpenTime) / 90000 + m_nPlayBackCurTime) * 100 / m_nPlayBackTime;
			if (m_bPlayOver)
				*outValue = 100;
			else
				if (*outValue >= 99)
					*outValue = 99;
		}
		return true;
	case REPLAY_CTRL_PLAYGETTIME:           //获取当前已经播放的时间
		if (m_nPlayOpenTime > 0)
		{
			*outValue = (m_nPlayCurTime - m_nPlayOpenTime) / 90000 + m_nPlayBackCurTime;
			if (m_bPlayOver)
				*outValue = m_nPlayBackCurTime;
			else
				if (*outValue >= (int)(m_nPlayBackTime*0.99))
					*outValue = (int)(m_nPlayBackTime*0.99);
		}
		return true;
	case REPLAY_CTRL_PLAYSETPOS:            //按进度改变回放进度
		cmd = SIP_PLAYBACK_SETTIME;
		param = m_nPlayBackTime * nInValue / 100;
		m_nPlayBackCurTime = param;
		m_nPlayOpenTime = 0;
		break;
	case REPLAY_CTRL_PLAYSETTIME:			//按时间改变回放进度
		cmd = SIP_PLAYBACK_SETTIME;
		param = nInValue;
		m_nPlayBackCurTime = nInValue;
		m_nPlayOpenTime = 0;
		break;
	case REPLAY_CTRL_GETTOTALTIME:          //获取当前播放文件总的时间
		*outValue = m_nPlayBackTime;
		return true;
	case REPLAY_CTRL_PLAYGETFRAME:          //获取当前已经播放的帧数
	case REPLAY_CTRL_GETTOTALFRAMES:        //获取当前播放文件总的帧数
		return false;
	default:
		return false;
	}
	return sip.send_info(m_nInviteId, cmd, param);
}

LLONG SIPHandler::replayRecordFile(LHWND hWnd, char* fileName, char* startTime, char* endTime, int nFileSize)
{
	if (m_nPlayBackTime == 0)
	{
		m_nPlayBackTime = (int)(sip_get_totalseconds(endTime) - sip_get_totalseconds(startTime));
		if (m_nPlayBackTime <= 0)
			return 0;
		m_nPlayBackIndex = 0;
		RECORDFILE recFile[60] = { 0 };
		int nCount = getRecordFileEx(SIP_RECORDTYPE_ALL, startTime, endTime, recFile, 60);
		if (nCount <= 0)
			printf("获取录像文件没有失败!\n");
		if (sip.m_conf.rec_type == 1)
		{
			m_vecPlayBack.clear();
			if (nCount >= 1)
			{
				int nSec = 0;
				for (size_t i = 0; i < nCount; i++)
				{
					REC_FILE file;
					memset(&file, 0, sizeof(file));
					file.nBeginSec = nSec;
					if (i == 0)					//第一个录像文件
					{
						strcpy(file.szBeginTime, startTime);
						strcpy(file.szEndTime, recFile[i].endTime);
						file.nEndSec = (int)(sip_get_totalseconds(recFile[i].endTime) - sip_get_totalseconds(startTime));
					}
					else if (i+1 == nCount)		//最后一个录像文件
					{
						strcpy(file.szBeginTime, recFile[i].startTime);
						strcpy(file.szEndTime, endTime);
						file.nEndSec = (int)(sip_get_totalseconds(endTime) - sip_get_totalseconds(recFile[i].startTime)) + file.nBeginSec;
					}
					else                         //中间录像文件
					{
						strcpy(file.szBeginTime, recFile[i].startTime);
						strcpy(file.szEndTime, recFile[i].endTime);
						file.nEndSec = (int)(sip_get_totalseconds(recFile[i].endTime) - sip_get_totalseconds(recFile[i].startTime)) + file.nBeginSec;
					}
					nSec = file.nEndSec;
					m_vecPlayBack.push_back(file);
				}
				endTime = recFile[0].endTime;
			}
			else
				return 0;
		}
	}
	m_hWnd = (HWND)hWnd;
	char szTime[SIP_HEADER_LEN] = { 0 };
	sip_build_sdp_time(szTime, sizeof(szTime), startTime, endTime);
	invite_info info;
	memset(&info, 0, sizeof(invite_info));
	strcpy(info.addr, m_szConnectKey);
	strcpy(info.deviceid, m_szDeviceID);
	if (m_szTerminalID[0] != '\0')
		strcpy(info.terminalid, m_szTerminalID);
	strcpy(info.invite_time, szTime);
	info.invite_type = SIP_INVITE_PLAYBACK;
	info.carrysdp = true;
	m_nInviteId = sip.send_invite(&info, stream_cb, this);
	return (LLONG)m_nInviteId;
}

bool SIPHandler::stopReplayRecordFile(LLONG lReplayHandle)
{
	if (m_nInviteId > 0)
	{
		sip.m_conf.log(LOG_INFO, "发送回放关闭请求![%d]\n", m_nInviteId);
		sip.send_bye(m_nInviteId, m_bCloseSream);
		m_nInviteId = 0;
	}
#ifdef _PLAY_PLUGIN
	if (m_pDecHandle)
	{
		stopPlayer();
	}
#endif
	m_pStramCallback = NULL;
	m_pStreamUser = NULL;
	m_bMakeKeyFrame = false;
	m_bFirstPacket = true;
	m_nFrameSize = 0;
	m_nTimestamp = 0;
	m_nStreamType = 0;
	m_nPlayOpenTime = 0;
	m_nPlayCurTime = 0;
	m_nPlayBackTime = 0;
	m_nPlayBackCurTime = 0;
	m_bPlayOver = false;
	m_bCloseSream = true;
	return true;
}

bool SIPHandler::playBackCaptureFile(LLONG lReplayHandle, char *pFileName)
{
#ifdef _PLAY_PLUGIN
	if (m_pDecHandle)
	{
		m_pDecHandle->capturePicture(pFileName);
	}
#endif
	return true;
}

bool SIPHandler::playBackSaveData(LLONG lReplayHandle, char *sFileName)
{
	m_fRecord = fopen(sFileName, "ab+");
	if (m_fRecord)
	{
		return true;
	}
	return false;
}

bool SIPHandler::stopPlayBackSave(LLONG lReplayHandle)
{
	if (m_fRecord)
	{
		fclose(m_fRecord);
		m_fRecord = NULL;
	}
	return true;
}

LLONG SIPHandler::downloadRecordFile(char* fileName, char* startTime, char* endTime, int nFileSize, char* saveFile, LPDOWNLOADRECORDFILECALLBACK lpCallBack, void* pUser)
{
	m_nPlayBackTime = (int)sip_get_totalseconds(endTime) - (int)sip_get_totalseconds(startTime);
	if (m_nPlayBackTime <= 0)
		return 0;

	RECORDFILE recFile[60] = { 0 };
	int nCount = getRecordFileEx(SIP_RECORDTYPE_ALL, startTime, endTime, recFile, 60);
	if (nCount <= 0)
		printf("获取录像文件没有失败!\n");

	m_fDownload = fopen(saveFile, "ab+");
	if (m_fDownload == NULL)
		return 0;
	strcpy(m_szFilePath, saveFile);
	char szTime[SIP_HEADER_LEN] = { 0 };
	sip_build_sdp_time(szTime, sizeof(szTime), startTime, endTime);
	invite_info info;
	memset(&info, 0, sizeof(invite_info));
	strcpy(info.addr, m_szConnectKey);
	strcpy(info.deviceid, m_szDeviceID);
	if (m_szTerminalID[0] != '\0')
		strcpy(info.terminalid, m_szTerminalID);
	strcpy(info.invite_time, szTime);
	info.invite_type = SIP_INVITE_DOWNLOAD;
	info.downloadspeed = 4;
	if (nFileSize > 0 && nFileSize < 20)
		info.downloadspeed = nFileSize;
	info.carrysdp = true;
	m_nInviteId = sip.send_invite(&info, stream_cb, this);
	return (LLONG)m_nInviteId;
}

bool SIPHandler::stopDownloadRecordFile(LLONG ldownFileHandle)
{
	if (m_nInviteId > 0)
	{
		sip.m_conf.log(LOG_INFO, "发送下载关闭请求![%d]\n", m_nInviteId);
		sip.send_bye(m_nInviteId, m_bCloseSream);
		m_nInviteId = 0;
	}
	m_pStramCallback = NULL;
	m_pStreamUser = NULL;
	m_bMakeKeyFrame = false;
	m_bFirstPacket = true;
	m_nFrameSize = 0;
	m_nTimestamp = 0;
	m_nStreamType = 0;
	m_nPlayOpenTime = 0;
	m_nPlayCurTime = 0;
	m_nPlayBackTime = 0;
	m_nPlayBackCurTime = 0;
	m_bPlayOver = false;
	m_bCloseSream = true;
	if (m_fDownload)
	{
		fclose(m_fDownload);
		m_fDownload = NULL;
	}
	memset(m_szFilePath, 0, sizeof(m_szFilePath));
	return true;
}


int SIPHandler::getDownloadPos(LLONG lFileHandle)
{
	int nTimePos = ((m_nPlayCurTime - m_nPlayOpenTime) / 90000) * 100 / m_nPlayBackTime;
	if (m_bPlayOver)
		return 100;
	else
		if (nTimePos >= 99)
			return 99;
	return nTimePos;
}

void SIPHandler::makeKey()
{
	m_bMakeKeyFrame = true;
}

bool SIPHandler::sendKeyFrame(unsigned char * buffer, unsigned int bufsize)
{
	if (!m_bMakeKeyFrame)	//不需要强制关键帧
		return true;
	for (unsigned int i = 0; i + 9 < bufsize; i++)
	{
		if ((BYTE)buffer[i] == 0 && (BYTE)buffer[i + 1] == 0 && (BYTE)buffer[i + 2] == 1 && (BYTE)buffer[i + 3] == 0xBC)
		{
			int nPeslen = 6 + buffer[i + 4] * 256 + buffer[i + 5];
			int nSteamTypePos = 10 + buffer[i + 8] * 256 + buffer[i + 9] + 2;
			if (nPeslen <= nSteamTypePos)
			{
				i++;
				continue;
			}
			m_bMakeKeyFrame = false;
			if (i + nSteamTypePos < bufsize)
			{
				if (buffer[i + nSteamTypePos] == 0x1B)
					m_nStreamType = 96;
				else if (buffer[i + nSteamTypePos] == 0x24)
					m_nStreamType = 98;
				else
					m_nStreamType = -1;
				//printf("镜头[%s]获取关键帧成功,码流类型为:%d\n", m_szDeviceID, m_nStreamType);
			}
			return true;	//找到关键帧
		}
	}
	return false;
}

void SIPHandler::stream_cb(int bufType, unsigned char * buffer, unsigned int bufsize, void * user)
{
	SIPHandler *pHandle = (SIPHandler *)user;
	if (pHandle == nullptr || pHandle->m_nInviteId == 0)
	{
		return;
	}
	if (bufType > 1)
	{
		int nErrorCode = 0;
		switch (bufType)
		{
		case 200:	//播放成功
			nErrorCode = 0;
			pHandle->m_nStreamType = bufsize;
			//printf("播放[%s]成功\n", pHandle->m_szDeviceID);
			break;
		case 400:	//播放失败
			nErrorCode = -30;
			printf("播放[%s]失败,设备离线！\n", pHandle->m_szDeviceID);
			break;
		case 403:	//连接超时
			nErrorCode = -31;
			//pHandle->reconnect();
			printf("播放[%s]失败,连接超时！\n", pHandle->m_szDeviceID);
			break;
		case 408:	//接收超时
			nErrorCode = -34;
			printf("播放[%s]失败,接收超时！\n", pHandle->m_szDeviceID);
			break;
		case 487:	//内部错误
			nErrorCode = -32;
			//pHandle->reconnect();
			printf("播放[%s]失败,码流己断开！\n", pHandle->m_szDeviceID);
			break;
		case 500:
			nErrorCode = -32;
			pHandle->m_bCloseSream = false;
			printf("播放[%s]失败,码流己断开！\n", pHandle->m_szDeviceID);
			break;
		case 121:
			pHandle->m_bPlayOver = true;
			break;
		default:
			nErrorCode = -30;
			printf("播放[%s]失败,ErrorCode:%d\n", pHandle->m_szDeviceID, bufType);
			break;
		}
		if (pHandle->m_pStramCallback != nullptr)
			pHandle->m_pStramCallback(nErrorCode, DVR_DATA_EVENT, NULL, 0, pHandle->m_pStreamUser);
#ifdef _PLAY_PLUGIN
		if (pHandle->m_pDrawCallback != nullptr)
			pHandle->m_pDrawCallback(nErrorCode, (LLONG)pHandle, 400, pHandle->m_pDrawUser);
#endif
	}
	if (bufType == 0 && buffer && bufsize > 0)
	{
		if (pHandle->m_fRtpRec)
		{
			fwrite(buffer, bufsize, 1, pHandle->m_fRtpRec);
		}
		u_long timestamp = ntohl(*((u_long *)&buffer[4]));
		if (pHandle->m_nPlayOpenTime == 0)
			pHandle->m_nPlayOpenTime = timestamp;
		int rtplen = 12;
		if ((unsigned char)buffer[0] == 0x90)
		{
			//rtplen += 2 + 2 + ntohs(*((u_short *)&buffer[14])) * 4;
			rtplen += 2 + 2 + (buffer[14]*256+buffer[15]) * 4;
		}
		if (pHandle->m_fRecord)
		{
			fwrite(buffer + rtplen, bufsize - rtplen, 1, pHandle->m_fRecord);
		}
		if (pHandle->m_fDownload)
		{
			pHandle->m_nPlayCurTime = timestamp;
			fwrite(buffer + rtplen, bufsize - rtplen, 1, pHandle->m_fDownload);
			return;
		}
		if (pHandle->m_bFirstPacket)	//第一个数据包
		{
			pHandle->m_bFirstPacket = false;
			if (pHandle->m_pStramCallback)
			{
#ifdef _PS_ANALYZE_USE
				PSStreamAnalyze *pAnalyze = new PSStreamAnalyze(0/*pHandle->m_nStreamType*/, pHandle->m_pStramCallback, pHandle->m_pStreamUser);
				pAnalyze->set_detachable(true);
				pAnalyze->start();
				pAnalyze->add_data(buffer + rtplen, bufsize - rtplen);
				pHandle->m_pAnalyzeThrd = pAnalyze;
#else
				if (pHandle->sendKeyFrame(buffer + rtplen, bufsize - rtplen))
					pHandle->m_pStramCallback((LLONG)pHandle, DVR_DATA_INIT, buffer + rtplen, bufsize - rtplen, pHandle->m_pStreamUser);
				else
					pHandle->m_pStramCallback((LLONG)pHandle, DVR_DATA_INIT, NULL, 0, pHandle->m_pStreamUser);
#endif				
			}
#ifdef _PLAY_PLUGIN
#ifdef _PLAY_TEST
			if (sip.m_conf.specify_declib == SIP_STREAM_GRG)
			{
				pHandle->initialPlayer(0, SIP_STREAM_GRG, NULL, 0, pHandle->m_nStreamType);
				if (pHandle->m_pDecCallback && pHandle->m_pDecHandle && pHandle->m_hWnd == NULL)
				{
					pHandle->m_pDecHandle->startPlayer4Standard(NULL, pHandle->m_pDecCallback, pHandle->m_nDecUser);
				}
				if (pHandle->m_pDrawCallback && pHandle->m_pDecHandle)
					pHandle->m_pDecHandle->startPlayer2(pHandle->m_hWnd, pHandle->m_pDrawCallback, pHandle->m_pDrawUser);
				else
					pHandle->startPlayer(pHandle->m_hWnd);
				pHandle->addData(buffer, bufsize);
				return;
			}
			if (sip.m_conf.specify_declib == SIP_STREAM_HIKVISION)
			{
				BYTE szHead[40] = { 0x49, 0x4D, 0x4B, 0X48, 0X01, 0X01, 0X00, 0X00,
					0X02, 0X00, 0X00, 0X01, 0X11, 0X71, 0X01, 0X10,
					0x40, 0x1F, 0x00, 0x00, 0x00, 0xFA, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				pHandle->initialPlayer(0, SIP_STREAM_HIKVISION, szHead, sizeof(szHead), pHandle->m_nStreamType);
				if (pHandle->m_pDecCallback && pHandle->m_pDecHandle && pHandle->m_hWnd == NULL)
				{
					pHandle->m_pDecHandle->startPlayer4Standard(NULL, pHandle->m_pDecCallback, pHandle->m_nDecUser);
				}
				if (pHandle->m_pDrawCallback && pHandle->m_pDecHandle)
					pHandle->m_pDecHandle->startPlayer2(pHandle->m_hWnd, pHandle->m_pDrawCallback, pHandle->m_pDrawUser);
				else
					pHandle->startPlayer(pHandle->m_hWnd);
				pHandle->addData(buffer, bufsize);
				return;
			}
			if ((unsigned char)buffer[1] == 0xA8)	//海康头
			{
				if (sip.m_conf.specify_declib == SIP_STREAM_DAHUA)
					pHandle->initialPlayer(0, SIP_STREAM_DAHUA, NULL, 0, 0);
				else
					pHandle->initialPlayer(0, SIP_STREAM_HIKVISION, buffer + rtplen, bufsize - rtplen, 0);
				if (pHandle->m_pDecCallback && pHandle->m_pDecHandle && pHandle->m_hWnd == NULL)
				{
					pHandle->m_pDecHandle->startPlayer4Standard(NULL, pHandle->m_pDecCallback, pHandle->m_nDecUser);
				}
				if (pHandle->m_pDrawCallback && pHandle->m_pDecHandle)
					pHandle->m_pDecHandle->startPlayer2(pHandle->m_hWnd, pHandle->m_pDrawCallback, pHandle->m_pDrawUser);
				else
					pHandle->startPlayer(pHandle->m_hWnd);
				return;
			}
#endif
			pHandle->initialPlayer(0, SIP_STREAM_DAHUA, NULL, 0, 0);
			if (pHandle->m_pDecCallback && pHandle->m_pDecHandle && pHandle->m_hWnd == NULL)
			{
				pHandle->m_pDecHandle->startPlayer4Standard(NULL, pHandle->m_pDecCallback, pHandle->m_nDecUser);
			}
			if (pHandle->m_pDrawCallback && pHandle->m_pDecHandle)
				pHandle->m_pDecHandle->startPlayer2(pHandle->m_hWnd, pHandle->m_pDrawCallback, pHandle->m_pDrawUser);
			else
				pHandle->startPlayer(pHandle->m_hWnd);
			if (pHandle->sendKeyFrame(buffer + rtplen, bufsize - rtplen))
				pHandle->addData(buffer + rtplen, bufsize - rtplen);
#endif
		}
		else                            //每N个数据包
		{
			pHandle->m_nPlayCurTime = timestamp;
			if (pHandle->m_pStramCallback)
			{
#ifdef _PS_ANALYZE_USE
				if (pHandle->m_pAnalyzeThrd)
					((PSStreamAnalyze *)pHandle->m_pAnalyzeThrd)->add_data(buffer + rtplen, bufsize - rtplen);
#else
				if (pHandle->sendKeyFrame(buffer + rtplen, bufsize - rtplen))
					pHandle->m_pStramCallback(pHandle->m_nStreamType, DVR_DATA_STREAM, buffer + rtplen, bufsize - rtplen, pHandle->m_pStreamUser);
#endif
			}
#ifdef _PLAY_PLUGIN
#ifdef _PLAY_TEST
			if (sip.m_conf.specify_declib == SIP_STREAM_GRG)
			{
				pHandle->addData(buffer, bufsize);
				return;
			}
#endif
			if (pHandle->sendKeyFrame(buffer + rtplen, bufsize - rtplen))
				pHandle->addData(buffer + rtplen, bufsize - rtplen);
#endif
		}
	}
}

void SIPHandler::reconnect()
{
	Reconnect *pConnect = new Reconnect(this);
	pConnect->set_detachable(true);
	pConnect->start();
}

/******************************************************************************************/

/****								以下为解码接口								       ****/

/******************************************************************************************/

bool SIPHandler::startPlayer4Standard(HWND hWnd, LPDecCBFun lpDecCBFun, int nCameraID)
{
#ifdef _PLAY_PLUGIN
	m_pDecCallback = lpDecCBFun;
	m_nDecUser = nCameraID;
#endif // !_NET_PLUGIN
	return startPlayerByCamera(hWnd, nCameraID);
}

bool SIPHandler::startPlayerByCamera(HWND hwndPlay, int nCameraID)
{
	//m_fRtpRec = fopen("rtp_stream_record.mp4", "ab+");
	makeKey();
#ifdef _PLAY_PLUGIN
	m_hWnd = hwndPlay;
	m_nChannel = nCameraID;

	invite_info info;
	memset(&info, 0, sizeof(invite_info));
	strcpy(info.addr, m_szConnectKey);
	strcpy(info.deviceid, m_szDeviceID);
	if (m_szTerminalID[0] != '\0')
		strcpy(info.terminalid, m_szTerminalID);
	info.invite_type = SIP_INVITE_PLAY;
	info.carrysdp = true;
	info.cseq = 20;
	m_nInviteId = sip.send_invite(&info, stream_cb, this);
#endif
	return m_nInviteId>0 ? true : false;
}

bool SIPHandler::stopPlayerByCamera(HWND hwndPlay, int nCameraID)
{
	if (m_fRtpRec)
	{
		fclose(m_fRtpRec);
		m_fRtpRec = NULL;
	}
#ifdef _PLAY_PLUGIN
	if (m_nInviteId > 0)
	{
		sip.send_bye(m_nInviteId, m_bCloseSream);
		m_nInviteId = 0;
	}
	if (m_pDecHandle)
	{
		stopPlayer();
	}
	m_pDecCallback = NULL;
	m_nDecUser = NULL;
	m_pDrawCallback = NULL;
	m_pDrawUser = NULL;
	m_bCloseSream = true;
#endif
	return true;
}

bool SIPHandler::capturePicture(char* sFilePath)
{
#ifdef _PLAY_PLUGIN
	if (m_pDecHandle)
	{
		m_pDecHandle->capturePicture(sFilePath);
	}
#endif
	return true;
}

bool SIPHandler::startRecord(char* saveFile)
{
	m_fRecord = fopen(saveFile, "ab+");
	if (m_fRecord)
	{
		return true;
	}
	return false;
}

bool SIPHandler::stopRecord()
{
	if (m_fRecord)
	{
		fclose(m_fRecord);
		m_fRecord = NULL;
	}
	return true;
}


bool SIPHandler::startPlayer2(HWND hWnd, LPDRAWCBCALLLBACK lpDrawCBFun, void *pUser)
{
#ifdef _PLAY_PLUGIN
	m_pDrawCallback = lpDrawCBFun;
	m_pDrawUser = pUser;
#endif
	return true;
}

bool SIPHandler::setDVRConfig(DWORD dwCommand, LONG lChannel, LPVOID lpInBuffer, DWORD dwInBufferSize)
{
	return true;
}

bool SIPHandler::getDVRConfig(DWORD dwCommand, LONG lChannel, LPVOID lpOutBuffer, DWORD dwOutBufferSize, LPDWORD lpBytesReturned)
{
	return true;
}

bool SIPHandler::initialPlayer(int nPort, int nDecodeType, BYTE* buff, int nSize, int nStreamType)
{
#ifdef _PLAY_PLUGIN
#ifdef _PLAY_TEST
	char szDllType[12] = { 0 };
	switch (nDecodeType)
	{
	case SIP_STREAM_HIKVISION:
		strcpy(szDllType, "HK");
		break;
	case SIP_STREAM_GRG:
		strcpy(szDllType, "GRG");
		break;
	default:
		strcpy(szDllType, "DH");
		break;
	}
	if (hDecModule == NULL)
	{
		char szDllPath[256] = { 0 };
		sprintf(szDllPath, "%s\\PlugIn\\%sPlugIn\\%sPlayPlugIn.dll", sip.m_conf.cwd, szDllType, szDllType);
		hDecModule = LoadLibraryEx(szDllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (hDecModule == NULL)
		{
			//sip.m_conf.log(LOG_ERROR, "Load PlayerDll false[%s]", szDllPath);
			sprintf(szDllPath, "%s\\PlugIn\\SIPPlugIn\\Player\\%s\\%sPlayPlugIn.dll", sip.m_conf.cwd, szDllType, szDllType);
			hDecModule = LoadLibraryEx(szDllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (hDecModule == NULL)
			{
				sip.m_conf.log(LOG_ERROR, "Load PlayerDll false[%s]", szDllPath);
				return false;
			}
		}
	}
#else
	if (hDecModule == NULL)
	{
		char szDllPath[256] = { 0 };
		sprintf(szDllPath, "%s\\PlugIn\\DHPlugIn\\DHPlayPlugIn.dll", sip.m_conf.cwd);
		hDecModule = LoadLibraryEx(szDllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (hDecModule == NULL)
		{
			sip.m_conf.log(LOG_ERROR, "Load PlayerDll false[%s]", szDllPath);
			return false;
		}
	}
#endif
	m_getHandler = (getHandlerFun)GetProcAddress(hDecModule, "getHandler");
	if (m_getHandler == NULL)
	{
		return false;
	}
	m_pDecHandle = m_getHandler();
	if (m_getHandler)
	{
		return m_pDecHandle->initialPlayer(nPort, 0, buff, nSize, nStreamType);
	}
#endif
	return false;
}

bool SIPHandler::startPlayer(HWND hWnd)
{
#ifdef _PLAY_PLUGIN
	if (m_pDecHandle)
		return m_pDecHandle->startPlayer(hWnd);
#endif
	return false;
}

bool SIPHandler::addData(BYTE* buff, int nSize)
{
#ifdef _PLAY_PLUGIN
	if (m_pDecHandle)
		return m_pDecHandle->addData(buff, nSize);
#endif
	return false;
}

bool SIPHandler::stopPlayer()
{
#ifdef _PLAY_PLUGIN
	stopRecord();
	if (m_pDecHandle)
	{
		m_pDecHandle->stopPlayer();
		m_pDecHandle->releaseHandler();
		m_pDecHandle = NULL;
		m_getHandler = NULL;
		m_hWnd = NULL;
		m_bFirstPacket = true;
		//FreeLibrary(hDecModule);
		//hDecModule = NULL;
	}
#endif
	return true;
}

bool SIPHandler::alarm_startCapture(int nPort, LPALARMCALLBACK lpCallBack)
{
	switch (nPort)
	{
	case 1:
		sip.send_position(m_szConnectKey, m_szDeviceID);
		break;
	case 2:
		sip.send_subscribe(m_szConnectKey, m_szDeviceID, 1, SIP_ALARM_GPS);
		break;
	default:
		break;
	}
	return true;
}

bool SIPHandler::alarm_stop()
{
	return true;
}