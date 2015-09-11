#include <stdio.h>
#include <time.h>
#include <string>
#include <sys/ioctl.h>
#include <boost/locale.hpp>
#include <linux/rtc.h> 
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <math.h>

#include "VDRRunParaTable.h"
#include "VDRTrackInfoTable.h"
#include "ElectronicFenceTable.h"
#include "DriversLoginTable.h"
#include "VDRDeployParaTable.h"
#include "VDRAccelerateTable.h"
#include "VDRPreTiredTable.h"
#include "VDRBaseConfigTable.h"
#include "VDRDregInfoTable.h"

#include "GeneralTimer.h"
#include "LogUtility.h"
#include "Utility.h"

#include "dbusApplication.h"
#include "dbusClient.h"
#include "dbusServer.h"

#include "phoneMessage.h"
#include "JT808DbusMessage.h"
#include "JT8082DbusMessage.h"
#include "NaviDbusMessage.h"
#include "DeviceManagerDbusMessage.h"
#include "mcuprotocol.h"
#include "serialprotocol2012.h"
#include "devicemanager.h"

#include "workq.h"
#include "errors.h"

class HyPreTiredWriteTable:public WriteTableHandlerInterface
{
	public:
		HyPreTiredWriteTable()
		{
			memset(&pretired_param,0,sizeof(PRETIRED_DRIVING));
		}
		virtual void WriteData(void *data);
		struct PRETIRED_DRIVING pretired_param;
		static VDRHyPreTiredTable *table;
};
workq_t workq_pretired;
workq_t workq_tired;
workq_t workq_runst;
workq_t workq_locst;
workq_t workq_accident;
workq_t workq_dayinfo;
//#define _WY_
//#define SLEEP_UP_POSITION  1

//#define IC_SUPPORT_UNKNOWN  1
//#define NORMAL_3C_ACCIDENT  1
#define W_TABLE 1

static unsigned char startup_buf[512];
static int startup_data_num = 0;
static int startup_time_flage = 0;
static int speed_zero = 1;
static int first_process_ic_card = 1;
static unsigned char un_speed_buf[128];
static int un_speed_len = 0;
static pthread_mutex_t unbuf_mutex;

DBusClientC *sp_ClientJT8082;
DBusClientC *sp_ClientJT808;
DBusServerC	*dev_DbusServer;
DBusClientC *sp_ClientGps;

GeneralTimerItem *mcutimer = NULL;             //read mcu time
GeneralTimerItem *uploadtimer = NULL;          // upload position info fixed time
GeneralTimerItem *tmpuploadtimer = NULL;       // tmp once upload position info fixed time
GeneralTimerItem *linetimer = NULL;            // line travel  max  time 
GeneralTimerItem *speedtimer = NULL;           // overspeed duration time   
GeneralTimerItem *parktimer = NULL;            //max parking time
GeneralTimerItem *sleeptimer = NULL;           //driver continue driving time 
GeneralTimerItem *statustimer = NULL;          //speed status
GeneralTimerItem *picturetimer = NULL;         // time take pictures
GeneralTimerItem *naviencrypttimer = NULL;         // get navi encrypt status
GeneralTimerItem *vsstimer = NULL;
GeneralTimerItem *ignitiontimer = NULL;
DeviceTiredDbusNotifyC   *tirednotify=NULL;

static pthread_mutex_t udp_mutex;
static pthread_mutex_t gps_mutex;
pthread_mutex_t engine_list_mutex_run;
pthread_mutex_t engine_list_mutex_loc;
    
static struct VDRSettingParam  vdr_param;
static struct VDRCurrentInfo vdr_current_info;
static struct ElectronicFenceData fence_info;
static struct AdditionalInformation add_info;
static struct PRETIRED_DRIVING  pretired_param,pretired_param_temp;
static struct SpeedMileageInfo speed_distance;
static struct NaviGPSInfo   hx_info[200];
static int hx_info_num = 0;
static int hx_info_flag = 0;
static int hx_info_begin_flag = 0;
static int gd_inout_time = 0;
static int gd_inout_flag = 0;
static int gd_line_flag = 0;
static int gd_line_up_time = 0;
static int gnss_mode = 3;
static struct NaviGPSInfo   gps_info;   //now sec
static struct NaviGPSInfo   loc_info;   //first sec speed=0
static struct NaviGPSInfo   norm_info;  //
static struct SpeedStatusJudge speed_info;
static struct MinSpeedLocInfo  min_info;
static struct LastSendLocData last_send_loc;
static int temp_min = 0;
static int first_getdata_flag = 1;
static int first_set_time_flag = 0;
static int onesec_upload_flag = 0;
static int un_position_count = 0;
static int in_position_count = 0;
struct LocStatusData udp_loc;
static int udp_loc_time = 0;
struct BaseData gui_data;
static int iccard_fd;
static struct ICcardInfo iccard_info;
static struct DregInfoData dreg_info;
static struct DregSensorInfo sensor_info;

VDRLocStatus1Table loc1table;
VDRLocStatus2Table loc2table;
static int total_time_start=-1;
static int flag_total_time=0;
static int total_time_tmp;
static int proc_tired_flag = 0;

static int authentication_result = 1; // 0鉴权成功
static int once_alarm=0;
static unsigned int vdr_status_flag = 0;
static unsigned int vdr_alarm_flag = 0;
static unsigned int tmp_alarm_flag = 0;
static unsigned int tmp_status_flag = 0;
static bool Flag_DriverLogin = 0;                          	//驾驶员登录状态   1:登录，0:未登录
static float tempmileage = 0;
static float picturedistance = 0;
static int exit_card = 0;
static int ICChkCount=0;
static int flag_readdone = 1;//本次卡读取完毕的标志
static int cst_fd = -1;
static int sensor_fd = -1;
static int speed_fd = -1;
static unsigned char  mcu_data[50];
static unsigned char  tmp_data[20];
static int mcudata_len= 0;
static int speed_zero_flag = 0;
static int speed_zero_count = 0;
static int speed_data_count = 0;
static int shutdown_reboot_flag = 0;
static int check_time_flag = 0;
static int gps_position_count = 0;
static int write_accident_flag = 0;
static int begin_can_test = 0;
    
static int socket_dp;
static int socket_recvfd;
struct sockaddr_in addr;
struct sockaddr_in addr_recv;

static int flag_prefatigue=0;
static int reverse_icount = 1; 

static unsigned char startup_mode = 0; 

FILE *fp_run = NULL;
FILE *fp_loc = NULL;
FILE *fp_08 = NULL;
FILE *fp_09 = NULL;
static int total_08 = 0;
static int total_09 = 0;
static int current_08 = 0;
static int current_09 = 0;
static int houre_09 = 0;
static unsigned char buf_08[126];
static unsigned char buf_09[666];
static int num_loc = 0;
struct LocStatusData loc_buf[5];
static int num_pretired_w = 0;
static int total_run = 0;
static int current_run = 0;

pthread_t insgps_tid;
static int insgps_begin = 0;
static unsigned char insbuf[9];

extern int not_process_loc1;
extern int mileage_flag;
extern int sp_fd;


VDRHyPreTiredTable * HyPreTiredWriteTable::table= new VDRHyPreTiredTable();
VDRTiredTable * TiredWriteTable::table=new VDRTiredTable(); 
VDRAccidentTable*  AccidentWriteTable::accident = new VDRAccidentTable();
VDRDayInfoTable* DayInfoWriteTable::table=new VDRDayInfoTable();

enum READ_ZONE
{
	DATA_ZONE,//数据区
	PROC_ZONE,//保护区
	SCUR_ZONE //密码区
};

struct sle5542_ioctl_data {	
	unsigned int  userzone;		//区域
	unsigned char password[4];  //密码
};

#define SLE5542_IOCTL_BASE      'C'
#define SLE5542_READ_ZONE        _IOW(SLE5542_IOCTL_BASE, 0, struct sle5542_ioctl_data)	//选择读写区域
#define SLE5542_CKECK_PASSWD     _IOW(SLE5542_IOCTL_BASE, 1, struct sle5542_ioctl_data) //验证密码
#define SLE5542_CHANGE_PASSWD    _IOW(SLE5542_IOCTL_BASE, 2, struct sle5542_ioctl_data) //改写密码


int  change_alarm_flag_and_upload(int flag,int set);
void change_report_time_distance_mode(int type);
void write_track_accident_from_buf(int type, int dif, struct NaviGPSInfo tmpinfo);
void write_track_accident_table(int type, struct NaviGPSInfo tmpinfo);
void write_track_speed_info_table();
void check_speed_status_info(int vdr_speed, int gps_speed);
void write_vdr_day_info_table(int itime);
void *udp_send_read_can_data(void *);
void read_d310_can_info_thread(int vflag );

void PreTired_Write();
void TiredDriving_Write(int );
void AllTiredDriving_Write(int );
int AllTiredDriving_Max_time();
int write_08H_vdr_file();
int write_09H_vdr_file();
void prepare_09H_buf_data( int itime, struct NaviGPSInfo *locstatus);

void process_driver_sleep_begin(int itime);
void process_driver_tired_begin(int itime);
void process_driver_tired_info(int itime);



void HyPreTiredWriteTable::WriteData(void * data)
{
	//VDRPreTiredTable  *table = new VDRPreTiredTable();
	table->SetStringValue(VDR_HY_PRETIRED_DRIVER_LICENSE_NUM, pretired_param.VehicleDriverID);
	table->SetIntValue(VDR_HY_PRETIRED_BEGIN_TIME, pretired_param.TiredTime);
	table->SetIntValue(VDR_HY_PRETIRED_BEGIN_LONGITUDE, pretired_param.TiredLongitude);
	table->SetIntValue(VDR_HY_PRETIRED_BEGIN_LATITUDE, pretired_param.TiredLatitude);
	table->SetIntValue(VDR_HY_PRETIRED_BEGIN_ALTITUDE, pretired_param.TiredAltitude);
	table->SetIntValue(VDR_HY_PRETIRED_END_TIME, pretired_param.StoppingTime);
	table->SetIntValue(VDR_HY_PRETIRED_END_LONGITUDE, pretired_param.StopLongitude);
	table->SetIntValue(VDR_HY_PRETIRED_END_LATITUDE, pretired_param.StopLatitude);
	table->SetIntValue(VDR_HY_PRETIRED_END_ALTITUDE, pretired_param.StopAltitude);
	table->SetIntValue(VDR_HY_PRETIRED_RUNNING_TIME, pretired_param.RunningTime);
	table->SetIntValue(VDR_HY_PRETIRED_DRIVING_TOTAL_TIME, pretired_param.TotalTime);
	table->SetIntValue(VDR_HY_PRETIRED_METER_TODAY, pretired_param.TripMeterCurDay);
	table->SetIntValue(VDR_HY_PRETIRED_RUN_TIME, pretired_param.RunningTimeCurDay);
	table->SetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, pretired_param.Flag_TiredDrive);
	table->SetIntValue(VDR_HY_PRETIRED_METER_TODAY, 0);
	table->SetIntValue(VDR_HY_PRETIRED_RUN_TIME,0);
	table->Commit();
	//delete table;
}
void TiredWriteTable::WriteData(void * data)
{
	table->SetIntValue(VDR_TIRED_BEGIN_TIME, pretired_param.TiredTime);
	table->SetIntValue(VDR_TIRED_END_TIME, pretired_param.StoppingTime);
	table->SetStringValue(VDR_TIRED_DRIVER_LICENSE_NUM, pretired_param.VehicleDriverID);
	table->SetIntValue(VDR_TIRED_BEGIN_LONGITUDE, pretired_param.TiredLongitude);
	table->SetIntValue(VDR_TIRED_BEGIN_LATITUDE, pretired_param.TiredLatitude);
	table->SetIntValue(VDR_TIRED_BEGIN_ALTITUDE, pretired_param.TiredAltitude);
	table->SetIntValue(VDR_TIRED_END_LONGITUDE, pretired_param.StopLongitude);
	table->SetIntValue(VDR_TIRED_END_LATITUDE, pretired_param.StopLatitude);
	table->SetIntValue(VDR_TIRED_END_ALTITUDE, pretired_param.StopAltitude);
	table->Commit();
}
void LocStatusWriteTable::WriteData(void * data)
{ 
    pthread_mutex_lock (&engine_list_mutex_loc);
    LargeTableBase * table;

    if(locstatus_data.type == 1)
        table = &loc1table;
    else
        table = &loc2table;
    
    table->SetIntValue(VDR_LOCS_OCCUR_TIME, locstatus_data.itime);
    table->SetIntValue(VDR_LOCS_ALARM_SIGNAL, locstatus_data.alarm_signal);
    table->SetIntValue(VDR_LOCS_STATUS_FLAG, locstatus_data.status_falg);
    table->SetIntValue(VDR_LOCS_LATITUDE, locstatus_data.latitude);
    table->SetIntValue(VDR_LOCS_LONGITUDE, locstatus_data.longitude);
    table->SetIntValue(VDR_LOCS_ALTITUDE, locstatus_data.altitude);
    table->SetIntValue(VDR_LOCS_ORIENTATION, locstatus_data.orientation);
    table->SetIntValue(VDR_LOCS_GPS_SPEED, locstatus_data.gps_speed);    //0.1km/h
    table->SetIntValue(VDR_LOCS_TOTAL_MILEAGE, locstatus_data.total_mileage);  //0.1km
    table->SetIntValue(VDR_LOCS_OIL_MASS, 0);
    table->SetIntValue(VDR_LOCS_VDR_STATUS, locstatus_data.vdr_status_old_hb);
    table->SetIntValue(VDR_LOCS_VDR_NEW_HB_STATUS, locstatus_data.vdr_status_new_hb);
    table->SetIntValue(VDR_LOCS_VDR_SPEED, locstatus_data.vdr_speed);  //0.1km/h
    table->SetIntValue(VDR_LOCS_VDR_DISTANCE, locstatus_data.vdr_distance); // m
    table->SetIntValue(VDR_LOCS_OVERSPEED_ALARM, locstatus_data.overspeed_flag);
    table->SetIntValue(VDR_LOCS_OVERSPEED_REGIONID, locstatus_data.overspeed_id);
    table->SetIntValue(VDR_LOCS_INOUT_ALARM, locstatus_data.fence_inout_alarm);
    table->SetIntValue(VDR_LOCS_INOUT_REGIONID, locstatus_data.fence_inout_id);
    table->SetIntValue(VDR_LOCS_INOUT_DIRECTION, locstatus_data.fence_inout_direction);
    table->SetIntValue(VDR_LOCS_SHORT_LONG_LINEID, locstatus_data.fence_line_id);
    table->SetIntValue(VDR_LOCS_SHORT_LONG_TIME, locstatus_data.fence_line_time);
    table->SetIntValue(VDR_LOCS_SHORT_LONG_RESULT, locstatus_data.fence_line_result);
    table->SetIntValue(VDR_LOCS_EMERGENCY_ALARM_ID, locstatus_data.emergency_num);
    table->SetIntValue(VDR_LOCS_IO_STATUS, locstatus_data.io_status);
    table->SetIntValue(VDR_LOCS_ANALOG_QUANTITY, locstatus_data.analog_quantity);
    table->SetIntValue(VDR_LOCS_WIRELESS_INTENSITY, locstatus_data.wireless_intensity);
    table->SetIntValue(VDR_LOCS_GNSS_SATELLITE_SUM, locstatus_data.gnss_satellite_sum);
    table->Add();

    pthread_mutex_unlock (&engine_list_mutex_loc);
}

void AccidentWriteTable::WriteData(void * data)
{ 
    accident->SetIntValue(VDR_ACCIDENT_OCCUR_TIME, accident_data.itime);  
    accident->SetSequenceValue(VDR_ACCIDENT_SPEED, accident_data.speed);
    accident->SetSequenceValue(VDR_ACCIDENT_STATUS, accident_data.status);
    accident->SetStringValue(VDR_ACCIDENT_DRIVER_LICENSE_NUM, accident_data.driverid);
    accident->SetIntValue(VDR_ACCIDENT_LONGITUDE, accident_data.longitude);
    accident->SetIntValue(VDR_ACCIDENT_LATITUDE, accident_data.latitude);
    accident->SetIntValue(VDR_ACCIDENT_ALTITUDE, accident_data.ialtitude);
    accident->Commit();
}
void DayInfoWriteTable::WriteData(void * data)
{
    table->SetIntValue(VDR_DAY_OCCUR_TIME, dayinfo_data.itime);
    table->SetIntValue(VDR_DAY_DRIVE_DURA, dayinfo_data.drive_dura);
    table->SetIntValue(VDR_DAY_DRIVE_MILEAGE, dayinfo_data.drive_mileage);  //m
    table->SetIntValue(VDR_DAY_TOTAL_MILEAGE, dayinfo_data.total_mileage); // m
    table->Commit();
}

int get_day_zero_time(int itime)
{
    time_t timep;
    struct tm p;
    
    timep = itime;
    localtime_r(&timep, &p);
    p.tm_hour = 0;
    p.tm_min = 0;
    p.tm_sec = 0;
    timep = mktime(&p);
    return (int)timep;
}
int get_signed_longitude_latitude(int direction, int data)
{
    int value;

    if(!direction)
        value = data*6/10;
    else
        value = 0-data*6/10;
    return value;
}

int  compute_vdrspeed_gpsspeed(int vdr, int gps)
{
    int tmp = 0;
    
    tmp = abs(vdr-gps);
    
    if((tmp*100/gps)<11)
        return SPEED_STATUS_OK;
    else
        return SPEED_STATUS_ERROR;
}
int send_device_SMS_alarm_notify(int flag)
{
    if(!(vdr_param.warning_SMS_mask & flag))
        return -1;
 
    DeviceSMSAlarmNotifyC notify;

    notify.s_alarm = flag;
    if(gps_info.position)
        notify.s_itime = gps_info.iTime;
    else
        notify.s_itime = (int)time(NULL);
    notify.s_fx = gps_info.fX;
    notify.s_fy = gps_info.fY;

    dev_DbusServer->sendNotify(&notify);
    return 0;
}

int send_device_alarm_camera_notify(int flag)
{
    if(!(vdr_param.warning_photo_mask & flag))
        return -1;
    
    DeviceCameraDbusNotifyC notify;

    notify.m_channel = 0x08; //cvbs  0x0F; //四个通道各拍1张
    notify.m_command = 1;
    notify.m_interval = 0;
    if(vdr_param.warning_photosave_mask & flag)
        notify.m_flag = 0x00;
    else
        notify.m_flag = 0x0F;
    notify.m_resolution = 0x02;
    if(flag == ALARM_STATUS_CAR_COLLISION_ALARM || flag == ALARM_STATUS_CAR_TURNOVER_ALARM)
        notify.m_event = 3;
    else
        notify.m_event = 8;
    dev_DbusServer->sendNotify(&notify);
    
    return 0;
}
int send_device_alarm_notify(int type, int flag)
{
    DeviceAlarmDbusNotifyC alarmnotify;
    alarmnotify.type = type;
    alarmnotify.flag = flag;
    dev_DbusServer->sendNotify(&alarmnotify);
    return 0;
}
int send_device_runstop_notify(int flag, int itime)
{
    DeviceRunStopDbusNotifyC notify;
    notify.flag = flag;
    notify.itime = itime;
    dev_DbusServer->sendNotify(&notify);
    return 0;
}
void printf_alarm_info(int alarm)
{
    printf("printfalarm=%d 报警: ",alarm);
    if(alarm & ALARM_STATUS_EMERGENCY_ALARM)
    {
        printf("0紧急 ");
    }
    if(alarm & ALARM_STATUS_OVERSPEED_ALARM)
    {
        printf("1超速 ");
    }
    if(alarm & ALARM_STATUS_FATIGUE_DRIVING)
    {
        printf("2疲劳 ");
    }
    if(alarm & ALARM_STATUS_EARLY_WARNING)
    {
        printf("3危险 ");
    }
    if(alarm & ALARM_STATUS_GNSS_FAILURE)
    {
        printf("4GNSS故障 ");
    }
    if(alarm & ALARM_STATUS_GNSS_UNCONNECT)
    {
        printf("5GNSS未接 ");
    }
    if(alarm & ALARM_STATUS_GNSS_SHORT_CIRCUIT)
    {
        printf("6GNSS短路 ");
    }
    if(alarm & ALARM_STATUS_POWER_UNDER_VOLTAGE)
    {
        printf("7欠压 ");
    }
    if(alarm & ALARM_STATUS_POWER_DOWN_ELECTRICITY)
    {
        printf("8掉电 ");
    }
    if(alarm & ALARM_STATUS_LCD_FAILURE)
    {
        printf("9LCD ");
    }
    if(alarm & ALARM_STATUS_TTS_FAILURE)
    {
        printf("10TTS ");
    }
    if(alarm & ALARM_STATUS_CAMERA_FAILURE)
    {
        printf("11摄像头 ");
    }
    if(alarm & ALARM_STATUS_IC_CARD_FAILURE)
    {
        printf("12IC卡 ");
    }
    if(alarm & ALARM_STATUS_OVERSPEED_WARNING)
    {
        printf("13超速预警 ");
    }
    if(alarm & ALARM_STATUS_PREFATIGUE_DRIVING)
    {
        printf("14疲劳预警 ");
    }
    if(alarm & ALARM_STATUS_SMOKE_ALARM)
    {
        printf("17烟雾 ");
    }
    if(alarm & ALARM_STATUS_DAY_DRIVING_OVERTIME)
    {
        printf("18累计超时 ");
    }
    if(alarm & ALARM_STATUS_PARKING_OVERTIIME)
    {
        printf("19停车 ");
    }
    if(alarm & ALARM_STATUS_IN_OUT_AREA)
    {
        printf("20进出区域 ");
    }
    if(alarm & ALARM_STATUS_IN_OUT_LINE)
    {
        printf("21进出路线 ");
    }
    if(alarm & ALARM_STATUS_ROAD_DRIVE_SHORT_LONG)
    {
        printf("22路段过长 ");
    }
    if(alarm & ALARM_STATUS_LINE_DEPARTURE_ALARM)
    {
        printf("23路线偏离 ");
    }
    if(alarm & ALARM_STATUS_CAR_VSS_FAILURE)
    {
        printf("24VSS故障 ");
    }
    if(alarm & ALARM_STATUS_CAR_OIL_FAILURE)
    {
        printf("25油量异常 ");
    }
    if(alarm & ALARM_STATUS_CAR_STOLEN_ALARM)
    {
        printf("26被盗 ");
    }
    if(alarm & ALARM_STATUS_CAR_ILLEGAL_IGNITION)
    {
        printf("27非法点火 ");
    } 
    if(alarm & ALARM_STATUS_CAR_ILLEGAL_OFFSET)
    {
        printf("28非法位移 ");
    }
    if(alarm & ALARM_STATUS_CAR_COLLISION_ALARM)
    {
        printf("29碰撞 ");
    }
    if(alarm & ALARM_STATUS_CAR_TURNOVER_ALARM)
    {
        printf("30侧翻 ");
    }
    if(alarm & ALARM_STATUS_CAR_ILLEGAL_DOOR_ALARM)
    {
        printf("31非法开门 ");
    }
    
    printf("\n");
}
int send_position_to_JT808_udp(int itime)
{
    struct sockaddr_in address;
    int port = 8901;
    int ret = 0;
    struct sockaddr_in address2;
    int port2 = 8921;

    memset(&address, 0,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);

    memset(&address2, 0,sizeof(address2));
    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = inet_addr("127.0.0.1");
    address2.sin_port = htons(port2);

    pthread_mutex_lock(&udp_mutex);
    if(udp_loc.latitude != 0 && (vdr_status_flag & LOCSTATUS_FIXED_POSITION))
    {
        last_send_loc.latitude = udp_loc.latitude;
        last_send_loc.longitude = udp_loc.longitude;
        last_send_loc.altitude = udp_loc.altitude;
        last_send_loc.orientation = udp_loc.orientation;
        //last_send_loc.gps_speed = udp_loc.gps_speed;
    }

    if(last_send_loc.latitude != 0 && !(vdr_status_flag & LOCSTATUS_FIXED_POSITION))
    {
        udp_loc.latitude = last_send_loc.latitude;
        udp_loc.longitude = last_send_loc.longitude;
        udp_loc.altitude = last_send_loc.altitude;
        udp_loc.orientation = last_send_loc.orientation;
        //udp_loc.gps_speed = last_send_loc.gps_speed;
    }
    
    if(udp_loc_time != udp_loc.itime)
    {
        udp_loc_time = udp_loc.itime;
        ret = sendto(socket_dp, &udp_loc, sizeof(struct LocStatusData), 0, (struct sockaddr *)&address, sizeof(address));
        ret = sendto(socket_dp, &udp_loc, sizeof(struct LocStatusData), 0, (struct sockaddr *)&address2, sizeof(address2));
        printf("dev_udp: time=%d,alarm=%d,staus=%d, fx=%d, fy=%d,gspeed=%d,vspeed=%d,fence:(%d,%d,%d),mileage=%d\n",udp_loc.itime,udp_loc.alarm_signal,udp_loc.status_falg,
            udp_loc.longitude,udp_loc.latitude,udp_loc.gps_speed,udp_loc.vdr_speed,udp_loc.fence_inout_alarm,udp_loc.fence_inout_id,udp_loc.fence_inout_direction,udp_loc.total_mileage);
        //printf_alarm_info(udp_loc.alarm_signal);
    }
    pthread_mutex_unlock(&udp_mutex);
    if(ret == -1)
    {
        perror("sendto loc1 call failed");
    } 
    return -1;
}

int send_change_gnss_mode_udp(int *buf, int len)
{
    struct sockaddr_in address;
    int port = 8902;
    int ret = 0;

    memset(&address, 0,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);

    ret = sendto(socket_dp, buf, len, 0, (struct sockaddr *)&address, sizeof(address));

    if(ret == -1)
    {
        perror("sendto gnss udp failed\n");
    } 
    return -1;
}

void take_picture_control_handler(string &name, void *data, void *data2)
{
    DeviceCameraDbusNotifyC notify;

    if((int)data == PICTURE_TIMER_DATA)
    {
        notify.m_channel = vdr_param.capture_control.time_capture_switch;
        notify.m_command = 1;
        notify.m_interval = 0;
        notify.m_flag = vdr_param.capture_control.time_capture_store;
        notify.m_resolution = 0x02;
        notify.m_event = 1;
        dev_DbusServer->sendNotify(&notify);
    }
    else if((int)data == PICTURE_DISTANCE_DATA)
    {
        notify.m_channel = vdr_param.capture_control.distance_capture_switch;
        notify.m_command = 1;
        notify.m_interval = 0;
        notify.m_flag = vdr_param.capture_control.distance_capture_store;
        notify.m_resolution = 0x02;
        notify.m_event = 7;
        dev_DbusServer->sendNotify(&notify);
    }
    
}
static int gdcount = 0;
void report_position_handler(string &name, void *data, void *data2)
{
    if((int)data == TEMP_TIMER_DATA)
    {
        if((vdr_current_info.temp_report_validity !=0) &&(int)time(NULL) > vdr_current_info.temp_report_validity)
        {
            if(vdr_current_info.is_time_report)
                change_report_time_distance_mode(DEFAULT_MODE);
            else
                StopTimer(uploadtimer);
            return;
        }
    }
    else if((int)data == TUPLOAD_TIMER_DATA)
    {
        StopTimer(tmpuploadtimer);
        uploadtimer->timeout = vdr_current_info.report_time*1000;
        uploadtimer->timeleft = uploadtimer->timeout;
        printf("start upposition timer 111 =%d\n",uploadtimer->timeout);
        StartTimer(uploadtimer);
    }
    if(gd_line_flag == 1)
    {
        gd_line_up_time = udp_loc.itime;
        gd_line_flag = 2;
        gdcount = 0;
    }    

    if(gd_line_flag == 2)
    {
        udp_loc.itime = gd_line_up_time+30*gdcount;
        gdcount++;
    }
    
    send_position_to_JT808_udp(-1);

    return;
}

void overtime_overspeed_alarm_handler(string &name, void *data, void *data2)
{
    switch((int)data)
    {
        case LINE_TIMER_DATA:
            add_info.short_long_line_id = fence_info.inflexion_info[fence_info.nowpoint].lineid;
            add_info.short_long_drive_time = linetimer->timeout/1000;
            add_info.short_long_result = 1;
            change_alarm_flag_and_upload(ALARM_STATUS_ROAD_DRIVE_SHORT_LONG, 1); 
            break;
        case PARK_TIMER_DATA:
            send_device_alarm_notify(DEV_ALARM_PARKING_OVERTIIME,1);
            change_alarm_flag_and_upload(ALARM_STATUS_PARKING_OVERTIIME, 1);            
            break;
        case SPEED_TIMER_DATA:
            if(fence_info.instatus)
            {
                switch(fence_info.etype)
                {
                    case 0:
                    case 1:
                    case 2:
                        add_info.overspeed_alarm_flag = fence_info.etype+1;
                        add_info.overspeed_region_id = fence_info.regionid;
                        break;
                    case 3:
                        add_info.overspeed_alarm_flag = 4;
                        add_info.overspeed_region_id =fence_info.inflexion_info[fence_info.nowpoint].lineid;
                        printf("line overspeed alarm \n");
                        break;
                }
            }
            else
            {
                add_info.overspeed_alarm_flag = 0;
                printf("default overspeed alarm \n");
            }
            
            change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 1);  
            if(vdr_current_info.over_speed_warn_flag)
            {
                vdr_current_info.over_speed_warn_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_WARNING, 0);
            }
            break;
        case SLEEP_TIMER_DATA:
            AllTiredDriving_Write(1);
            vdr_current_info.sleep_timer_flag = 0;
            change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
            change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
            flag_prefatigue = 0;
            break;
        default:
            break;
    }

}
void car_start_stop_run_handler(string &name, void *data, void *data2)
{
	int itime;
    int dif = 0;
    DeviceSpeedAbnormalDbusNotifyC notify;
    switch((int)data)
    {
        case START_TIMER_DATA:
		//车辆行驶10s以后进行的操作
            if(!(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF))
              break;
            tmp_status_flag = 1;
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CAR_WORK_OFF);
            send_device_runstop_notify(0, pretired_param_temp.StoppingTime);
            change_alarm_flag_and_upload(ALARM_STATUS_PARKING_OVERTIIME, 0);
            if(vdr_current_info.park_timer_flag)
            {
                StopTimer(parktimer);
                vdr_current_info.park_timer_flag = 0; 
            }

            vdr_current_info.stop_flag=0; 
            vdr_current_info.running_flag=1; 
            break;
        case STOP_TIMER_DATA:
		//车辆静止10s以后进行的操作
            if(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF)
                break;
            tmp_status_flag = 1;
            VDRBITSET(vdr_status_flag, LOCSTATUS_CAR_WORK_OFF);
            vdr_current_info.stop_flag=1; 
            vdr_current_info.running_flag=0; 
            send_device_runstop_notify(1,pretired_param_temp.StoppingTime);
            
            itime = time(NULL);
            dif = itime - startup_time_flage;
            if(dif>=30)                        // read data from runstatus db
                write_accident_flag = 1;       
            else if(dif<=10)                   //read data from buf
            {
                write_track_accident_from_buf(2, dif, loc_info);
            }
            else
                write_accident_flag = 5;      // read data from runstatus db and  buf

            write_vdr_day_info_table(itime);

            if((!(vdr_param.warning_mask & ALARM_STATUS_PARKING_OVERTIIME))&&(!(vdr_param.max_parking_time == 0)))
            {
                parktimer->timeout = (vdr_param.max_parking_time-10)*1000;
                StartTimer(parktimer);
                vdr_current_info.park_timer_flag = 1;
            }
            break;
        case STATUS_TIMER_DATA:
            vdr_current_info.status_timer_flag = 0;
            speed_info.day_speed_status = 1;
            if(speed_info.result == SPEED_STATUS_ERROR)
            {
                notify.flag = 1;
            }
            else
                notify.flag = 0;
            dev_DbusServer->sendNotify(&notify);
            write_track_speed_info_table();
            break;
        case VSSALARM_TIMER_DATA:
            change_alarm_flag_and_upload(ALARM_STATUS_CAR_VSS_FAILURE, 1);
            break;
        case IGNITION_TIMER_DATA:
            change_alarm_flag_and_upload(ALARM_STATUS_CAR_ILLEGAL_IGNITION, 1);
            break;
    }

}

void read_d310_dreg_info_table()
{
    VDRDregInfoTable *table = new VDRDregInfoTable();
    table->GetIntValue(VDR_DREG_ACTIVE_DISABLE, dreg_info.active_disable);
    table->GetIntValue(VDR_DREG_ENGINE_STATUS, dreg_info.engine_status);
    table->GetIntValue(VDR_DREG_LIFT_STATUS, dreg_info.lift_status);
    table->GetIntValue(VDR_DREG_SPEED_LIMIT_STATUS, dreg_info.speed_status);
    table->GetIntValue(VDR_DREG_SPEED_LIMIT_VALUE, dreg_info.speed_value);
    table->GetIntValue(VDR_DREG_SPEED_LIMIT_DURATION, dreg_info.speed_dura);    
    delete table;

    dreg_info.engine_status = 0;
    memset(&sensor_info,0,sizeof(struct DregSensorInfo));
}
void write_d310_dreg_info_table()
{
    VDRDregInfoTable *table = new VDRDregInfoTable();
    table->SetIntValue(VDR_DREG_ACTIVE_DISABLE, dreg_info.active_disable);
    table->SetIntValue(VDR_DREG_ENGINE_STATUS, dreg_info.engine_status);
    table->SetIntValue(VDR_DREG_LIFT_STATUS, dreg_info.lift_status);
    table->SetIntValue(VDR_DREG_SPEED_LIMIT_STATUS, dreg_info.speed_status);
    table->SetIntValue(VDR_DREG_SPEED_LIMIT_VALUE, dreg_info.speed_value);
    table->SetIntValue(VDR_DREG_SPEED_LIMIT_DURATION, dreg_info.speed_dura); 
    table->Commit();
    delete table;
}

void read_base_config_table()
{
    vdr_current_info.gps_mcu_speed_flag = 0;

    VDRBaseConfigTable *table = new VDRBaseConfigTable();
    table->GetIntValue(VDR_BASE_CONFIG_D0, vdr_current_info.gps_mcu_speed_flag);
    table->GetIntValue(VDR_BASE_CONFIG_D2, vdr_current_info.carry_status);
    table->GetIntValue(VDR_BASE_CONFIG_D3, vdr_current_info.change_time_flag);
    table->GetIntValue(VDR_BASE_CONFIG_D5, vdr_current_info.left_right_flag);
    delete table;
    
    switch(vdr_current_info.carry_status)
    {
        case 0:
            //空车
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 1:
            //半载
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 2:
            //保留
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 3:
            //满载
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
    }    

    int fd = 0;
    fd = open("/application/upgrade.ini",O_RDONLY);
    if(fd == -1)
        vdr_current_info.upgrade_version_flag = 0;
    else
        vdr_current_info.upgrade_version_flag = 1;
}

void read_deploy_status_config_table()
{
    VDRDeployStatusConfigTable *table = new VDRDeployStatusConfigTable();
    table->GetIntValue(VDR_DEPLOY_STATUS_ENABLE, vdr_current_info.status_enable);
    table->GetIntValue(VDR_DEPLOY_STATUS_BITSET, vdr_current_info.status_level);
    delete table;
}

void read_deploy_para_table_set_pluse()
{
    int speed_factor, lap_factor;
    string version;
    unsigned char buf[5];

    VDRDeployParaTable *table = new VDRDeployParaTable();
    table->GetIntValue(VDR_DEPLOY_PARA_SPEED_PULSE_FACTOR, speed_factor);
    table->GetIntValue(VDR_DEPLOY_PARA_LAP_PULSE_FACTOR, lap_factor);
    table->GetStringValue(VDR_DEPLOY_PARA_HARDWARE_VERSION, version);
    I2c_read_mcu_code_version(buf, 3);
    buf[1] += 0x30;
    buf[2] += 0x30;
    printf("vdrsion:%s,%s\n",version.c_str(), buf);
    if(memcmp(buf,version.c_str(),3) !=0)
    {
        table->SetStringValue(VDR_DEPLOY_PARA_HARDWARE_VERSION, (char *)buf); 
        table->Commit();
    }    
    delete table;
    vdr_current_info.speed_factor = speed_factor;
    vdr_current_info.lap_factor = lap_factor;

    I2c_write_speed_pluse_factor_data(speed_factor);
    I2c_write_lap_pluse_factor_data(lap_factor);
}

void write_track_speed_info_table()
{
    vector<unsigned char> vdr_speed, gps_speed;

    VDRSpeedInfoTable *table = new VDRSpeedInfoTable();
    table->SetIntValue(VDR_SPEED_BEGIN_TIME, speed_info.beginsec);
    table->SetIntValue(VDR_SPEED_END_TIME, speed_info.beginsec+300);
    table->SetIntValue(VDR_SPEED_STATUS, speed_info.result);
    for(int i=0; i<60; i++)
    {
        vdr_speed.push_back(speed_info.vdr_speed[i]);
        gps_speed.push_back(speed_info.gps_speed[i]);
    }
    table->SetSequenceValue(VDR_SPEED_VDR_SPEED, vdr_speed);
    table->SetSequenceValue(VDR_SPEED_GPS_SPEED, gps_speed);
    table->Commit();
    delete table;
}
void read_track_speed_info_table()
{
    int ret = 0;
    int sec = 0;
    
    VDRSpeedInfoTable *table = new VDRSpeedInfoTable();
    ret = table->RefreshBottom();
    if(ret < 0)
    {
        speed_info.day_speed_status = 0;
    }
    else
    {
        table->GetIntValue(VDR_SPEED_BEGIN_TIME, sec);
        if(vdr_current_info.day_time < sec)
        {
            speed_info.day_speed_status = 1;
        }
        else
            speed_info.day_speed_status = 0;
        
    }
	delete table;
}

void write_track_run_status_table(unsigned char *buf, int len, int itime)
{    
    int offset = 0;
    
    if(total_run != 50)
    {
        total_run++;
        current_run++;
    }
    else
    {
        if(current_run == 50)
            current_run = 0;

        current_run++;
    }
    
    fseek(fp_run, 0, SEEK_SET);
    fwrite(&total_run, 4, 1, fp_run);
    fwrite(&current_run, 4, 1, fp_run);

    offset = 10 + (current_run-1)*14;    
    fseek(fp_run, offset, SEEK_SET);
    fwrite(&itime, sizeof(int), 1, fp_run); 
    fwrite(buf, 10, 1, fp_run);
    fflush(fp_run);
    
    if(gps_position_count == 10)    //run status  & gps position stay the same
    {
        write_track_accident_table(4, norm_info);
    }

    if(write_accident_flag != 0)   // stop status  
    {
       write_track_accident_table(write_accident_flag, loc_info);
       write_accident_flag = 0;
    }
}

void write_track_accident_from_buf(int type, int dif, struct NaviGPSInfo tmpinfo)
{
    int i=0, j=0, t=0;
    unsigned char ispeed[256],istatus[256];
    int ret= 0, count = 0;

    memset(ispeed, 0, sizeof(ispeed));
    memset(istatus, 0, sizeof(istatus));
    i = startup_data_num -1-(10-dif);;
    printf("type=%d, dif=%d...i=%d...\n",type,dif,i);
    while(1)
    {        
        for(j=0, t=9; j<5; j++)
        {
            ispeed[count*5 + j] = startup_buf[i*10+t];
            t--;
            istatus[count*5 + j] = startup_buf[i*10+t];
            t--;
        }  
        count++;
        i--;
        if(i<0 || count >22)
            break;
    }
    
	AccidentWriteTable* accident = new AccidentWriteTable();
    j=0;
    for(i=0; i<count*5; i++)
    {
        if(ispeed[i] == 0)
            continue;
        if(ispeed[i] !=0 )
        {
            j = i-1;
            i= i/5;
            accident->accident_data.itime = startup_time_flage-(10-dif)-i;
            break;
        }  
    }
            
    printf("-------i=%d, count=%d,time=%d---------\n",i,count,accident->accident_data.itime);
    for(; j<count*5; j++)
    {
        accident->accident_data.speed.push_back(ispeed[j]);
        accident->accident_data.status.push_back(istatus[j]);
    }
    memcpy(accident->accident_data.driverid,pretired_param.VehicleDriverID,18);
    if(tmpinfo.position == 0)
    {
        accident->accident_data.longitude = 0x7FFFFFFF;
        accident->accident_data.latitude = 0x7FFFFFFF;
        accident->accident_data.ialtitude = 0x7FFF;
    }
    else
    {
        accident->accident_data.longitude = get_signed_longitude_latitude(tmpinfo.ilongitude,tmpinfo.fX);
        accident->accident_data.latitude = get_signed_longitude_latitude(tmpinfo.ilatitude,tmpinfo.fY);
        accident->accident_data.ialtitude = tmpinfo.ialtitude;
    }

	ret = workq_add (&workq_accident, (void*)accident);
	if (ret != 0)
		err_abort (ret, "Add accident to work queue");    
}

/*
   type = 1  speed!=0 -> speed=0 10s 
   type = 2  speed!=0 accoff, dbus off
   type = 3  illegality power off 
   type = 4  gps position stay the same
   type = 5  starting up speed!=0->speed=0 10s
*/
void write_track_accident_table(int type, struct NaviGPSInfo tmpinfo)
{
    int i=0, j=0, t=0,index2=0;
    int seconds[50];
    unsigned char buf[10];
    unsigned char ispeed[256],istatus[256];
    int ret=0, count = 0;
    
    memset(ispeed, 0, sizeof(ispeed));
    memset(istatus, 0, sizeof(istatus));
    memset(seconds, 0, sizeof(seconds));
    
    int current= current_run;
    int offset = 0, total=0;

    if(type == 1)
        total = 35;
    else 
        total = 25;

    for(i = 0; i<total; i++)
    {
        memset(buf, 0, 10);
        offset = 10+(current-1)*14;
        fseek(fp_run, offset, SEEK_SET);
        fread(&seconds[i], sizeof(int), 1, fp_run);
        fread(buf, 10, 1, fp_run);

        if(type == 1 || type == 5)
        {
            if(seconds[i] == pretired_param_temp.StoppingTime)
                index2 = i;
        }
        else
        {
            if(seconds[i] == vdr_current_info.acc_off_time)
                index2 = i;
        }
        
        for(j=0, t=9; j<5; j++)
        {
            ispeed[count*5 + j] = buf[t];
            t--;
            istatus[count*5 + j] = buf[t];
            t--;
        }
        count++;
        
        current--;
        if(current == 0)
            current = total_run;
        
        if((count-index2) > 22)
            break;
    }


    if(type==5)
    {
        if(index2 < 0)
            index2 = count;
        i = startup_data_num -1;;
        while(1)
         {        
             for(j=0, t=9; j<5; j++)
             {
                 ispeed[count*5 + j] = startup_buf[i*10+t];
                 t--;
                 istatus[count*5 + j] = startup_buf[i*10+t];
                 t--;
             }  
             count++;
             i--;
             if(i<0 || (count-index2) > 22)
                 break;
         }
    }
	AccidentWriteTable* accident = new AccidentWriteTable();

    if(type == 1)
    {
        j = index2*5;
        for(i=j; i<count*5; i++)
        {
        #ifdef NORMAL_3C_ACCIDENT
            if(ispeed[i] == 0)
                continue;
            if(ispeed[i] !=0 )
            {
                j = i-1;
                i= i/5-index2;
                accident->accident_data.itime = pretired_param_temp.StoppingTime-i;
                break;
            }  
        #else
            if(ispeed[i] < 5)
                continue;
            if(ispeed[i] !=0 )
            {
                ispeed[i-1] = 4;
                ispeed[i-2] = 3;
                ispeed[i-3] = 2;
                ispeed[i-4] = 0;
                j = i-4;
                #if 0
                if((i-3)%5 >1)
                    i = (i-3)/5-index2 + 1;
                else
                    i = (i-3)/5-index2;
                #endif
                printf("j=%d,i=%d, time=%d\n",j,i,seconds[i/5]);
                //accident->accident_data.itime = pretired_param_temp.StoppingTime-i;
                accident->accident_data.itime = seconds[i/5]-1;
                break;
            }
        #endif
        }
        
    }
    else if(type == 3)
    {
        j = index2*5;        
        accident->accident_data.itime = vdr_current_info.acc_off_time;
    }
    else if(type == 4)
    {
        j = 5;
    #ifdef NORMAL_3C_ACCIDENT
        accident->accident_data.itime = seconds[1];
    #else
        accident->accident_data.itime = seconds[2];
    #endif
    }
    else if(type == 5)
    {
        j = index2*5;
        printf("---i=%d,j=%d,count=%d,itime=%d------\n",i,j,count,pretired_param_temp.StoppingTime);
        for(i=j; i<count*5; i++)
        {
            if(ispeed[i] == 0)
                continue;
            if(ispeed[i] !=0 )
            {
                j = i-1;
                i= i/5-index2;
                accident->accident_data.itime = pretired_param_temp.StoppingTime-i;
                break;
            }  
        }
    }
    else
    {
        if(index2 < 0)
        {
            j = 0;
            accident->accident_data.itime = seconds[0];
        }
        else
        {
            j = index2*5;
            accident->accident_data.itime = vdr_current_info.acc_off_time;
        }
    }
    
    for(; j<count*5; j++)
    {
        accident->accident_data.speed.push_back(ispeed[j]);
        accident->accident_data.status.push_back(istatus[j]);
    }
    memcpy(accident->accident_data.driverid,pretired_param.VehicleDriverID,18);
    if(tmpinfo.position == 0)
    {
        accident->accident_data.longitude = 0x7FFFFFFF;
        accident->accident_data.latitude = 0x7FFFFFFF;
        accident->accident_data.ialtitude = 0x7FFF;
    }
    else
    {
        accident->accident_data.longitude = get_signed_longitude_latitude(tmpinfo.ilongitude,tmpinfo.fX);
        accident->accident_data.latitude = get_signed_longitude_latitude(tmpinfo.ilatitude,tmpinfo.fY);
        accident->accident_data.ialtitude = tmpinfo.ialtitude;
    }
#ifdef W_TABLE
	ret = workq_add (&workq_accident, (void*)accident);
	if (ret != 0)
		err_abort (ret, "Add accident to work queue");    
#endif
}
void write_track_locstatus_table(int type, int itime, struct NaviGPSInfo *locstatus)
{
    int status=0, speed=0;

    LocStatusWriteTable* table = new LocStatusWriteTable();
	table->locstatus_data.type = type;
    //if(first_set_time_flag)
     //   gui_data.itime = table->locstatus_data.itime = locstatus->iTime;
    //else
        gui_data.itime = table->locstatus_data.itime = itime;    
    table->locstatus_data.upload_flag = 0;
    if(tmp_alarm_flag && (type == 1))
    {
        table->locstatus_data.up_alarm_flag = 1;
        tmp_alarm_flag = 0;
    }
    else
        table->locstatus_data.up_alarm_flag = 0;
    
    vdr_alarm_flag = (~vdr_param.warning_mask) &vdr_alarm_flag;
    gui_data.alarm_signal = table->locstatus_data.alarm_signal = vdr_alarm_flag;
    gui_data.status_flag = table->locstatus_data.status_falg = vdr_status_flag;
    gui_data.latitude = table->locstatus_data.latitude = locstatus->fY;
    gui_data.longitude = table->locstatus_data.longitude = locstatus->fX;
    gui_data.altitude = table->locstatus_data.altitude = locstatus->ialtitude;
    gui_data.orientation = table->locstatus_data.orientation = locstatus->iOrientation;
    gui_data.gps_speed = table->locstatus_data.gps_speed = locstatus->iVelocity*10;
    table->locstatus_data.total_mileage = vdr_current_info.total_mileage/100; //0.1km
    gui_data.total_mileage = vdr_current_info.total_mileage /1000; //km
    gui_data.oil_mass = table->locstatus_data.oil_mass = 0;
    if(type == 1)
    {
        if(min_info.m_flag)
            table->locstatus_data.m_flag = 1;
        else
            table->locstatus_data.m_flag = 0;

        if(onesec_upload_flag == 1)
            table->locstatus_data.upload_flag = 1;

        int tmp_i_status = speed_distance.signal;
        int tmp_newhb_status = 0;
        tmp_newhb_status |= ((tmp_i_status & 0x01) << 5);// d0 -----> d5
        tmp_newhb_status |= (tmp_i_status & 0x06);       // d1 d2 not change
        tmp_newhb_status |= ((tmp_i_status & 0x08) >> 3);// d3 -----> d0
        tmp_newhb_status |= ((tmp_i_status & 0x10) >> 3);// d4 -----> d1
        tmp_newhb_status |= ((tmp_i_status & 0x20) >> 3);// d5 -----> d2
        tmp_newhb_status |= ((tmp_i_status & 0x40) >> 3);// d6 -----> d3
        tmp_newhb_status |= ((tmp_i_status & 0x80) >> 3);// d7 -----> d4
        tmp_newhb_status |= (tmp_i_status & 0x7F00);
        table->locstatus_data.vdr_status_new_hb = tmp_newhb_status;
        
        //国标读loc08.vdr，非此处，扩d310自定义
        tmp_newhb_status |= (speed_distance.ext_signal & 0xFF) << 16;
        gui_data.vdr_status_new_hb = table->locstatus_data.vdr_status_old_hb = tmp_newhb_status; 
        
        if(vdr_current_info.gps_mcu_speed_flag == 1)
            gui_data.vdr_speed = table->locstatus_data.vdr_speed = vdr_current_info.vdr_sec_speed*10;
        else
            gui_data.vdr_speed = table->locstatus_data.vdr_speed = speed_distance.sec_speed[speed_distance.index]*10;
        table->locstatus_data.vdr_distance = speed_distance.sec_distance[speed_distance.index];
    }
    else
    {
        if(type == 3)
            speed = min_info.m_speed*10/60;  //min_info.m_speed*10/min_info.m_count;
        else
            speed = min_info.t_speed;
        if(speed != 0)
        {
            vdr_current_info.day_drive_dura++;
        }
        table->locstatus_data.m_flag = 0;
        table->locstatus_data.vdr_status_old_hb = min_info.m_status & 0xFF;
        table->locstatus_data.vdr_status_new_hb = min_info.m_status;
        table->locstatus_data.vdr_speed = speed;
        table->locstatus_data.vdr_distance = 0;
    }
    
    gui_data.io_status = table->locstatus_data.io_status = add_info.io_status;
    gui_data.analog_quantity = table->locstatus_data.analog_quantity = add_info.analog_quantity;
    gui_data.wireless_intensity = table->locstatus_data.wireless_intensity = add_info.wireless_intensity;
    gui_data.gnss_satellite_sum = table->locstatus_data.gnss_satellite_sum = add_info.gnss_satellite_sum;
    gui_data.gnss_mode = locstatus->mode;
    table->locstatus_data.overspeed_flag = add_info.overspeed_alarm_flag;
    table->locstatus_data.overspeed_id = add_info.overspeed_region_id;
    table->locstatus_data.fence_inout_alarm = add_info.inout_alarm_flag;
    table->locstatus_data.fence_inout_id = add_info.inout_alarm_region_id;
    table->locstatus_data.fence_inout_direction = add_info.inout_alarm_direction;
    table->locstatus_data.fence_line_id= add_info.short_long_line_id;
    table->locstatus_data.fence_line_time = add_info.short_long_drive_time;
    table->locstatus_data.fence_line_result = add_info.short_long_result;
    table->locstatus_data.emergency_num = vdr_current_info.emergency_alarm_num;

    if(type == 1)
    {
        pthread_mutex_lock(&udp_mutex);
        memcpy(&udp_loc, &table->locstatus_data, sizeof(struct LocStatusData));
        pthread_mutex_unlock(&udp_mutex);

        if(num_loc == 5)
            num_loc = 0;
        memcpy(&loc_buf[num_loc], &table->locstatus_data, sizeof(struct LocStatusData));
        num_loc++;
        if(num_loc == 5)
        {
            printf("------write loc0809.vdr file-----\n");
            fseek(fp_loc, 0, SEEK_SET);
            fwrite(loc_buf, sizeof(struct LocStatusData), 5, fp_loc); 
            fflush(fp_loc);
            num_loc = 0;
        }
        
        sendto(socket_dp, &gui_data, sizeof(struct BaseData), 0, (struct sockaddr *)&addr, sizeof(addr));

        if(tmp_status_flag == 1)
        {
            table->locstatus_data.upload_flag = 1;
            tmp_status_flag = 0;
        }
        else if(tmp_status_flag != 0)
        {
            udp_loc.type = tmp_status_flag;
            table->locstatus_data.upload_flag = 1;
            tmp_status_flag = 0;
        }

    }

    if(vdr_current_info.is_distance_report)
    {
        //printf("--tmpmileage=%f, report_distance=%d----\n",tempmileage,vdr_current_info.report_distance);
        if((int)tempmileage >= vdr_current_info.report_distance)
        {
            table->locstatus_data.upload_flag = 1;
            tempmileage = tempmileage-vdr_current_info.report_distance;
            if((int)tempmileage >= vdr_current_info.report_distance)
                tempmileage = 0;                
        }
    }

    if(table->locstatus_data.up_alarm_flag == 1 || table->locstatus_data.upload_flag == 1)
    {
        if(startup_mode != 0x40 && startup_mode != 0x80 && startup_mode != 0xC0)
            send_position_to_JT808_udp(1);
    }
    
    if(type == 2)
    {
        status = workq_add (&workq_locst, (void*)table);
        if (status != 0)
            err_abort (status, "Add runstatus to work queue"); 
    }   
}

void read_electronic_fence_table(int type, int regionid)
{ 
    fence_info.instatus = 1;
    fence_info.nowpoint = 0;
    fence_info.temptime = 0;
    
    fence_info.etype = type;
    fence_info.regionid = regionid;
    
    if(type == 0)
    {
        VDRCircleFenceTable *circletable = new VDRCircleFenceTable();
        circletable->Refresh(fence_info.regionid);
        circletable->GetIntValue(VDR_CIRCLE_REGION_ATTRIBUTE,fence_info.attribute);
        circletable->GetIntValue(VDR_CIRCLE_MAX_SPEED, vdr_current_info.max_speed);
        circletable->GetIntValue(VDR_CIRCLE_OVER_SPEED_DURA,vdr_current_info.over_speed_dura);
        delete circletable;
    }
    else if(type == 1)
    {
        VDRRectangleFenceTable *recttable = new VDRRectangleFenceTable();
        recttable->Refresh(fence_info.regionid);
        recttable->GetIntValue(VDR_RECT_REGION_ATTRIBUTE,fence_info.attribute);
        recttable->GetIntValue(VDR_RECT_MAX_SPEED,vdr_current_info.max_speed);
        recttable->GetIntValue(VDR_RECT_OVER_SPEED_DURA,vdr_current_info.over_speed_dura);
        delete recttable;
    }
    else if(type == 2)
    {
        VDRPolygonFenceTable *polytable = new VDRPolygonFenceTable();
        polytable->Refresh(fence_info.regionid);
        polytable->GetIntValue(VDR_POL_REGION_ATTRIBUTE,fence_info.attribute);
        polytable->GetIntValue(VDR_POL_MAX_SPEED,vdr_current_info.max_speed);
        polytable->GetIntValue(VDR_POL_OVER_SPEED_DURA,vdr_current_info.over_speed_dura);
        delete polytable;
    }
    else if(type == 3)
    {
        SQLiteSequence lineseq;
        int num=0;
        int temp=0;
        int index = 0;
        int attribute = 0;
        
        VDRLineFenceTable *linetable = new VDRLineFenceTable();
        linetable->Refresh(fence_info.regionid);
        linetable->GetIntValue(VDR_LINE_REGION_ATTRIBUTE,fence_info.attribute);
        linetable->GetIntValue(VDR_LINE_INFLEXION_COUNT,num);
        linetable->GetSequenceValue(VDR_LINE_INFLEXION_INFO,lineseq);
        delete linetable;
        
        for(int i = 0; i < num; i ++)
        {
            if((lineseq.size() - index) < 18)
            {
                printf("getLine lineseq invalid(%d, %d)", index, lineseq.size());
                return ;
            }
            temp = (lineseq[index]<<24)|(lineseq[index+1]<<16)|(lineseq[index+2]<<8)|lineseq[index+3];
            fence_info.inflexion_info[i].id = temp;
            temp = (lineseq[index+4]<<24)|(lineseq[index+5]<<16)|(lineseq[index+6]<<8)|lineseq[index+7];
            fence_info.inflexion_info[i].lineid= temp;
            attribute = lineseq[index+17];
            fence_info.inflexion_info[i].lineattribute = attribute;
            index += 18;
            if(attribute & 0x01)
            {
                fence_info.inflexion_info[i].maxtime = (lineseq[index]<<8)|lineseq[index+1];
                fence_info.inflexion_info[i].mintime = (lineseq[index+2]<<8)|lineseq[index+3];
                index += 4;
            }
            else
            {
                fence_info.inflexion_info[i].maxtime = 0;
                fence_info.inflexion_info[i].mintime = 0;
            }
            if(attribute & 0x02)
            {
                fence_info.inflexion_info[i].maxspeed = (lineseq[index]<<8)|lineseq[index+1];
                fence_info.inflexion_info[i].overspeeddura = lineseq[index+3];
                index += 3;
            }
            else
            {
                fence_info.inflexion_info[i].maxtime = 0;
                fence_info.inflexion_info[i].mintime = 0;
            }
        }
    }

}

void write_acceleration_info_table()
{
        VDRAccelerationTable *table = new VDRAccelerationTable();
        table->SetIntValue(VDR_ACCELERATION_COLLIDE_TIME, vdr_param.collision_time);//单位4ms
        table->SetIntValue(VDR_ACCELERATION_SIDE_TURN_ANGLE, vdr_param.rollover_angle);//侧翻角度
        table->SetIntValue(VDR_ACCELERATION_COLLIDE_MIN, vdr_param.collision_acceleration);//碰撞加速度
        table->Commit();
        delete table;
}

void read_vdr_run_param_table()
{
    VDRRunParaTable *vdrrunpara = new VDRRunParaTable();
    vdrrunpara->GetIntValue(VDR_PARA_LOC_REPORT_POLICY, vdr_param.report_policy);
    vdrrunpara->GetIntValue(VDR_PARA_LOC_REPORT_SCENARIO, vdr_param.report_scenario);
    vdrrunpara->GetIntValue(VDR_PARA_NOLOGIN_REPORT_INTERVAL, vdr_param.nologin_report_interval);
    vdrrunpara->GetIntValue(VDR_PARA_SLEEP_REPORT_INTERVAL, vdr_param.sleep_report_interval);
    vdrrunpara->GetIntValue(VDR_PARA_EMERGENCY_REPORT_INTERVAL, vdr_param.alarm_report_interval);
    vdrrunpara->GetIntValue(VDR_PARA_DEFAULT_REPORT_INTERVAL, vdr_param.default_report_interval);
    vdrrunpara->GetIntValue(VDR_PARA_DEFAULT_REPORT_DISTANCE, vdr_param.default_report_distance);
    vdrrunpara->GetIntValue(VDR_PARA_NOLOGIN_REPORT_DISTANCE, vdr_param.nologin_report_distance);
    vdrrunpara->GetIntValue(VDR_PARA_SLEEP_REPORT_DISTANCE, vdr_param.sleep_report_distance);
    vdrrunpara->GetIntValue(VDR_PARA_EMERGENCY_REPORT_DISTANCE, vdr_param.alarm_report_distance);
    vdrrunpara->GetIntValue(VDR_PARA_FENCE_RADIUS,vdr_param.fence_radius);
    vdrrunpara->GetIntValue(VDR_PARA_WARNING_MASK, vdr_param.warning_mask); 
    vdrrunpara->GetIntValue(VDR_PARA_WARNING_SMS_MASK, vdr_param.warning_SMS_mask);
    vdrrunpara->GetIntValue(VDR_PARA_WARNING_PHOTO_MASK, vdr_param.warning_photo_mask); 
    vdrrunpara->GetIntValue(VDR_PARA_WARNING_PHOTOSAVE_MASK, vdr_param.warning_photosave_mask); 
    vdrrunpara->GetIntValue(VDR_PARA_WARNING_KEY_MASK, vdr_param.warning_key_mask);
    vdr_param.warning_mask = vdr_param.warning_mask & (~vdr_param.warning_key_mask);
    vdr_param.warning_mask = vdr_param.warning_mask & (~Hw0); //紧急报警不能被屏蔽
    
    vdrrunpara->GetIntValue(VDR_PARA_MAX_SPEED, vdr_param.max_speed);    
    vdrrunpara->GetIntValue(VDR_PARA_OVERSPEED_DURATION, vdr_param.over_speed_duration);
    vdrrunpara->GetIntValue(VDR_PARA_CONT_DRIVE_THRESHOLD, vdr_param.continue_drive_threshold);
    vdrrunpara->GetIntValue(VDR_PARA_DAY_DRIVE_THRESHOLD, vdr_param.day_drive_time_threshold);
    vdrrunpara->GetIntValue(VDR_PARA_REST_MIN, vdr_param.min_resting_time);
    vdrrunpara->GetIntValue(VDR_PARA_PARKING_MAX, vdr_param.max_parking_time);
    
    vdrrunpara->GetStringValue(VDR_PARA_IC_MAIN_SERVER_IP_ADDR, vdr_param.ic_main_serv_ip_addr);
    vdrrunpara->GetIntValue(VDR_PARA_IC_MAIN_SERVER_TCP_PORT, vdr_param.ic_main_serv_tcp_port);
    vdrrunpara->GetIntValue(VDR_PARA_IC_MAIN_SERVER_UDP_PORT, vdr_param.ic_main_serv_udp_port);
    vdrrunpara->GetStringValue(VDR_PARA_IC_SECOND_SERVER_IP_ADDR, vdr_param.ic_second_serv_ip_addr);
    vdrrunpara->GetIntValue(VDR_PARA_OVERSPEED_WARNING, vdr_param.overspeed_warning);
    vdrrunpara->GetIntValue(VDR_PARA_TIRED_DRIVE_WARNING, vdr_param.tired_drive_warning);
    
    int collision_alarm_param;
    int time_capture_param;
    int distance_capture_param;
    vdrrunpara->GetIntValue(VDR_PARA_COLLISION_ALARM, collision_alarm_param);
    vdr_param.collision_time = collision_alarm_param & 0xFF;//单位4ms
    vdr_param.collision_acceleration = (collision_alarm_param >> 8) & 0xFF;//单位0.1g
    
    vdrrunpara->GetIntValue(VDR_PARA_ROLLOVER_ALARM, vdr_param.rollover_angle);
    
    vdrrunpara->GetIntValue(VDR_PARA_TIME_CAPTURE_SET, time_capture_param);
    vdr_param.capture_control.time_capture_switch = time_capture_param & 0xFF;
    vdr_param.capture_control.time_capture_store = (time_capture_param >> 8) & 0xFF;
    if((time_capture_param >> 16) & 0x01) //minutes
    {
        vdr_param.capture_control.time_interval = ((time_capture_param >> 17) & 0x7FFF)*60;
    }
    else  if((time_capture_param >> 17) >= 5)
    {
        vdr_param.capture_control.time_interval = (time_capture_param >> 17) & 0x7FFF;
    }
    else
    {
        vdr_param.capture_control.time_interval = 5;
    }
    
    if(vdr_param.capture_control.time_capture_switch != 0)
    {
        picturetimer->timeout = vdr_param.capture_control.time_interval*1000;
        StartTimer(picturetimer);
    }
    
    vdrrunpara->GetIntValue(VDR_PARA_DISTANCE_CAPTURE_SET, distance_capture_param);
    vdr_param.capture_control.distance_capture_switch = distance_capture_param & 0xFF;
    vdr_param.capture_control.distance_capture_store = (distance_capture_param >> 8) & 0xFF;
    if((distance_capture_param >> 16) & 0x01) //km
    {
        vdr_param.capture_control.distance_interval = ((distance_capture_param >> 17) & 0x7FFF)*1000;
    }
    else  if((time_capture_param >> 17) >= 100)
    {
        vdr_param.capture_control.distance_interval = (distance_capture_param >> 17) & 0x7FFF;
    }
    else
    {
        vdr_param.capture_control.distance_interval = 100;
    }
    
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_LOC_MODE, vdr_param.gnss_locate_mode);
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_BAUD_RATE, vdr_param.gnss_baud_rate);
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_DATA_OUT_RATE, vdr_param.gnss_data_out_rate);
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_DATA_COLLECT_RATE, vdr_param.gnss_data_collect_rate);
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_DATA_UPLOAD_MODE, vdr_param.gnss_data_upload_mode);
    vdrrunpara->GetIntValue(VDR_PARA_GNSS_DATA_UPLOAD_SET, vdr_param.gnss_data_upload_set);
    vdrrunpara->GetIntValue(VDR_PARA_CAN_1_COLLECT_INTERVAL, vdr_param.can_1_collect_interv);
    vdrrunpara->GetIntValue(VDR_PARA_CAN_1_UPLOAD_INTERVAL, vdr_param.can_1_upload_interv);
    vdrrunpara->GetIntValue(VDR_PARA_CAN_2_COLLECT_INTERVAL, vdr_param.can_2_collect_interv);
    vdrrunpara->GetIntValue(VDR_PARA_CAN_2_UPLOAD_INTERVAL, vdr_param.can_2_upload_interv);
    vdrrunpara->GetSequenceValue(VDR_PARA_CAN_ID_COLLECT_SET, vdr_param.can_id_collect);
    vdrrunpara->GetSequenceValue(VDR_PARA_OTHER_CAN_ID_COLLECT_SET, vdr_param.other_can_id_collect);
    delete vdrrunpara;

    //write_acceleration_info_table();

    int sideturn_angle[2], collision_min = 128, tmp_collision_min;
    int collision_time;
    
    VDRAccelerationTable *table = new VDRAccelerationTable();
    table->GetIntValue(VDR_ACCELERATION_OFFSET_ANGLE, sideturn_angle[1]);
    table->GetIntValue(VDR_ACCELERATION_SIDE_TURN_ANGLE, sideturn_angle[0]);
    table->GetIntValue(VDR_ACCELERATION_COLLIDE_MIN, collision_min);//单位0.1g
    table->GetIntValue(VDR_ACCELERATION_COLLIDE_TIME, collision_time);
    delete table;

    if(collision_min%10 < 5)
        tmp_collision_min = collision_min /10;
    else
        tmp_collision_min = collision_min /10 + 1;
    if(cst_fd > 0)
    {
        ioctl(cst_fd, 0x01, &collision_time);  //set 
        ioctl(cst_fd, 0x06, &tmp_collision_min);  //set 
        ioctl(cst_fd, 0x07, sideturn_angle);  //set 
    }
        
    tempmileage = 0;
    picturedistance = 0;
    gnss_mode = vdr_param.gnss_locate_mode;
    vdr_current_info.report_time = 0;
    //vdr_current_info.report_distance = 0;
    vdr_current_info.is_time_report = 0;
    vdr_current_info.is_distance_report = 0;
    vdr_current_info.temp_report_validity = 0;
    vdr_current_info.illegal_displacement = 0;
    vdr_current_info.max_speed = vdr_param.max_speed;
    vdr_current_info.over_speed_dura = vdr_param.over_speed_duration;
    add_info.wireless_intensity = 15;
    add_info.io_status = 0;

    switch(vdr_param.report_policy)
    {
        case PRP_TIMEING:
            vdr_current_info.is_time_report = 1;
            break;
        case PRP_TIMEING_DISTANCE:
            vdr_current_info.is_time_report = 1;
            vdr_current_info.is_distance_report = 1;
            break;
        case PRP_DISTANCE:
            vdr_current_info.is_distance_report = 1;
            break;
    }
    #if 0
    if(vdr_alarm_flag & ALARM_STATUS_EMERGENCY_ALARM) 
        change_report_time_distance_mode(ALARM_MODE);
    else
        change_report_time_distance_mode(DEFAULT_MODE);
    #endif
    if(gnss_mode == 1)
     {
             VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
             VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
     }
     else if(gnss_mode == 2)
     {
             VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
             VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
     }    
     else if(gnss_mode == 3)
     {
             VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
             VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
     }
     else
     {
             VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
             VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
     }
}
void PreTired_Read(int i,int t)
{
	int tmp,diftime,dif_time;
	string driverid;

    VDRHyPreTiredTable *table = new VDRHyPreTiredTable();
	int ret = table->Refresh(pretired_param.VehicleDriverID);
	if(ret != -1)
	{
		table->GetIntValue(VDR_HY_PRETIRED_BEGIN_TIME, pretired_param.TiredTime);
		table->GetIntValue(VDR_HY_PRETIRED_BEGIN_LONGITUDE, pretired_param.TiredLongitude);
		table->GetIntValue(VDR_HY_PRETIRED_BEGIN_LATITUDE, pretired_param.TiredLatitude);
		table->GetIntValue(VDR_HY_PRETIRED_BEGIN_ALTITUDE, pretired_param.TiredAltitude);
		table->GetIntValue(VDR_HY_PRETIRED_END_TIME, pretired_param.StoppingTime);
		table->GetIntValue(VDR_HY_PRETIRED_END_LONGITUDE, pretired_param.StopLongitude);
		table->GetIntValue(VDR_HY_PRETIRED_END_LATITUDE, pretired_param.StopLatitude);
		table->GetIntValue(VDR_HY_PRETIRED_END_ALTITUDE, pretired_param.StopAltitude);
		table->GetIntValue(VDR_HY_PRETIRED_RUNNING_TIME, pretired_param.RunningTime);
		table->GetIntValue(VDR_HY_PRETIRED_DRIVING_TOTAL_TIME, pretired_param.TotalTime);
		table->GetIntValue(VDR_HY_PRETIRED_METER_TODAY, pretired_param.TripMeterCurDay);
		table->GetIntValue(VDR_HY_PRETIRED_RUN_TIME, pretired_param.RunningTimeCurDay);
		table->GetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, pretired_param.Flag_TiredDrive);
		delete table;
        printf("PreTired_Read::%s:%d,%d,%d,%d,%d\n",pretired_param.VehicleDriverID,pretired_param.RunningTime,
            pretired_param.TotalTime,pretired_param.TiredTime,pretired_param.StoppingTime,pretired_param.Flag_TiredDrive);
        
        if(pretired_param.RunningTime<0)
            pretired_param.RunningTime=0;
        
		diftime=time(NULL)-AllTiredDriving_Max_time();
		dif_time=time(NULL)-pretired_param.StoppingTime;
        printf("PreTired_Read::diftime=%d, dif_time=%d,%d\n",diftime,dif_time,vdr_current_info.day_time);

        if(vdr_current_info.day_time > pretired_param.StoppingTime)
        {
            pretired_param.TotalTime=0;
            flag_total_time=0;
            change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);
        }
        
        total_time_tmp = pretired_param.TotalTime;
        
        if(diftime>= vdr_param.min_resting_time)
		{
		//所有卡都休息解除疲劳
			if(0==i)
				AllTiredDriving_Write(1);
            
            total_time_start = -1;
			pretired_param.Flag_TiredDrive=0;
			pretired_param.RunningTime=0;
            flag_prefatigue = 0;
			once_alarm=0;
            change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
            change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
            if(pretired_param.TotalTime < vdr_param.day_drive_time_threshold)
            {
                flag_total_time=0;
                change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);
            }
            else
            {
                flag_total_time=1;
                change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,1);
            }
            //printf("-1---pretired tired day====0 0 0 %d==\n",total_time_tmp);
		}
		else
		{
            if(dif_time > vdr_param.min_resting_time)
            {
                total_time_start = -1;
                pretired_param.Flag_TiredDrive=0;
                pretired_param.RunningTime=0;
                flag_prefatigue = 0;
                once_alarm=0;

                change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
                change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
                if(pretired_param.TotalTime < vdr_param.day_drive_time_threshold)
                {
                    flag_total_time=0;
                    change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);
                }
                else
                {
                    flag_total_time=1;
                    change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,1);
                }
                //printf("-2---pretired tired day====0 0 0 %d==\n",total_time_tmp);
            }
            else
            {
                total_time_start = pretired_param.StoppingTime;
                if(pretired_param.Flag_TiredDrive==true)
                {
                    change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
                    change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 1);
                    flag_prefatigue = 0;
                }
                else
                {
                    change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
                    if(pretired_param.RunningTime>=(vdr_param.continue_drive_threshold-vdr_param.tired_drive_warning))
                    {
                        flag_prefatigue=1;
                        change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 1);
                    }
                    else
                    {
                        flag_prefatigue=0;
                        change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
                    }
                }
                if(pretired_param.TotalTime < vdr_param.day_drive_time_threshold)
                {
                    flag_total_time=0;
                    change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);
                }
                else
                {
                    flag_total_time=1;
                    change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,1);
                }
                
                if(vdr_current_info.sleep_timer_flag==1)
                {
                    StopTimer(sleeptimer);
                }
                sleeptimer->timeout = (vdr_param.min_resting_time-1-dif_time)*1000;
                StartTimer(sleeptimer);
                vdr_current_info.sleep_timer_flag = 1;
                
                if(((tmp=(pretired_param.RunningTime+1800-vdr_param.continue_drive_threshold))>0)&&(!(vdr_param.warning_mask &  ALARM_STATUS_FATIGUE_DRIVING))&&(vdr_param.min_resting_time!= 0)){
                    once_alarm=tmp/300;
                    if(once_alarm>=12)
                    {
                        tirednotify->flag=2;
                        tirednotify->dura=pretired_param.RunningTime;
                        memcpy(tirednotify->driverid,pretired_param.VehicleDriverID,18);
                        dev_DbusServer->sendNotify(tirednotify);
                    }
                }
                else 
                    once_alarm=0;  
                
            }
            //AllTiredDriving_Write(0);
            //printf("-22---pretired tired day==== %d==\n",total_time_tmp);
		}
	}
    else
	{
        printf("PreTired_Read::else\n");
        change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
        change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
        change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);
        pretired_param.Flag_TiredDrive=0;
        pretired_param.RunningTime=0;
        pretired_param.TotalTime=0;
        total_time_tmp = 0;
        total_time_start = -1;
        flag_total_time=0;
        flag_prefatigue = 0;
        once_alarm=0;
	}
}

void PreTired_Write()
{
	int status;
	HyPreTiredWriteTable* ptrPretiredWriteTable=new HyPreTiredWriteTable();
	memcpy(ptrPretiredWriteTable->pretired_param.VehicleDriverID,pretired_param.VehicleDriverID,18);
	ptrPretiredWriteTable->pretired_param.TiredTime=pretired_param.TiredTime;
	ptrPretiredWriteTable->pretired_param.TiredLongitude=pretired_param.TiredLongitude;
	ptrPretiredWriteTable->pretired_param.TiredLatitude=pretired_param.TiredLatitude;
	ptrPretiredWriteTable->pretired_param.TiredAltitude=pretired_param.TiredAltitude;
	ptrPretiredWriteTable->pretired_param.StoppingTime=pretired_param.StoppingTime;
	ptrPretiredWriteTable->pretired_param.StopLongitude=pretired_param.StopLongitude;
	ptrPretiredWriteTable->pretired_param.StopLatitude=pretired_param.StopLatitude;
	ptrPretiredWriteTable->pretired_param.StopAltitude=pretired_param.StopAltitude;
	ptrPretiredWriteTable->pretired_param.RunningTime=pretired_param.RunningTime;
	ptrPretiredWriteTable->pretired_param.TotalTime=pretired_param.TotalTime;
	ptrPretiredWriteTable->pretired_param.TripMeterCurDay=pretired_param.TripMeterCurDay;
	ptrPretiredWriteTable->pretired_param.RunningTimeCurDay=pretired_param.RunningTimeCurDay;
	ptrPretiredWriteTable->pretired_param.Flag_TiredDrive=pretired_param.Flag_TiredDrive;
	status = workq_add (&workq_pretired, (void*)ptrPretiredWriteTable);
	if (status != 0)
		err_abort (status, "Add to work queue");
}
void PreTired_Clear()
{
	VDRHyPreTiredTable *table = new VDRHyPreTiredTable();
	int ret = table->Refresh(pretired_param.VehicleDriverID);
	pretired_param.TotalTime=0;
	pretired_param.RunningTime=0;
	if(ret != -1)
	{
		table->GetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, pretired_param.Flag_TiredDrive);
		if(pretired_param.Flag_TiredDrive==1)
			table->SetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, 0);
		table->SetIntValue(VDR_HY_PRETIRED_RUNNING_TIME, 0);
		table->SetIntValue(VDR_HY_PRETIRED_DRIVING_TOTAL_TIME,0);
		table->Commit();
	}
	delete table;
}
void TiredDriving_Write(int a)
{
	int status;
	TiredWriteTable* ptrPretiredWriteTable=new TiredWriteTable();
	memcpy(ptrPretiredWriteTable->pretired_param.VehicleDriverID,pretired_param.VehicleDriverID,18);
	ptrPretiredWriteTable->pretired_param.TiredTime=pretired_param.TiredTime;
	ptrPretiredWriteTable->pretired_param.TiredLongitude=pretired_param.TiredLongitude;
	ptrPretiredWriteTable->pretired_param.TiredLatitude=pretired_param.TiredLatitude;
	ptrPretiredWriteTable->pretired_param.TiredAltitude=pretired_param.TiredAltitude;
	if(0==a)
		ptrPretiredWriteTable->pretired_param.StoppingTime=pretired_param.TiredTime+vdr_param.continue_drive_threshold;
	else
		ptrPretiredWriteTable->pretired_param.StoppingTime=pretired_param.StoppingTime;
	ptrPretiredWriteTable->pretired_param.StopLongitude=pretired_param.StopLongitude;
	ptrPretiredWriteTable->pretired_param.StopLatitude=pretired_param.StopLatitude;
	ptrPretiredWriteTable->pretired_param.StopAltitude=pretired_param.StopAltitude;
	status = workq_add (&workq_tired, (void*)ptrPretiredWriteTable);
	if (status != 0)
		err_abort (status, "Add to work queue");
}
int AllTiredDriving_Max_time()
{
	int maxtime=0;
	int temp=0;
	VDRHyPreTiredTable *table = new VDRHyPreTiredTable();
	int ret = table->RefreshTop();
	while(ret != -1)
	{
		table->GetIntValue(VDR_HY_PRETIRED_END_TIME, temp);
		if(temp>maxtime)maxtime=temp;
		ret = table->RefreshNext();
	}
	delete table;
	return maxtime;
}
void AllTiredDriving_Write(int type)
{
	string driverid;
    struct PRETIRED_DRIVING  pretired_param_temp2;

    memset(&pretired_param_temp2,0,sizeof(struct PRETIRED_DRIVING));

	VDRHyPreTiredTable *table = new VDRHyPreTiredTable();
	int ret = table->RefreshTop();
	while(ret != -1)
	{
		table->GetStringValue(VDR_HY_PRETIRED_DRIVER_LICENSE_NUM, driverid);
		table->GetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, pretired_param_temp2.Flag_TiredDrive);
		if(memcmp(pretired_param.VehicleDriverID, driverid.c_str(),18)==0)
		{
			if(pretired_param_temp2.Flag_TiredDrive==true)
            {
				TiredDriving_Write(1);
			}
            if(1==type)
            {
                pretired_param.RunningTime=0;	
                pretired_param.Flag_TiredDrive=false;
                table->SetIntValue(VDR_HY_PRETIRED_RUNNING_TIME, 0);
                table->SetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, 0);
                table->Commit();
            }
        
			if(tirednotify->flag==2)
            {
				tirednotify->flag=3;
				tirednotify->dura=0;
				memcpy(tirednotify->driverid,pretired_param.VehicleDriverID,18);
				dev_DbusServer->sendNotify(tirednotify);
				tirednotify->flag=0;
			}
		}
		else
		{
			if(pretired_param_temp2.Flag_TiredDrive==true)
            {
                table->GetIntValue(VDR_HY_PRETIRED_BEGIN_TIME, pretired_param_temp2.TiredTime);
                table->GetIntValue(VDR_HY_PRETIRED_BEGIN_LONGITUDE, pretired_param_temp2.TiredLongitude);
                table->GetIntValue(VDR_HY_PRETIRED_BEGIN_LATITUDE, pretired_param_temp2.TiredLatitude);
                table->GetIntValue(VDR_HY_PRETIRED_BEGIN_ALTITUDE, pretired_param_temp2.TiredAltitude);
                table->GetIntValue(VDR_HY_PRETIRED_END_TIME, pretired_param_temp2.StoppingTime);
                table->GetIntValue(VDR_HY_PRETIRED_END_LONGITUDE, pretired_param_temp2.StopLongitude);
                table->GetIntValue(VDR_HY_PRETIRED_END_LATITUDE, pretired_param_temp2.StopLatitude);
                table->GetIntValue(VDR_HY_PRETIRED_END_ALTITUDE, pretired_param_temp2.StopAltitude);

                VDRTiredTable *table2 = new VDRTiredTable();
                table2->SetIntValue(VDR_TIRED_BEGIN_TIME, pretired_param_temp2.TiredTime);
                table2->SetIntValue(VDR_TIRED_END_TIME, pretired_param_temp2.StoppingTime);
                table2->SetStringValue(VDR_TIRED_DRIVER_LICENSE_NUM, driverid);
                table2->SetIntValue(VDR_TIRED_BEGIN_LONGITUDE, pretired_param_temp2.TiredLongitude);
                table2->SetIntValue(VDR_TIRED_BEGIN_LATITUDE, pretired_param_temp2.TiredLatitude);
                table2->SetIntValue(VDR_TIRED_BEGIN_ALTITUDE, pretired_param_temp2.TiredAltitude);
                table2->SetIntValue(VDR_TIRED_END_LONGITUDE, pretired_param_temp2.StopLongitude);
                table2->SetIntValue(VDR_TIRED_END_LATITUDE, pretired_param_temp2.StopLatitude);
                table2->SetIntValue(VDR_TIRED_END_ALTITUDE, pretired_param_temp2.StopAltitude);
                table2->Commit();
                delete table2;
			}

            if(1==type){
				table->SetIntValue(VDR_HY_PRETIRED_RUNNING_TIME, 0);
				table->SetIntValue(VDR_HY_PRETIRED_FLAG_TIRED, 0);
				table->SetIntValue(VDR_HY_PRETIRED_METER_TODAY, 0);
				table->SetIntValue(VDR_HY_PRETIRED_RUN_TIME,0);
				table->Commit();
			}
		}
		ret = table->RefreshNext();
	}
	delete table;
}

void write_vdr_off_status_table(int type)
{
    int timeleft = 0;
    VDROFFStatusTable *table = new VDROFFStatusTable();
    table->SetIntValue(VDR_OFFS_ACC_ON_TIME, vdr_current_info.acc_on_time);
    if(type == 1)
    {
        table->SetIntValue(VDR_OFFS_ACC_OFF_TIME, 0);
        table->SetIntValue(VDR_OFFS_PARKED_DURA, 0);
    }
    else
    {
        table->SetIntValue(VDR_OFFS_ACC_OFF_TIME, vdr_current_info.acc_off_time);
        if(vdr_current_info.park_timer_flag)
        {
            timeleft = GetTimerLeft(parktimer);
            table->SetIntValue(VDR_OFFS_PARKED_DURA, (parktimer->timeout-timeleft)/1000);
        }
        else
            table->SetIntValue(VDR_OFFS_PARKED_DURA, 0);
    }
    table->SetIntValue(VDR_OFFS_TOTAL_MILEAGE, vdr_current_info.total_mileage); // m
    table->Commit();
    delete table;
}

void read_vdr_off_status_table()
{
    int parked_dura = 0;
    int tmp = 0;
    string driverid;
    time_t timep;
    struct tm p;
    
    DriversLoginTable *table1 = new DriversLoginTable();
    tmp = table1->RefreshBottom();
    if(tmp < 0)
    {
        delete table1;
        Flag_DriverLogin = false;   
        memset(pretired_param.VehicleDriverID,'0',18);
    }
    else
    {
        int i;
        table1->GetIntValue(VDR_DRV_LOGIN_LOGOUT, i);
        if(i==1){
            Flag_DriverLogin = true;
            table1->GetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, driverid);
            memcpy(pretired_param.VehicleDriverID,driverid.c_str(),18);
        }else  {
            Flag_DriverLogin = false;
            memset(pretired_param.VehicleDriverID,'0',18);
        }
        delete table1;
    }
    PreTired_Read(0,0);

    VDROFFStatusTable *table = new VDROFFStatusTable();
    tmp = table->RefreshBottom();
    if(tmp < 0)
    {
        vdr_current_info.acc_off_time = time(NULL);;
        vdr_current_info.total_mileage = 0;
        parked_dura = 0;
        delete table;
    }
    else
    {
        table->GetIntValue(VDR_OFFS_ACC_ON_TIME, vdr_current_info.acc_on_time);
        table->GetIntValue(VDR_OFFS_ACC_OFF_TIME, vdr_current_info.acc_off_time);
        table->GetIntValue(VDR_OFFS_PARKED_DURA, parked_dura);
        table->GetIntValue(VDR_OFFS_TOTAL_MILEAGE, vdr_current_info.total_mileage);
        delete table;
        if(vdr_current_info.acc_off_time == 0)
        {
            if(num_loc == 0)
            {
                vdr_current_info.acc_off_time = time(NULL);
                vdr_current_info.total_mileage = 0;
                parked_dura = 0;
            }
            else
            {
                vdr_current_info.acc_off_time = loc_buf[num_loc-1].itime;
                vdr_current_info.total_mileage = loc_buf[num_loc-1].total_mileage*100;
                loc_info.fY = loc_buf[num_loc-1].latitude;
                loc_info.fX = loc_buf[num_loc-1].longitude;
                loc_info.ialtitude = loc_buf[num_loc-1].latitude;
                
                if(loc_buf[num_loc-1].status_falg& Hw2)
                    loc_info.ilatitude = 1;
                else
                    loc_info.ilatitude = 0;
                
                if(loc_buf[num_loc-1].status_falg & Hw3)
                    loc_info.ilongitude = 1;
                else
                    loc_info.ilongitude = 0;
                
                if(!(loc_buf[num_loc-1].status_falg & Hw4))
                    write_track_accident_table(3,loc_info);
                write_vdr_off_status_table(0);
            }
        }
    }
    
    vdr_current_info.acc_on_time = time(&timep);
    vdr_current_info.speed_timer_flag = 0;
    vdr_current_info.sleep_timer_flag = 0;
    vdr_current_info.park_timer_flag = 0;
    vdr_current_info.stop_timer_flag = 1;
    vdr_current_info.start_timer_flag = 0;
    vdr_current_info.status_timer_flag = 0;
    vdr_current_info.emergency_alarm_flag = 0;
    vdr_current_info.emergency_alarm_num = 0;
    vdr_current_info.vssalarm_timer_flag = 0;
    vdr_current_info.ignition_timer_flag = 0;
    vdr_current_info.accoff_timer_flag = 0;
    vdr_current_info.car_reverse_flag = 0;
    vdr_current_info.car_reverse_count = 0;
    vdr_current_info.car_smoke_flag = 0;
    vdr_current_info.car_collision_flag = 0;
    vdr_current_info.car_turn_over_flag = 0;
    vdr_current_info.undervoltage_flag = 0;
    vdr_current_info.illegal_power_flag = 0;
    vdr_current_info.illegal_displacement_flag = 0;
    vdr_current_info.d310_can_ready_flag = 0;
    vdr_current_info.gnss_line_flag = 0;
    vdr_current_info.gnss_circuit_flag = 0;
    vdr_current_info.gps_mcu_speed_flag = 0;
    vdr_current_info.gps_speed_count = 0;
    vdr_current_info.change_time_flag = -1;
    vdr_current_info.upgrade_version_flag = 0;
    vdr_current_info.left_right_flag = 0;
    vdr_current_info.stop_flag=1; 
    vdr_current_info.running_flag=0; 
    vdr_current_info.speed_flag=0; 
    vdr_current_info.sleep_status_flag = 0;
    VDRBITSET(vdr_status_flag, LOCSTATUS_CAR_WORK_OFF);
    VDRBITSET(vdr_status_flag, LOCSTATUS_OILWAY_OFF);
    localtime_r(&timep, &p);
    temp_min = p.tm_min;
    min_info.m_time = (int)timep;

    unsigned char accstatus[2];
    DeviceACCInfoDbusNotifyC accinfo;
    I2c_read_acc_data(accstatus, 2);
    printf("****first  accstatus[2]***%d*\n",accstatus[0]);
    if(accstatus[0]&Hw0)
    {
        set_lcd_enable_disable(1);
        VDRBITSET(vdr_status_flag, LOCSTATUS_ACC_ONO);
        tmp_status_flag = 1;
        vdr_current_info.illegal_displacement_flag = 0;
        system("caraction unmutegf");
        accinfo.flag = 1;
        dev_DbusServer->sendNotify(&accinfo);
    }
    else if(!(accstatus[0]&Hw0))
    {
        VDRBITCLR(vdr_status_flag, LOCSTATUS_ACC_ONO);
        tmp_status_flag = 1;
        vdr_current_info.illegal_displacement = 0;
        set_lcd_enable_disable(0);
        system("caraction mutegf");
        accinfo.flag = 0;
        dev_DbusServer->sendNotify(&accinfo);
    }

    tmp = vdr_current_info.acc_on_time - vdr_current_info.acc_off_time + parked_dura;
    //printf("====%d,%d,%d,%d=%d===\n",vdr_current_info.acc_on_time,vdr_current_info.acc_off_time,parked_dura,tmp,vdr_param.max_parking_time);
    if((!(vdr_param.warning_mask & ALARM_STATUS_PARKING_OVERTIIME))&&(!(vdr_param.max_parking_time == 0))){

	    if(tmp >= vdr_param.max_parking_time)
	    {
		    change_alarm_flag_and_upload(ALARM_STATUS_PARKING_OVERTIIME, 1);
	    }
	    else 
	    {
		    parktimer->timeout = (vdr_param.max_parking_time-tmp)*1000;
		    StartTimer(parktimer);
		    vdr_current_info.park_timer_flag = 1;
	    }
    }
}

void write_vdr_day_info_table(int itime)
{
    int sec = 0;

    sec = get_day_zero_time(itime);
    //printf("sec=%d, %d, %d----\n",itime,sec,vdr_current_info.day_time);
    if(sec != vdr_current_info.day_time)
    {
        pretired_param.TotalTime=0;
        total_time_tmp = 0;
        total_time_start = sec;
        PreTired_Write();
        flag_total_time=0;
        change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,0);

        speed_info.day_speed_status = 0;
        if(vdr_current_info.status_timer_flag)
        {        
            vdr_current_info.status_timer_flag = 0;
            StopTimer(statustimer);
            memset(&speed_info,0,sizeof(struct SpeedStatusJudge));
        }
        
        DeviceSpeedAbnormalDbusNotifyC notify;
        notify.flag = 0;
        dev_DbusServer->sendNotify(&notify);

        int status=0;
        DayInfoWriteTable* table = new DayInfoWriteTable();
        table->dayinfo_data.itime = itime;
        table->dayinfo_data.drive_dura = vdr_current_info.day_drive_dura;
        table->dayinfo_data.drive_mileage= vdr_current_info.day_drive_mileage;
        table->dayinfo_data.total_mileage= vdr_current_info.total_mileage;
        
        status = workq_add (&workq_dayinfo, (void*)table);
        if (status != 0)
            err_abort (status, "Add runstatus to work queue");    

        vdr_current_info.day_time = sec;
        vdr_current_info.day_drive_dura = 0;
        vdr_current_info.day_drive_mileage = 0;
        vdr_current_info.day_tmp_mileage = 0;
    }

    if(vdr_current_info.total_mileage > 999999999)
    {
        VDRDeployParaTable *itable = new VDRDeployParaTable();
        itable->SetIntValue(VDR_DEPLOY_PARA_DEPLOY_MILEAGE, 0);
        itable->Commit();
        delete itable;
        vdr_current_info.total_mileage = 0;
        I2c_write_clear_mileage_data(0);
    }
}
void read_vdr_day_info_table()
{
    int ret = -1;
    int sec = 0;
    int itime = time(NULL);
    
    sec = get_day_zero_time(itime);
    
    VDRDayInfoTable *table = new VDRDayInfoTable();
    ret = table->RefreshBottom();
    if(ret < 0)
    {
        vdr_current_info.day_time = sec;
        vdr_current_info.day_drive_dura = 0;
        vdr_current_info.day_drive_mileage = 0;
        vdr_current_info.day_tmp_mileage = 0;
    }
    else
    {
        table->GetIntValue(VDR_DAY_OCCUR_TIME, vdr_current_info.day_time);
        table->GetIntValue(VDR_DAY_DRIVE_DURA, vdr_current_info.day_drive_dura);
        table->GetIntValue(VDR_DAY_DRIVE_MILEAGE, ret);
        table->GetIntValue(VDR_DAY_TOTAL_MILEAGE, vdr_current_info.total_mileage);
        if(vdr_current_info.day_time < sec)
        {
            vdr_current_info.day_time = sec;
            vdr_current_info.day_drive_dura = 0;
            vdr_current_info.day_drive_mileage = 0;
            vdr_current_info.day_tmp_mileage = 0;
            
            int status=0;
            DayInfoWriteTable* table = new DayInfoWriteTable();
            table->dayinfo_data.itime = itime;
            table->dayinfo_data.drive_dura = vdr_current_info.day_drive_dura;
            table->dayinfo_data.drive_mileage= vdr_current_info.day_drive_mileage;
            table->dayinfo_data.total_mileage= vdr_current_info.total_mileage;
            
            status = workq_add (&workq_dayinfo, (void*)table);
            if (status != 0)
                err_abort (status, "Add runstatus to work queue");    
        }
        else
            vdr_current_info.day_time = sec;
        
    }
    delete table;
    vdr_current_info.day_info_flag = 0;
}

void change_report_time_distance_mode(int type)
{
    StopTimer(uploadtimer);

    if(vdr_current_info.is_time_report)
    {
        switch(type)
        {
            case NOLOGIN_MODE:
                vdr_current_info.report_time = vdr_param.nologin_report_interval;
                break;
            case SLEEP_MODE:
                vdr_current_info.report_time = vdr_param.sleep_report_interval;
                break;
            case ALARM_MODE:
                vdr_current_info.report_time = vdr_param.alarm_report_interval;
                break;
            case DEFAULT_MODE:
            default:
                if(vdr_param.report_scenario && !Flag_DriverLogin)
                    vdr_current_info.report_time = vdr_param.nologin_report_interval;
                else
                    vdr_current_info.report_time = vdr_param.default_report_interval;
                
                break;
        }
        uploadtimer->timeout = vdr_current_info.report_time*1000;
        uploadtimer->timeleft = uploadtimer->timeout;
        uploadtimer->data = (void *)UPLOAD_TIMER_DATA;
        if(uploadtimer->timeout == 1000)
        {
            onesec_upload_flag = 1;
        }
        else
        {
            printf("start upposition timer =%d\n",uploadtimer->timeout);
            onesec_upload_flag = 0;
            StartTimer(uploadtimer);
        }
    }
    
    if(vdr_current_info.is_distance_report)
    {
        tempmileage = (int)tempmileage%100;
        switch(type)
        {
            case NOLOGIN_MODE:
                vdr_current_info.report_distance = vdr_param.nologin_report_distance;
                break;
            case SLEEP_MODE:
                vdr_current_info.report_distance = vdr_param.sleep_report_distance;
                break;
            case ALARM_MODE:
                vdr_current_info.report_distance = vdr_param.alarm_report_distance;
                break;
            case DEFAULT_MODE:
            default:
                if(vdr_param.report_scenario && !Flag_DriverLogin)
                    vdr_current_info.report_distance = vdr_param.nologin_report_distance;
                else
                    vdr_current_info.report_distance = vdr_param.default_report_distance;
                
                break;
        }
        printf("-1-tmpmileage=%f, report_distance=%d----\n",tempmileage,vdr_current_info.report_distance);
    }
    vdr_current_info.report_mode = type;

}



void CNaviGPSInfoReportNotifyHandler(DBusNotifyC *pNotify)
{    
    if(pNotify->getIdentifier()!=NOTF_ID_NAVI_GPSINFO_REPORT)
        return;
#if 0    
    CNaviGPSInfoReportNotifyC *locnotify = (CNaviGPSInfoReportNotifyC*)pNotify;
    
    pthread_mutex_lock(&gps_mutex);
    gps_info.position = locnotify->cPositioning;
    gps_info.fX = locnotify->fX*1000000;
    gps_info.fY = locnotify->fY*1000000;
    gps_info.iTime = locnotify->iTime;
    gps_info.iSysTime = locnotify->iSysTime;
    gps_info.iOrientation = locnotify->iOrientation;
    gps_info.iVelocity = locnotify->iVelocity;    
    gps_info.ilatitude = locnotify->ilatitude;
    gps_info.ilongitude = locnotify->ilongitude;
    gps_info.ialtitude = locnotify->ialtitude;
    gps_info.mode = locnotify->cMode + 1;
    gps_info.gpsnum = locnotify->cSatellitenum;
    gps_info.bdnum = locnotify->cBDSatellitenum;
    
    if(gps_info.position == 0)
    {
        gps_info.fX = 0;
        gps_info.fY = 0;
        gps_info.iOrientation = 0;
        gps_info.iVelocity = 0;
        gps_info.ialtitude = 0;
        gps_info.mode = 0;
    }

	printf("fx=%d, fy=%d, itime=%d,isystime=%d,iOrientation=%d,iVelocity=%d,----ilatitude=%d,ilongitude=%d,ialtitude=%d \n",
		gps_info.fX,gps_info.fY,gps_info.iTime,gps_info.iSysTime,gps_info.iOrientation,gps_info.iVelocity,
		gps_info.ilatitude, gps_info.ilongitude,gps_info.ialtitude);

    pthread_mutex_unlock(&gps_mutex);
#endif
}

void CNaviElectronicFenceStatetNotifyHandler(DBusNotifyC *pNotify)
{
    if(pNotify->getIdentifier()!=NOTF_ID_NAVI_ELECTRONIC_FENCE_STATE)
        return;

    int itime;
    DeviceFenceInOutDbusNotifyC notify;
    
    CNaviElectronicFenceStateNotifyC *fencetatus = (CNaviElectronicFenceStateNotifyC*)pNotify;
	
	printf("etype=%d, eid=%d, estate=%d,epoint=%d,edeviation=%d, itime=%d,gpstime=%d\n",
		fencetatus->etype,fencetatus->eid,fencetatus->estate,fencetatus->epoint,fencetatus->edeviation,
		fencetatus->iTime, gps_info.iTime);
    gd_inout_time = fencetatus->iTime;
    
    switch(fencetatus->etype)
    {
        case 0:
        case 1:
        case 2:
            if(fencetatus->estate == 1)
            {
                read_electronic_fence_table(fencetatus->etype, fencetatus->eid); 
                add_info.inout_alarm_flag = fencetatus->etype + 1;
                add_info.inout_alarm_region_id = fencetatus->eid;
                add_info.inout_alarm_direction = 0;
                
                if(fence_info.attribute & Hw2)
                {
                    notify.flag = 1;
                }
                else
                    notify.flag = 2;

                if(fence_info.attribute & Hw3)
                {
                    vdr_current_info.emergency_alarm_num = 1;
                    change_alarm_flag_and_upload(ALARM_STATUS_IN_OUT_AREA, 1); 
                    tmp_alarm_flag = 1;
                    gd_inout_flag = 1;
                }
                if(fence_info.attribute & Hw14)
                    notify.cm_flag = 0;
                else
                    notify.cm_flag = 1;
                
                if(fence_info.attribute & Hw15)
                    notify.gnss_flag = 1;
                else
                    notify.gnss_flag = 0;
                
                if((dreg_info.active_disable == 1) && (fencetatus->etype == 0))
                {   // D310 渣土车举升控制
                    dreg_info.lift_status = 1;
                    dreg_info.wait_reply_flag = 1;
                    dreg_info.command_type = 5;
                    I2c_write_CM_Control_Module_data(0x05);
                    notify.flag = 4;
                    notify.cm_flag = 1;
                }
                
                printf("DeviceFenceInOutDbusNotifyC flag=%d, cm_flag=%d, gnss_flag=%d\n",
                    notify.flag, notify.cm_flag, notify.gnss_flag);
                dev_DbusServer->sendNotify(&notify);
            }
            else if(fencetatus->estate == 0)
            {
                add_info.inout_alarm_direction = 1;
                fence_info.instatus = 0;
                if(fence_info.attribute & Hw4)
                {
                    notify.flag = 0;
                }
                else
                    notify.flag = 2;
                
                if(fence_info.attribute & Hw5)
                {
                    vdr_current_info.emergency_alarm_num = 2;
                    change_alarm_flag_and_upload(ALARM_STATUS_IN_OUT_AREA, 1); 
                    tmp_alarm_flag = 1;
                    gd_inout_flag = 1;
                }
                
                notify.cm_flag = 1;
                notify.gnss_flag = 0;

                if((dreg_info.active_disable == 1) && (fencetatus->etype == 0))
                {   // D310 渣土车举升控制
                    dreg_info.lift_status = 0;
                    dreg_info.wait_reply_flag = 1;
                    dreg_info.command_type = 5;
                    I2c_write_CM_Control_Module_data(0x06);
                    notify.flag = 4;
                    notify.cm_flag = 0;
                }
                
                dev_DbusServer->sendNotify(&notify);

                vdr_current_info.max_speed = vdr_param.max_speed;
                vdr_current_info.over_speed_dura = vdr_param.over_speed_duration;
            }
            
            if(vdr_current_info.speed_timer_flag)
            {
                StopTimer(speedtimer);
                vdr_current_info.speed_timer_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0);            
            }
            break;
        case 3:
            if(fencetatus->estate==1)
            {
                read_electronic_fence_table(fencetatus->etype, fencetatus->eid); 
                add_info.inout_alarm_flag = fencetatus->etype + 1;
                add_info.inout_alarm_region_id = fencetatus->eid;
                add_info.inout_alarm_direction = 0;

                if(fence_info.attribute & Hw2)
                {
                    notify.flag = 1;
                }
                else
                    notify.flag = 2;
                
                if(fence_info.attribute & Hw3)
                {
                    vdr_current_info.emergency_alarm_num = 3;
                    change_alarm_flag_and_upload(ALARM_STATUS_IN_OUT_LINE, 1); 
                    tmp_alarm_flag = 1;
                    gd_inout_flag = 1;
                }
                
                if(fence_info.attribute & Hw14)
                    notify.cm_flag = 0;
                else
                    notify.cm_flag = 1;
                
                if(fence_info.attribute & Hw15)
                    notify.gnss_flag = 1;
                else
                    notify.gnss_flag = 0;
                
                fence_info.nowpoint = fencetatus->epoint;
                if(fence_info.inflexion_info[fence_info.nowpoint].lineattribute & Hw0)
                {
                    itime = fence_info.inflexion_info[fence_info.nowpoint].maxtime;
                    linetimer->timeout = itime*1000;
                    StartTimer(linetimer);
                    fence_info.temptime = (int)time(NULL);
                }

                if(fence_info.inflexion_info[fence_info.nowpoint].lineattribute & Hw1)
                {
                    fence_info.attribute |= Hw1;
                    vdr_current_info.max_speed = fence_info.inflexion_info[fence_info.nowpoint].maxspeed;
                    vdr_current_info.over_speed_dura= fence_info.inflexion_info[fence_info.nowpoint].overspeeddura;
                
                    if(dreg_info.active_disable == 1)
                    {   // D310 渣土车限速
                        dreg_info.speed_status = 1;
                        dreg_info.speed_value = vdr_current_info.max_speed;
                        dreg_info.speed_dura = vdr_current_info.over_speed_dura;
                        I2c_write_CM_Speed_Control_data(dreg_info.speed_value/10);
                        dreg_info.wait_reply_flag = 1;
                        dreg_info.command_type = 4;
                        
                        notify.flag = 3; 
                        notify.cm_flag = dreg_info.speed_value;
                    }
                } 
                
                dev_DbusServer->sendNotify(&notify);

                if(vdr_current_info.speed_timer_flag)
                {
                    StopTimer(speedtimer);
                    vdr_current_info.speed_timer_flag = 0;                    
                    change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0);            
                }
                
            }
            else if(fencetatus->estate==0)
            {
                add_info.inout_alarm_direction = 1;
                fence_info.instatus = 0;
                if(fence_info.attribute & Hw4)
                {
                    notify.flag = 0;
                }
                else
                    notify.flag = 2;
                
                if(fence_info.attribute & Hw5)
                {
                    vdr_current_info.emergency_alarm_num = 2;
                    change_alarm_flag_and_upload(ALARM_STATUS_IN_OUT_LINE, 1); 
                    tmp_alarm_flag = 1;
                    gd_inout_flag = 1;
                }
                
                notify.cm_flag = 1;
                notify.gnss_flag = 0;
                dev_DbusServer->sendNotify(&notify);
                
                if(vdr_current_info.speed_timer_flag)
                {
                    StopTimer(speedtimer);
                    vdr_current_info.speed_timer_flag = 0;
                    change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0);            
                }
                vdr_current_info.max_speed = vdr_param.max_speed;
                vdr_current_info.over_speed_dura = vdr_param.over_speed_duration;
                
                if(dreg_info.active_disable == 1)
                {   // D310 渣土车限速
                    dreg_info.speed_status = 0;
                    dreg_info.speed_value = 0;
                    dreg_info.speed_dura = 0;
                    I2c_write_CM_Speed_Control_data(0);
                    dreg_info.wait_reply_flag = 1;
                    dreg_info.command_type = 4;
                }
            }
            else if(fencetatus->estate==2)
            {
                if(fence_info.nowpoint == fencetatus->epoint)
                {
                    if(fencetatus->edeviation == 1)
                    {
                        vdr_current_info.emergency_alarm_num = 1;
                        change_alarm_flag_and_upload(ALARM_STATUS_LINE_DEPARTURE_ALARM, 1);
                        add_info.inout_alarm_direction = 1;
                        change_alarm_flag_and_upload(ALARM_STATUS_IN_OUT_LINE, 1); 
                        gd_inout_flag = 1;
                        gd_line_flag = 1;
                    }
                    else if(fencetatus->edeviation == 2)
                    {
                        vdr_current_info.emergency_alarm_num = 0;
                        change_alarm_flag_and_upload(ALARM_STATUS_LINE_DEPARTURE_ALARM, 0);
                        gd_inout_flag = 1;
                        gd_line_flag = 0;
                    }
               }
               else
               {
                    vdr_current_info.emergency_alarm_num = 0;
                    change_alarm_flag_and_upload(ALARM_STATUS_LINE_DEPARTURE_ALARM, 0);
                    gd_inout_flag = 1;
                    gd_line_flag = 0;
                    if(fence_info.inflexion_info[fence_info.nowpoint].lineattribute & Hw0)
                    {
                        StopTimer(linetimer);
                        itime = (int)time(NULL) - fence_info.temptime;
                        if(itime < fence_info.inflexion_info[fence_info.nowpoint].mintime)
                        {
                            add_info.short_long_line_id = fence_info.inflexion_info[fence_info.nowpoint].lineid;
                            add_info.short_long_drive_time = itime;
                            add_info.short_long_result = 0;
                            change_alarm_flag_and_upload(ALARM_STATUS_ROAD_DRIVE_SHORT_LONG, 1); 
                        }
                        else if(itime > fence_info.inflexion_info[fence_info.nowpoint].maxtime)
                        {
                            add_info.short_long_line_id = fence_info.inflexion_info[fence_info.nowpoint].lineid;
                            add_info.short_long_drive_time = itime;
                            add_info.short_long_result = 1;
                            change_alarm_flag_and_upload(ALARM_STATUS_ROAD_DRIVE_SHORT_LONG, 1); 
                        }
                    }

                    fence_info.nowpoint = fencetatus->epoint;
                    
                    if(fence_info.inflexion_info[fence_info.nowpoint].lineattribute & Hw0)
                    {
                        itime = fence_info.inflexion_info[fence_info.nowpoint].maxtime;
                        linetimer->timeout = itime*1000;
                        StartTimer(linetimer);
                        fence_info.temptime = (int)time(NULL);
                    }

                    if(fence_info.inflexion_info[fence_info.nowpoint].lineattribute & Hw1)
                    {
                        fence_info.attribute |= Hw1;
                        vdr_current_info.max_speed = fence_info.inflexion_info[fence_info.nowpoint].maxspeed;
                        vdr_current_info.over_speed_dura = fence_info.inflexion_info[fence_info.nowpoint].overspeeddura;
                    } 
                    
                    if(vdr_current_info.speed_timer_flag)
                    {
                        StopTimer(speedtimer);
                        vdr_current_info.speed_timer_flag = 0;
                        change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0);            
                    }
               }
            }
            break;
    }
    
    printf("---gd_inout_time=%d, gd_line_flag=%d----\n",gd_inout_time,gd_line_flag);
}

void JT808LocReportDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_LOC_REPORT && notify->getIdentifier()!=JT8082_DBUS_NTF_ID_LOC_REPORT)
        return;

    JT808LocReportDbusNotifyC *locnotify = (JT808LocReportDbusNotifyC *)notify;

    if(locnotify->confirm_alarm == 4)
    {
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_EMERGENCY_ALARM);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_EARLY_WARNING);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_IN_OUT_AREA);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_IN_OUT_LINE);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_ROAD_DRIVE_SHORT_LONG);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_CAR_ILLEGAL_IGNITION);
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_CAR_ILLEGAL_OFFSET);
        vdr_current_info.emergency_alarm_num = 0;
    }

    //receive ask 
    if(locnotify->confirm_alarm & Hw0)
    {
        change_alarm_flag_and_upload(ALARM_STATUS_EMERGENCY_ALARM, 0);
        change_report_time_distance_mode(DEFAULT_MODE);
    }
    if(locnotify->confirm_alarm & Hw3)
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_EARLY_WARNING);
    if(locnotify->confirm_alarm & Hw20)
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_IN_OUT_AREA);
    if(locnotify->confirm_alarm & Hw21)
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_IN_OUT_LINE);
    if(locnotify->confirm_alarm & Hw22)
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_ROAD_DRIVE_SHORT_LONG);
    if(locnotify->confirm_alarm & Hw27)
    {
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_CAR_ILLEGAL_IGNITION);
        vdr_current_info.ignition_timer_flag = 0;
    }
    if(locnotify->confirm_alarm & Hw28)
    {
        vdr_current_info.illegal_displacement = 0;
        vdr_current_info.illegal_displacement_flag = 0;
        VDRBITCLR(vdr_alarm_flag, ALARM_STATUS_CAR_ILLEGAL_OFFSET);
    }
    
}

void JT808TempLocReportDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_TEMP_LOC_REPORT && notify->getIdentifier()!=JT8082_DBUS_NTF_ID_TEMP_LOC_REPORT)
        return;

    JT808TempLocReportDbusNotifyC *JT808Notify=(JT808TempLocReportDbusNotifyC*)notify;

    if(JT808Notify->interval == 0)
    {
        if(vdr_current_info.is_time_report)
            change_report_time_distance_mode(DEFAULT_MODE);
        else
            StopTimer(uploadtimer);
    }
    else
    {
        if(vdr_current_info.is_time_report)
            StopTimer(uploadtimer);

        onesec_upload_flag = 0;
        uploadtimer->timeout = JT808Notify->interval*1000;
        uploadtimer->timeleft = uploadtimer->timeout;
        uploadtimer->data = (void *)TEMP_TIMER_DATA;
        StartTimer(uploadtimer);
        if(JT808Notify->validity <= 0)
            vdr_current_info.temp_report_validity = 0;
        else
            vdr_current_info.temp_report_validity = (int)time(NULL) + JT808Notify->validity;
    }
}

void JT808SettingUpdateDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_SETTING_UPDATE && notify->getIdentifier()!=JT8082_DBUS_NTF_ID_SETTING_UPDATE)
        return;

    JT808SettingUpdateDbusNotifyC *snotify=(JT808SettingUpdateDbusNotifyC*)notify;

    printf("---JT808SettingUpdateDbusNotifyHandler---%d--\n",snotify->flag);
    //if(vdr_current_info.is_time_report)
    //    StopTimer(uploadtimer);
    if(vdr_current_info.is_distance_report)
        tempmileage = 0;
    //if(vdr_current_info.speed_timer_flag)
        //StopTimer(speedtimer);
    if(vdr_current_info.park_timer_flag)
        StopTimer(parktimer);
    if(vdr_current_info.sleep_timer_flag)
        StopTimer(sleeptimer);
    
    StopTimer(picturetimer);

    read_vdr_run_param_table();

    if(snotify->flag & Hw0)
    {
        if(vdr_alarm_flag & ALARM_STATUS_EMERGENCY_ALARM) 
            change_report_time_distance_mode(ALARM_MODE);
        else
            change_report_time_distance_mode(DEFAULT_MODE);
    }

    if(snotify->flag & Hw1)
    {
        int init_mileage = 0;
        
        VDRDeployParaTable *table = new VDRDeployParaTable();
        table->GetIntValue(VDR_DEPLOY_PARA_DEPLOY_MILEAGE, init_mileage);
        delete table;
        I2c_write_clear_mileage_data(init_mileage/10);
    }
    if(snotify->flag & Hw8)
    {       //恢复出厂设置
        if(vdr_alarm_flag & ALARM_STATUS_EMERGENCY_ALARM) 
            change_report_time_distance_mode(ALARM_MODE);
        else
            change_report_time_distance_mode(DEFAULT_MODE);

        I2c_write_clear_mileage_data(0);
    }
        
    
    //vdr_current_info.speed_timer_flag = 0;
    vdr_current_info.park_timer_flag = 0; 
    vdr_current_info.sleep_timer_flag = 0; 

    pretired_param.RunningTime=0;
}
void JT808ParkingTestDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_PARKING_TEST && notify->getIdentifier()!=JT8082_DBUS_NTF_ID_PARKING_TEST)
        return;
    
    JT808ParkingDbusNotifyC *ParkNotify=(JT808ParkingDbusNotifyC*)notify;
    printf("---JT808ParkingDbusNotifyC- flag=%d\n",ParkNotify->flag);
    if(ParkNotify->flag == 3 && begin_can_test == 0)
    {
        begin_can_test = 1;
        
        int ret = -1;
        pthread_t can_tid;
        ret = pthread_create(&can_tid, NULL, udp_send_read_can_data, NULL);
        if(ret < 0)
        {
            printf("create udp send can data error!\n");
            return ;
        }
        pthread_detach(can_tid);
    }
    if(ParkNotify->flag == 5)
    {
        //上报0x0201 位置信息

        tmp_status_flag = 5;
    }
    if(ParkNotify->flag == 9)
    {
        //车辆控制0x8500  上报位置信息
        tmp_status_flag = 9;
        if(ParkNotify->value == 0)
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARDOOR_LOCK);
        else    
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARDOOR_LOCK);
    }
    
}

int  check_external_dev_sum(unsigned char *buf,int len)
{
	int i;
	int temp = 0;

	for(i=0; i<len; i++)
	{
		temp = temp + buf[i];
	}
	return temp;
}

void JT808DataTransmitDbusNotifyHandler(DBusNotifyC *notify)
{
    unsigned char sed_buf[128];
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_DATA_TRANSMIT && notify->getIdentifier()!=JT8082_DBUS_NTF_ID_DATA_TRANSMIT)
        return;
    DeviceDriveLoginDbusNotifyC drivenotify;
    JT808DataTransmitNotifyC *DataNotify=(JT808DataTransmitNotifyC*)notify;
    if(DataNotify->type == 0x0B)
    {
        printf("revjt808:len=%d,errnum=%d, ",DataNotify->total,DataNotify->errnum);
        for(int j=0; j<DataNotify->total;j++)
        {
            printf("%02x ",DataNotify->buf[j]);
        }
        printf("\n");
        memset(sed_buf, 0, 128);
        sed_buf[0] = 0x7E;
        sed_buf[1] = 0x00;
        sed_buf[2] = iccard_info.version[0];
        sed_buf[3] = iccard_info.version[1];
        sed_buf[4] = iccard_info.manuid[0];
        sed_buf[5] = iccard_info.manuid[1];
        sed_buf[6] = iccard_info.type;
        sed_buf[7] = 0x40;

        if(DataNotify->errnum == 0x00)
        {
            sed_buf[8] = 0x00;
            memcpy(sed_buf+9,DataNotify->buf,DataNotify->total);
            sed_buf[1] = check_external_dev_sum(sed_buf+4, 4+DataNotify->total+1) & 0x00FF;
            sed_buf[9+DataNotify->total] = 0x7E;
            WriteComPort(iccard_fd, sed_buf, 10+DataNotify->total);
        }
        else if(DataNotify->errnum == 0x01)
        {
            sed_buf[8] = 0x01;
            sed_buf[9] = 0x7E;
            sed_buf[1] = check_external_dev_sum(sed_buf+4, 4+2) & 0x00FF;
            WriteComPort(iccard_fd, sed_buf, 10);
            drivenotify.command = 0x40;
            drivenotify.type = 0x06;
            dev_DbusServer->sendNotify(&drivenotify);
        }
        else if(DataNotify->errnum == 0x02)
        {
            sed_buf[8] = 0x02;
            sed_buf[9] = 0x7E;
            sed_buf[1] = check_external_dev_sum(sed_buf+4, 4+2) & 0x00FF;
            WriteComPort(iccard_fd, sed_buf, 10);
            drivenotify.command = 0x40;
            drivenotify.type = 0x07;
            dev_DbusServer->sendNotify(&drivenotify);
        }

        int j=0;
        for(j=0; j<sizeof(sed_buf);j++)
        {
            printf("%02x ",sed_buf[j]);
        }
        printf(" ,end revjt808\n");
    }
    else if(DataNotify->type == 0x41 || DataNotify->type == 0x42)
    {
        printf("0x41:len=%d,errnum=%d, ",DataNotify->total,DataNotify->errnum);
        for(int j=0; j<DataNotify->total;j++)
        {
            printf("%02x ",DataNotify->buf[j]);
        }
        printf("\n");
    
        data_down_pass_through_info(DataNotify->buf,DataNotify->total);
    }
    else if(DataNotify->type == 0xF8)
    {
        unsigned char downparam[7];
        int len = DataNotify->total - 2;
        printf("revjt808:len=%d,errnum=%d, ",DataNotify->total,DataNotify->errnum);
        for(int j=0; j<DataNotify->total;j++)
        {
            printf("%02x ",DataNotify->buf[j]);
        }
        printf("\n");

        downparam[0] = len & 0xFF;
        downparam[1] = (len>>8) & 0xFF;
        if((len%7) == 0)
            downparam[2] = len/7;
        else
            downparam[2] = len/7 + 1;
        downparam[3] = DataNotify->buf[0];
        downparam[4] = DataNotify->buf[1];
        downparam[5] = 0x00;

        printf("send to mcu: %02x %02x %02x %02x %02x %02x\n",downparam[0],downparam[1],
            downparam[2],downparam[3],downparam[4],downparam[5]);
        int j = 0;
        
        I2c_write_can_down_param_info(downparam, 6);
        for(int i=0; i<downparam[2]; i++)
        {
            I2c_write_can_down_data_info(DataNotify->buf+2+i*7, 7);
            for(j=0; j<7;j++)
            {
                printf("%02x ",DataNotify->buf[2+i*7+j]);
            }
            printf("\n");
        }
    }

}
void JT808GNSSChangeDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=NOTF_ID_NAVI_GNSS_ATTR_CHANGE)
        return;
    
    CGNSSAttrChangeNotifyC *gnssnotify=(CGNSSAttrChangeNotifyC*)notify;
    gnss_mode = gnssnotify->mode;
    printf("CGNSSAttrChangeNotifyC mode=%d\n",gnss_mode);
    send_change_gnss_mode_udp(&gnssnotify->mode, 4);
}
void check_ignition_alarm_info()
{
    if((authentication_result == 0)&&(vdr_param.report_scenario == 1)&&(Flag_DriverLogin == false))
    {
        if(vdr_current_info.ignition_timer_flag == 0)
        {
            StartTimer(ignitiontimer);
            vdr_current_info.ignition_timer_flag = 1;
        }
    }
    else
    {
        StopTimer(ignitiontimer);
        vdr_current_info.ignition_timer_flag = 0;
    }

}
void JT808AuthenticationDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_AUTHENTICATION)
        return;
    JT808AuthenticationDbusNotifyC *aunotify=(JT808AuthenticationDbusNotifyC*)notify;
    
    printf("==================result=%d==============\n",aunotify->result);
    authentication_result = aunotify->result;
    check_ignition_alarm_info();
}

void JT808LocTimeDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_LOCTIME)
        return;
    
    JT808LocTimeDbusNotifyC *tnotify=(JT808LocTimeDbusNotifyC*)notify;
    
    printf("JT808LocTimeDbusNotifyC time=%d,%d,%d\n",tnotify->LocUtcTime,(int)time(NULL),tnotify->LocInterval);

    StopTimer(uploadtimer);
    int icount=60;
    while(1)
    {
        if(vdr_status_flag & LOCSTATUS_FIXED_POSITION)
        {
            udp_loc.itime = tnotify->LocUtcTime+tnotify->LocInterval;
            send_position_to_JT808_udp(-1);
            break;
        }

        icount--;
        //printf("==========time=%d, icount=%d====\n",(int)time(NULL),icount);
        if(icount == 0)
        {    
            udp_loc.itime = tnotify->LocUtcTime+tnotify->LocInterval;
            send_position_to_JT808_udp(-1);
            break;
        }
        sleep(1);
    }

}

void JT808AccOffDbusNotifyHandler(DBusNotifyC *notify)
{
    printf("------------jt808 acc off dbus notify-----\n");

    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_ACCOFF)
        return;
    
    shutdown_reboot_flag = 1;
}

void JT808DregCommonNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=JT808_DBUS_NTF_ID_DREG_COMMON)
        return;
    
    JT808DregCommonNotifyC *dnotify=(JT808DregCommonNotifyC*)notify;
    switch(dnotify->type)
    {
        case 1:
            //激活
            if(dreg_info.active_disable == 0)
            {
                I2c_write_CM_Control_Module_data(0x01);
                dreg_info.wait_reply_flag = 1;
                dreg_info.command_type = 1;
            }
            break;
        case 0:
            //解锁
            if(dreg_info.active_disable == 1)
            {
                I2c_write_CM_Control_Module_data(0x02);
                dreg_info.wait_reply_flag = 1;
                dreg_info.command_type = 2;
            }
            break;
        default:
            break;
    }
}

void PhoneDbusNotifyHandler(DBusNotifyC *notify)
{
    if(notify->getIdentifier()!=NOTF_ID_PHONE)
        return;

    PhoneNotifyC *phonenotify=(PhoneNotifyC*)notify;
    add_info.wireless_intensity = phonenotify->signal_strength;
    
    printf("phone dbus wireless=%d\n",add_info.wireless_intensity);
}

int  change_alarm_flag_and_upload(int flag,int set)
{
    if(vdr_param.warning_mask & flag)
        return -1;

    if(set == 1)
    {
        if((vdr_alarm_flag & flag) == 0)
        {
            tmp_alarm_flag = 1;
            VDRBITSET(vdr_alarm_flag, flag); 
            send_device_alarm_camera_notify(flag);
            send_device_SMS_alarm_notify(flag);
        }
    }
    else
    {
        if((vdr_alarm_flag & flag)!=0)
        {
            VDRBITCLR(vdr_alarm_flag, flag);
            tmp_alarm_flag = 1;
        }
    }
    
    return 0;
}
int check_over_speed_warning_alarm(int speed)
{
    if((vdr_param.warning_mask & ALARM_STATUS_OVERSPEED_ALARM) || (vdr_current_info.max_speed == 0))
        return -1;
    
    if(fence_info.instatus && !(fence_info.attribute&Hw1))
        return -1;
        
    if(speed >= vdr_current_info.max_speed - vdr_param.overspeed_warning /10)//预警差值单位0.1km/h
    {    
         if(speed >= vdr_current_info.max_speed)
        {    
            if(!vdr_current_info.speed_timer_flag)
            {
                if(vdr_current_info.over_speed_warn_flag)
                {
                    vdr_current_info.over_speed_warn_flag = 0;
                    change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_WARNING, 0);
                }
                vdr_current_info.speed_timer_flag = 1;
                if(vdr_current_info.over_speed_dura == 0)
                {
                    if(fence_info.instatus)
                    {
                        switch(fence_info.etype)
                        {
                            case 0:
                            case 1:
                            case 2:
                                add_info.overspeed_alarm_flag = fence_info.etype+1;
                                add_info.overspeed_region_id = fence_info.regionid;
                                break;
                            case 3:
                                add_info.overspeed_alarm_flag = 4;
                                add_info.overspeed_region_id =fence_info.inflexion_info[fence_info.nowpoint].lineid;
                                printf("line overspeed alarm \n");
                                break;
                        }
                    }
                    else
                    {
                        add_info.overspeed_alarm_flag = 0;
                        printf("default overspeed alarm \n");
                    }

                    change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 1);
                    return 0;
                }
                speedtimer->timeout = vdr_current_info.over_speed_dura*1000;
                StartTimer(speedtimer);
            }
        }
        else
        {
            if(!vdr_current_info.over_speed_warn_flag)
            {
                vdr_current_info.over_speed_warn_flag = 1;
                change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_WARNING, 1);
            }
            
            if(vdr_current_info.speed_timer_flag)
            {        
                vdr_current_info.speed_timer_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0);
                
                if(vdr_current_info.over_speed_dura == 0)
                    return 0;
                
                StopTimer(speedtimer);
            }
        }
        
    }
    else
    {
        if(vdr_current_info.over_speed_warn_flag)
        {        
            vdr_current_info.over_speed_warn_flag = 0;
            change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_WARNING, 0);
        }

        if(vdr_current_info.speed_timer_flag)
        {        
           vdr_current_info.speed_timer_flag = 0;
           change_alarm_flag_and_upload(ALARM_STATUS_OVERSPEED_ALARM, 0); 
            
            if(vdr_current_info.over_speed_dura == 0)
                return 0;
            
            StopTimer(speedtimer);
        }
    }
    return 0;        
}
    
void check_speed_status_info(int vdr_speed, int gps_speed, int itime)
{
    int ret = 0;
    if(!speed_info.day_speed_status)
    {
        if(gps_speed > 40)
        {    
            if(!vdr_current_info.status_timer_flag)
            {
                vdr_current_info.status_timer_flag = 1;
                StartTimer(statustimer);
                ret = compute_vdrspeed_gpsspeed(vdr_speed, gps_speed);
                speed_info.index = 0;
                speed_info.beginsec = itime;
                speed_info.result = ret;
                speed_info.vdr_speed[0] = vdr_speed;
                speed_info.gps_speed[0] = gps_speed;
            }
            else
            {
                ret = compute_vdrspeed_gpsspeed(vdr_speed, gps_speed);
                if(ret == speed_info.result)
                {
                    if(speed_info.index < 59)
                    {
                        speed_info.index ++;
                        speed_info.vdr_speed[speed_info.index] = vdr_speed;
                        speed_info.gps_speed[speed_info.index] = gps_speed;
                    }
                }
                else
                {
                    vdr_current_info.status_timer_flag = 0;
                    StopTimer(statustimer);
                    memset(&speed_info,0,sizeof(struct SpeedStatusJudge));
                }
            }
        }
        else
        {
            if(vdr_current_info.status_timer_flag)
            {        
                vdr_current_info.status_timer_flag = 0;
                StopTimer(statustimer);
                memset(&speed_info,0,sizeof(struct SpeedStatusJudge));
            }
        }
    }
}

void check_vss_alarm_info(int vdr_speed, int gps_speed)
{
    if((gps_speed > 5) && (vdr_speed == 0))
    {    
        if(!vdr_current_info.vssalarm_timer_flag)
        {
            vdr_current_info.vssalarm_timer_flag = 1;
            StartTimer(vsstimer);
        }
    }
    else
    {
        if(vdr_current_info.vssalarm_timer_flag)
        {        
            vdr_current_info.vssalarm_timer_flag = 0;
            StopTimer(vsstimer);
            change_alarm_flag_and_upload(ALARM_STATUS_CAR_VSS_FAILURE, 0);
        }
    }
}

void parse_detail_status_info(unsigned char *buf)
{
    DeviceReverseDbusNotifyC notify;
    int i=0,leftcount=0,rightcount=0, d310count=0;
    
    for(i=0;i<5; i++)
    {
        if(buf[i*2]&Hw0)
        {
            if(vdr_current_info.car_reverse_flag != 1)
            {
                vdr_current_info.car_reverse_count++;
                
                if(vdr_current_info.car_reverse_count > 2)
                {
                    vdr_current_info.car_reverse_flag = 1;
                    notify.flag = 1;
                    notify.icount = reverse_icount++;
                    dev_DbusServer->sendNotify(&notify);
                }
            }
        }
        else 
        {
            if(vdr_current_info.car_reverse_flag == 1)
            {
                vdr_current_info.car_reverse_count--;
        
                if(vdr_current_info.car_reverse_count == 0)
                {
                    vdr_current_info.car_reverse_flag = 0;
                    notify.flag = 0;
                    notify.icount = reverse_icount++;
                    dev_DbusServer->sendNotify(&notify);
                }
            }
            else
                vdr_current_info.car_reverse_count = 0;
        }
        
        if(buf[i*2] & Hw1)
        {
            if(vdr_current_info.car_reverse_flag == 0)
            {
                d310count++;
                if(d310count > 2)
                {
                    vdr_current_info.car_reverse_flag = 4;
                    notify.flag = 2;
                    notify.icount = reverse_icount++;
                    printf("===D310 CM===\n");
                    dev_DbusServer->sendNotify(&notify);
                }
            }
        }
        else
        {
            if(vdr_current_info.car_reverse_flag == 4)
            {
                vdr_current_info.car_reverse_flag = 0;
                notify.flag = 0;
                notify.icount = reverse_icount++;
                printf("===out D310 CM===\n");
                dev_DbusServer->sendNotify(&notify);
            }
        }
        
        if(buf[i*2] & Hw6)
        {
            if(vdr_current_info.car_reverse_flag == 0)
            {
                leftcount++;
                if(leftcount > 2)
                {
                    vdr_current_info.car_reverse_flag = 2;
                    notify.flag = 2;
                    notify.icount = reverse_icount++;
                    printf("===left===\n");
                    dev_DbusServer->sendNotify(&notify);
                }
            }
        }
        else
        {
            if(vdr_current_info.car_reverse_flag == 2)
            {
                vdr_current_info.car_reverse_flag = 0;
                notify.flag = 0;
                notify.icount = reverse_icount++;
                printf("===out left===\n");
                dev_DbusServer->sendNotify(&notify);
            }
        }

        if(buf[i*2] & Hw5)
        {
            if(vdr_current_info.car_reverse_flag == 0)
            {
                rightcount++;
                if(rightcount > 2)
                {
                    vdr_current_info.car_reverse_flag = 3;
                    notify.flag = 3;
                    notify.icount = reverse_icount++;
                    printf("===right===\n");
                    dev_DbusServer->sendNotify(&notify);
                }
            }
        }
        else
        {
            if(vdr_current_info.car_reverse_flag == 3)
            {
                vdr_current_info.car_reverse_flag = 0;
                notify.flag = 0;
                notify.icount = reverse_icount++;
                printf("===out right===\n");
                dev_DbusServer->sendNotify(&notify);
            }
        }
    }
}
     
int parse_speed_status_info(unsigned char *buf,int len, int itime)
{
    int speed = 0, tmpspd = 0;
    float distance = 0, tmpdist = 0;
    int i=0, ret = 0;
    int tmp_status = 0;
    string timername;
    
    for(i=0;i<len; i++)
    {
        speed += buf[i*2+1];
        buf[i*2] = STATUS_SWITCH(buf[i*2], vdr_current_info.status_level, vdr_current_info.status_enable);
        tmp_status |= buf[i*2];
    }
    
    if((vdr_current_info.upgrade_version_flag==1 && vdr_current_info.left_right_flag == 1)||(dreg_info.active_disable == 1))
    {
        parse_detail_status_info(buf);
        //printf("===%02x,%02x==%d=\n", speed_distance.signal, tmp_status,vdr_current_info.car_reverse_flag);
    }

    time_t timep;
    struct tm p;

    timep = itime;
    localtime_r(&timep, &p);
    ret = itime - min_info.m_time - 1;
    
    tmpspd = speed_distance.sec_speed[speed_distance.index];
    tmpdist = speed_distance.sec_distance[speed_distance.index]; 
    
    if((p.tm_min > temp_min)||((p.tm_min ==0) && (temp_min == 59)))
    {
        if(ret > p.tm_sec)
        {
            if(tmpspd !=0)
            {
                min_info.m_speed = min_info.m_speed + tmpspd*(ret-p.tm_sec) ; 
                min_info.m_count = min_info.m_count + (ret-p.tm_sec);
            }
        }

        if(min_info.m_count != 0)
            min_info.t_speed  = min_info.m_speed*10/60;   //min_info.m_speed*10/min_info.m_count;
        else
            min_info.t_speed  = 0;
        
        temp_min = p.tm_min;
        min_info.m_flag = 1;
        min_info.m_count = 0;
        min_info.m_speed = 0;

        if(ret > 0)
        {
            if(tmpspd !=0)
            {
                min_info.m_speed = min_info.m_speed + tmpspd*p.tm_sec ; 
                min_info.m_count = min_info.m_count + p.tm_sec;
            }
            
            tempmileage += ret*tmpdist; 
            picturedistance += ret*tmpdist;
        }

    }
    else
    { //p.tm_min == temp_min
        if(ret > 0)
        {
            if(tmpspd !=0)
            {
                min_info.m_speed = min_info.m_speed + tmpspd*ret ;
                min_info.m_count = min_info.m_count + ret;
            }
            
            buf_08[6+2*(p.tm_sec-1)] = tmpspd;
            buf_08[6+2*(p.tm_sec-1)+1] = speed_distance.signal;
            tempmileage += ret*tmpdist; 
            picturedistance += ret*tmpdist;
        }
    }
    min_info.m_time = itime;

    if(speed == 0)
        speed_zero_flag = 1;
    else
        speed_zero_flag = 0;

    speed = speed*1.0/len + 0.8; 
    distance = speed/3.6;

    if(min_info.m_flag == 1)
    {
        //printf("--time=%d, buf08[0]=%02x..speed=%d\n",itime, buf_08[0],min_info.t_speed);
        if(buf_08[0] == 0xFF)
        {
            buf_08[0]= VALUE_TO_BCD(p.tm_year - 100);
            buf_08[1]= VALUE_TO_BCD(p.tm_mon + 1);
            int imin=0, ihour=0, iday=0;
            iday = p.tm_mday;
            ihour = p.tm_hour;
            imin = p.tm_min;
            
            if(imin == 0)
            {
                if(ihour == 0)
                {
                    iday--;
                    ihour = 23;
                }
                else
                    ihour--;
                imin = 59;
            }
            else
                imin--;
            
            buf_08[2]= VALUE_TO_BCD(iday);
            buf_08[3]= VALUE_TO_BCD(ihour);
            buf_08[4]= VALUE_TO_BCD(imin);
            buf_08[5]= 0;
        }
        
        if(min_info.t_speed != 0)
            write_08H_vdr_file();
        
        memset(buf_08, 0xFF, 126);
        buf_08[0]= VALUE_TO_BCD(p.tm_year - 100);
        buf_08[1]= VALUE_TO_BCD(p.tm_mon + 1);
        buf_08[2]= VALUE_TO_BCD(p.tm_mday);
        buf_08[3]= VALUE_TO_BCD(p.tm_hour);
        buf_08[4]= VALUE_TO_BCD(p.tm_min);
        buf_08[5]= 0;
    }
    
    buf_08[6+2*p.tm_sec] = speed;
    buf_08[6+2*p.tm_sec+1] = tmp_status;

    speed_distance.index++;
    if(speed_distance.index == 60)
        speed_distance.index = 0;

    if(speed_distance.signal != tmp_status)
    {
        tmp_status_flag = 1;
        speed_distance.signal = tmp_status;
    }
    
    min_info.m_status |= tmp_status;
    speed_distance.sec_speed[speed_distance.index]=speed;        // km/h
    speed_distance.sec_distance[speed_distance.index]=distance;  //m

    if(speed !=0)
    {
        min_info.m_speed = min_info.m_speed + speed ; 
        min_info.m_count++;
    }
        
    tempmileage += distance; 
    picturedistance += distance;

    unsigned char mbuf[5];
    memset(mbuf, 0, 5);
    I2c_read_total_mileage_data(mbuf, 5);
    int imileage = 0;  //m
    imileage = ((mbuf[3]<<8)|mbuf[4])*1000/(vdr_current_info.speed_factor*vdr_current_info.lap_factor);
    vdr_current_info.total_mileage = ((mbuf[0]<<16)|(mbuf[1]<<8)|(mbuf[2]))*1000 + imileage;
    
    //printf("---0x%02x,0x%02x,0x%02x,0x%02x,0x%02x-------\n ",mbuf[0],mbuf[1],mbuf[2],mbuf[3],mbuf[4]);
    //printf("====%d====total_mileage=%d--%d--%02x,%02x----\n",speed,vdr_current_info.total_mileage,speed,mbuf[3],mbuf[4]);
    
    if(!(vdr_status_flag & LOCSTATUS_ACC_ONO)&&(vdr_current_info.illegal_displacement_flag == 0))
    {
        vdr_current_info.illegal_displacement += distance;
        if((int)vdr_current_info.illegal_displacement > vdr_param.fence_radius)
        {
            change_alarm_flag_and_upload(ALARM_STATUS_CAR_ILLEGAL_OFFSET, 1); 
            vdr_current_info.illegal_displacement_flag = 1;
        }
    }
    else
        vdr_current_info.illegal_displacement = 0;
    
    if(vdr_param.capture_control.distance_capture_switch !=0)
    {
        if((int)picturedistance >= vdr_param.capture_control.distance_interval)
        {
            timername = "picdistance"; 
            take_picture_control_handler(timername, (void *)PICTURE_DISTANCE_DATA, NULL);
            picturedistance = 0;
        }
    }
    else
        picturedistance = 0;
    
    return speed;
}  
void process_driver_sleep_begin(int itime)
{
    printf("---process_driver_sleep_begin===time=%d==\n",itime);
    if(gps_info.position == 0)
    {
        pretired_param.StopLongitude=0x7FFFFFFF;
        pretired_param.StopLatitude=0x7FFFFFFF;
        pretired_param.StopAltitude=0x7FFF;
    }
    else
    {
        pretired_param.StopLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
        pretired_param.StopLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
        pretired_param.StopAltitude=gps_info.ialtitude;
    }
    pretired_param.StoppingTime=itime-1;
    PreTired_Write();//refresh datebase
    if(true == pretired_param.Flag_TiredDrive)
    {
        TiredDriving_Write(1);
    }

    if(vdr_current_info.sleep_timer_flag==1)
    {
        StopTimer(sleeptimer);
    }
    sleeptimer->timeout = (vdr_param.min_resting_time-1)*1000;
    StartTimer(sleeptimer);
    vdr_current_info.sleep_timer_flag = 1;
    
    total_time_tmp = pretired_param.TotalTime;
}

void process_driver_tired_begin(int itime)
{
    printf("---process_driver_tired_begin===time=%d==runtime=%d,\n",itime,pretired_param.RunningTime);
    if(vdr_current_info.sleep_timer_flag==1)
    {
        StopTimer(sleeptimer);
        vdr_current_info.sleep_timer_flag = 0;
    }

    if(pretired_param.RunningTime == 0)
    {   //第一次未疲劳开车
        /*记录下开车时间*/
        if(gps_info.position == 0)
        {
            pretired_param.TiredLongitude=0x7FFFFFFF;
            pretired_param.TiredLatitude=0x7FFFFFFF;
            pretired_param.TiredAltitude=0x7FFF;
            pretired_param.StopLongitude=0x7FFFFFFF;
            pretired_param.StopLatitude=0x7FFFFFFF;
            pretired_param.StopAltitude=0x7FFF;
        }
        else
        {
            pretired_param.TiredLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
            pretired_param.TiredLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
            pretired_param.TiredAltitude=gps_info.ialtitude;
            pretired_param.StopLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
            pretired_param.StopLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
            pretired_param.StopAltitude=gps_info.ialtitude;
        }
        
        pretired_param.StoppingTime=itime;
        pretired_param.TiredTime= itime;
        pretired_param.Flag_TiredDrive= false;//标志未疲劳驾驶
        pretired_param.RunningTime=0;
        once_alarm=0;
        total_time_start = itime;
    }
    else
    {   //再次开车
        /*计算停车时间*/
        if((itime - pretired_param.StoppingTime) >= vdr_param.min_resting_time) //默认20minute = 1200second
        {   //休息大于20分钟
            if(true == pretired_param.Flag_TiredDrive)
            {
                TiredDriving_Write(1);
                change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 0);
                change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 0);
                flag_prefatigue = 0;
            }
            /*重新记录开车时间*/ 
            if(gps_info.position == 0)
            {
                pretired_param.TiredLongitude=0x7FFFFFFF;
                pretired_param.TiredLatitude=0x7FFFFFFF;
                pretired_param.TiredAltitude=0x7FFF;
                pretired_param.StopLongitude=0x7FFFFFFF;
                pretired_param.StopLatitude=0x7FFFFFFF;
                pretired_param.StopAltitude=0x7FFF;
            }
            else
            {
                pretired_param.TiredLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
                pretired_param.TiredLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
                pretired_param.TiredAltitude=gps_info.ialtitude;
                pretired_param.StopLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
                pretired_param.StopLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
                pretired_param.StopAltitude=gps_info.ialtitude;
            }
            
            pretired_param.StoppingTime=itime;
            pretired_param.TiredTime= itime;
            pretired_param.Flag_TiredDrive= false;//标志未疲劳驾驶
            pretired_param.RunningTime = 0;
            once_alarm=0;
            total_time_start = itime;
        }
        else
        {//休息不够20分钟
            /*将停车时间也累加到行车时间里*/
            total_time_start = pretired_param.StoppingTime;
            pretired_param.RunningTime = itime -pretired_param.TiredTime;
            pretired_param.TotalTime = itime - total_time_start + total_time_tmp;
            if(gps_info.position == 0)
            {
                pretired_param.StopLongitude=0x7FFFFFFF;
                pretired_param.StopLatitude=0x7FFFFFFF;
                pretired_param.StopAltitude=0x7FFF;
            }
            else
            {
                pretired_param.StopLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
                pretired_param.StopLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
                pretired_param.StopAltitude=gps_info.ialtitude;
            }
            pretired_param.StoppingTime=itime;
            
            if(vdr_param.continue_drive_threshold> 0)//连续驾驶时间(分钟)，0表示不启用连续驾驶超时告警
            {//启用连续驾驶超时
                if(pretired_param.RunningTime>=(vdr_param.continue_drive_threshold-vdr_param.tired_drive_warning) && flag_prefatigue==0 &&pretired_param.Flag_TiredDrive==false)
                {
                    flag_prefatigue=1;
                    change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 1);
                }
                if((pretired_param.TotalTime>=vdr_param.day_drive_time_threshold) && flag_total_time==0)
                {
                    flag_total_time=1;
                    change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,1);
                }

                if(pretired_param.RunningTime> vdr_param.continue_drive_threshold)
                {
                    if(pretired_param.Flag_TiredDrive==false)
                    {
                        pretired_param.Flag_TiredDrive=true;
                        change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING,0);
                        change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 1);
                    }
                    TiredDriving_Write(1);
                }
                int tired_tmp=(pretired_param.RunningTime -(vdr_param.continue_drive_threshold-1801))/300;
                once_alarm=(tired_tmp>0?(tired_tmp>=12?12:tired_tmp):0);
            }
        }
    }
    StopTimer(sleeptimer);
    PreTired_Write();//refresh datebase
}
void process_driver_tired_info(int itime)
{
    int tiredi=0;    

    pretired_param.RunningTime = itime-pretired_param.TiredTime;
    pretired_param.TotalTime = itime - total_time_start + total_time_tmp;

    if(gps_info.position == 0)
    {
        pretired_param.StopLongitude=0x7FFFFFFF;
        pretired_param.StopLatitude=0x7FFFFFFF;
        pretired_param.StopAltitude=0x7FFF;
    }
    else
    {
        pretired_param.StopLongitude=get_signed_longitude_latitude(gps_info.ilongitude,gps_info.fX);
        pretired_param.StopLatitude=get_signed_longitude_latitude(gps_info.ilatitude,gps_info.fY);
        pretired_param.StopAltitude=gps_info.ialtitude;
    }
    pretired_param.StoppingTime=itime;
    
    printf("-----::%d ,%d,%d, %d,%d,%d,%d \n",pretired_param.TiredTime,itime,pretired_param.StoppingTime,
        pretired_param.RunningTime,pretired_param.TotalTime,flag_total_time, once_alarm);

	if(pretired_param.RunningTime>=(vdr_param.continue_drive_threshold-vdr_param.tired_drive_warning) && flag_prefatigue==0 &&pretired_param.Flag_TiredDrive==false)
    {
        flag_prefatigue=1;
        change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING, 1);
        PreTired_Write();//refresh datebase
    }
    if((pretired_param.TotalTime>=vdr_param.day_drive_time_threshold) && flag_total_time==0)
    {
        flag_total_time=1;
        change_alarm_flag_and_upload(ALARM_STATUS_DAY_DRIVING_OVERTIME,1);
        PreTired_Write();//refresh datebase
    }

    if((pretired_param.RunningTime < (vdr_param.continue_drive_threshold+1801))||(once_alarm==12))
    {
        for(tiredi=0;pretired_param.RunningTime > (vdr_param.continue_drive_threshold-1801+tiredi*300);tiredi++)
        {
            if(once_alarm==tiredi)
            {
                once_alarm++;
                if(once_alarm>12)
                    tirednotify->flag=2;
                else if(once_alarm>6)
                    tirednotify->flag=1;
                else 
                    tirednotify->flag=0;
                tirednotify->dura=pretired_param.RunningTime;
                memcpy(tirednotify->driverid,pretired_param.VehicleDriverID,18);
                dev_DbusServer->sendNotify(tirednotify);
            
                if((once_alarm>=7)&& (pretired_param.Flag_TiredDrive==false))
                {
                    pretired_param.Flag_TiredDrive= true;//标志为疲劳驾驶
                	change_alarm_flag_and_upload(ALARM_STATUS_PREFATIGUE_DRIVING,0);
                	change_alarm_flag_and_upload(ALARM_STATUS_FATIGUE_DRIVING, 1);
                    TiredDriving_Write(0);
                    PreTired_Write();
                }
                break;
            }
        }

       if(once_alarm==13)
            once_alarm=0;
    }

    num_pretired_w ++;
    if(num_pretired_w == 60)
    {
        PreTired_Write();//refresh datebase
        num_pretired_w = 0;
    }

}
void process_speed_signal_info(int speed, int itime)
{
    string timername;
    
    if(speed == 0)
    {
	    speed_zero=1;

	    if(!vdr_current_info.stop_timer_flag)
	    {
		    vdr_current_info.stop_timer_flag = 1;
            speed_zero_count = 1;
            
		    memcpy(&loc_info, &gps_info, sizeof(struct NaviGPSInfo));
		    pretired_param_temp.StoppingTime=itime;
            
            if(proc_tired_flag == 1)
            {
                process_driver_sleep_begin(itime);
                proc_tired_flag = 0;
            }
	    }
        else
        {
            speed_zero_count++;
            if(speed_zero_count == 11)
            {
                timername = "stoprun"; 
                car_start_stop_run_handler(timername, (void *)STOP_TIMER_DATA, NULL);
            }
        }
       
	    if(vdr_current_info.start_timer_flag)
	    {
		    vdr_current_info.start_timer_flag = 0;
            speed_data_count = 0;
	    }

        if(vdr_current_info.speed_flag==1)
            vdr_current_info.speed_flag=0;
    }
    else
    {
	    speed_zero=0;

        if(vdr_current_info.speed_flag==0)
            vdr_current_info.speed_flag=1;
        
	    if(vdr_current_info.stop_timer_flag)
	    {
		    vdr_current_info.stop_timer_flag = 0;
            speed_zero_count = 0;
	    }

	    if(!vdr_current_info.start_timer_flag)
	    {
		    vdr_current_info.start_timer_flag = 1;
            speed_data_count = 1;
	    }
        else
        {
            speed_data_count++;
            if(speed_data_count == 11)
            {
                timername = "startrun"; 
                car_start_stop_run_handler(timername, (void *)START_TIMER_DATA, NULL);
            }
        }
        if(proc_tired_flag == 0)
        {
            process_driver_tired_begin(itime);
            proc_tired_flag = 1;
        }
        
        if(proc_tired_flag == 1)
            process_driver_tired_info(itime);
        
    }
    check_over_speed_warning_alarm(speed);
}

int write_rtc(int time)
{
	if(time < 0)
	{
		printf("time < 0\n");
		return -1;
	}

	int	fd=open("/dev/rtc",O_RDONLY);	
	if(fd<0)
	{
		printf("error open /dev/rtc\n");
		return -1;
	}

	printf("time is %d\n", time);
	if(ioctl(fd, RTC_WKALM_SET, &time) < 0)
	{
		printf("error RTC_WKALM_SET ioctl\n");
		return -1;
	}

	return 0;
}

void process_acc_off_info(int flag, int itime)
{
    int first=0;

    if(min_info.m_speed != 0)
        write_08H_vdr_file();

    vdr_current_info.acc_off_time = itime; 
    vdr_current_info.running_flag=0;
    
    if(min_info.m_speed > 0)
        write_track_locstatus_table(3, itime/60*60, &min_info.m_gps);
    
    write_vdr_off_status_table(0);     

    DestroyTimer(sleeptimer);
    DestroyTimer(parktimer);
    DestroyTimer(speedtimer);
    DestroyTimer(linetimer);
    DestroyTimer(uploadtimer);
    DestroyTimer(mcutimer);  
    DestroyTimer(picturetimer);

    workq_pretired.quit = 1;
    workq_tired.quit = 1;
    workq_runst.quit = 1;
    workq_locst.quit = 1;
    workq_accident.quit = 1;
    workq_dayinfo.quit = 1;
    
    int count = 0;
    while(1)
    {
        if(count > 40)
            break;
        
        if((workq_runst.counter == 0) && (first == 0))
        {
            if(!(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF))
            {
                write_track_accident_table(2,gps_info);
            }
            first = 1;
        }
        
        if((workq_pretired.counter == 0) &&(workq_tired.counter == 0) && (workq_runst.counter == 0) 
            &&(workq_locst.counter == 0) &&(workq_accident.counter == 0) && (workq_dayinfo.counter == 0))
            break;
        usleep(100000);
        count++;
    }

    system("sync");
    printf("shutdown ..\n");
    #ifdef SLEEP_UP_POSITION
    if(vdr_current_info.is_time_report)
    {
        if(vdr_param.sleep_report_interval > 110)
            write_rtc(vdr_param.sleep_report_interval-60);
    }
    #endif
    /*
    if(flag == SHUTDOWN_FLAG)
    {
        system("accoff.sh 1 &");
    }
    else if(flag == REBOOT_FLAG)
    {
        system("accoff.sh 2 &");
    }
    else if(flag == 8)
    {
        system("accoff.sh 4 &");
    }
    else
    {
        system("accoff.sh 3 &");
    }
    */
    system("sync");
    fclose(fp_08);
    fclose(fp_09);
    fclose(fp_loc);
    fclose(fp_run);
    close(speed_fd);
    close(cst_fd);
    close(sensor_fd);
    close(socket_dp);
    close(socket_recvfd);
    exit(0);
}
void check_gps_position_to_accident(struct NaviGPSInfo *gpsinfo)
{
    int x=0, y=0;

    if((gps_position_count > 10) && !(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF))
        return ;

    if(gpsinfo->position && gpsinfo->fX!=0 && !(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF))
    {
        x = abs(gpsinfo->fX - norm_info.fX);
        y = abs(gpsinfo->fY - norm_info.fY);
        if(x<200 && y<500)
        {
            gps_position_count++;
        }
        else
        {
            memcpy(&norm_info, gpsinfo, sizeof(struct NaviGPSInfo));
            gps_position_count = 0;
        }
    }
    else
    {
        gps_position_count = 0;
        memset(&norm_info,0,sizeof(struct NaviGPSInfo));
    }
}

void process_gps_position_info(int itime)
{
    struct NaviGPSInfo tmp_gps;
    int vdr_speed, gps_speed;
    
    memcpy(&tmp_gps, &gps_info, sizeof(struct NaviGPSInfo));
    
    check_gps_position_to_accident(&tmp_gps);

    if(!tmp_gps.position && (vdr_status_flag & LOCSTATUS_FIXED_POSITION))
    {
        in_position_count = 0;
        un_position_count++;
        //if(un_position_count > 3)
        {
            VDRBITCLR(vdr_status_flag, LOCSTATUS_FIXED_POSITION);
            tmp_status_flag = 1;
            un_position_count = 0;
            //first_set_time_flag = 0;
        }
    }
    else if(tmp_gps.position && !(vdr_status_flag & LOCSTATUS_FIXED_POSITION))
    {
        un_position_count = 0;
        in_position_count++;
        //if(in_position_count > 2)
        {
            VDRBITSET(vdr_status_flag, LOCSTATUS_FIXED_POSITION);
            tmp_status_flag = 1;
            in_position_count = 0;
            if(first_set_time_flag == 0 && tmp_gps.fX != 0 && tmp_gps.fY != 0)
            {
                if(vdr_current_info.change_time_flag == 0)
                {
                    struct timeval t;
                if(hx_info_num == 0)
                    t.tv_sec = tmp_gps.iTime = hx_info[19].iTime;
                else
                    t.tv_sec = tmp_gps.iTime = hx_info[hx_info_num-1].iTime;
                    t.tv_usec=0;
                    int ret = abs(tmp_gps.iTime-itime);
                    if( (ret > 30) && (vdr_current_info.is_time_report==1))
                    {
                        StopTimer(uploadtimer);
                    }
                    settimeofday(&t,NULL);
                    system("hwclock -w");
                    itime = tmp_gps.iTime;
                    printf("---dev set time ok---%d--%d\n",tmp_gps.iTime,itime);
                    if( (ret > 30) && (vdr_current_info.is_time_report ==1))
                    {
                        uploadtimer->timeout = vdr_current_info.report_time*1000;
                        StartTimer(uploadtimer);
                    }
                    first_set_time_flag = 1;
                    check_time_flag = 1;
                    hx_info_flag = hx_info_num;
                }
            }
        }
    }else if(tmp_gps.position && (vdr_status_flag & LOCSTATUS_FIXED_POSITION))
    {
        un_position_count = 0;
        in_position_count = 0;
    }
    
    if(tmp_gps.ilatitude && !(vdr_status_flag & LOCSTATUS_EAST_WEST))
    {
        VDRBITSET(vdr_status_flag, LOCSTATUS_EAST_WEST);
        tmp_status_flag = 1;
    }
    else if(!tmp_gps.ilatitude && (vdr_status_flag & LOCSTATUS_EAST_WEST))
    {
        VDRBITCLR(vdr_status_flag, LOCSTATUS_EAST_WEST);
        tmp_status_flag = 1;
    }
    
    if(tmp_gps.ilongitude && !(vdr_status_flag & LOCSTATUS_NORTH_SOUTH))
    {
        VDRBITSET(vdr_status_flag, LOCSTATUS_NORTH_SOUTH);
        tmp_status_flag = 1;
    }
    else if(!tmp_gps.ilongitude && (vdr_status_flag & LOCSTATUS_NORTH_SOUTH))
    {
        VDRBITCLR(vdr_status_flag, LOCSTATUS_NORTH_SOUTH);
        tmp_status_flag = 1;
    }

    if(tmp_gps.mode == 1)
    {
        add_info.gnss_satellite_sum = tmp_gps.gpsnum;
        if(!(vdr_status_flag & LOCSTATUS_GNSS_GPS_MODE))
        {
            VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
            tmp_status_flag = 1;
        }
        if(vdr_status_flag & LOCSTATUS_GNSS_BD_MODE )
        {
            VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
            tmp_status_flag = 1;
        }
    }
    else if(tmp_gps.mode == 2)
    {
        add_info.gnss_satellite_sum = tmp_gps.bdnum;
        if(vdr_status_flag & LOCSTATUS_GNSS_GPS_MODE)
        {
            VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
            tmp_status_flag = 1;
        }
        if(!(vdr_status_flag & LOCSTATUS_GNSS_BD_MODE) )
        {
            VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
            tmp_status_flag = 1;
        }
    }    
    else if(tmp_gps.mode == 3)
    {
        add_info.gnss_satellite_sum = tmp_gps.gpsnum + tmp_gps.bdnum;
        if(!(vdr_status_flag & LOCSTATUS_GNSS_GPS_MODE))
        {
            VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
            tmp_status_flag = 1;
        }
        if(!(vdr_status_flag & LOCSTATUS_GNSS_BD_MODE) )
        {
            VDRBITSET(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
            tmp_status_flag = 1;
        }
    }
    else
    {
        add_info.gnss_satellite_sum = 0;
        if(vdr_status_flag & LOCSTATUS_GNSS_GPS_MODE)
        {
            VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_GPS_MODE);
        }
        if(vdr_status_flag & LOCSTATUS_GNSS_BD_MODE )
        {
            VDRBITCLR(vdr_status_flag, LOCSTATUS_GNSS_BD_MODE);
        }
    }
    
    gps_speed = tmp_gps.iVelocity;
    vdr_speed = speed_distance.sec_speed[speed_distance.index];
    check_vss_alarm_info(vdr_speed,gps_speed);

    write_track_locstatus_table(1, itime, &tmp_gps);

    if((min_info.m_position == 0) && (tmp_gps.position == 1))
    {
        memcpy(&min_info.m_gps, &tmp_gps, sizeof(struct NaviGPSInfo));
        min_info.m_position = 1;
    }
    
    if(min_info.m_flag)
    {
        //if((itime-60) >= vdr_current_info.acc_on_time)
        write_track_locstatus_table(2, itime-60, &min_info.m_gps);
        printf("----time=%d,\n",itime-60);
        prepare_09H_buf_data(itime-60, &min_info.m_gps);
        min_info.m_position = 0;
        min_info.m_flag = 0;
        min_info.m_status = 0;
        memset(&min_info.m_gps, 0, sizeof(struct NaviGPSInfo));
    } 
    
    write_vdr_day_info_table(itime);
    check_speed_status_info(vdr_speed, gps_speed, itime);

}
void process_alarm_status_info(unsigned char alarmstatus)
{
    int alarmnum=0;
    int flag = 0;
    
    if(vdr_param.warning_mask&ALARM_STATUS_EMERGENCY_ALARM)
        return;

    flag = vdr_current_info.emergency_alarm_flag;
        
    for(int i=0; i<8; i++)
    {
        if((alarmstatus<<i) & Hw7)
        {
            if(flag == 0)
            {
                alarmnum ++;
                flag = 1;
            }
        }
        else
        {
            flag = 0;
        }

    }

    if(alarmnum > 0)
    {
        if(vdr_current_info.report_mode != ALARM_MODE)
            change_report_time_distance_mode(ALARM_MODE);
        change_alarm_flag_and_upload(ALARM_STATUS_EMERGENCY_ALARM, 1);
        vdr_current_info.emergency_alarm_num += alarmnum;            
    }
    
    vdr_current_info.emergency_alarm_flag = flag;
        
}
void process_acc_status_info(unsigned char *accstatus)
{
    DeviceACCInfoDbusNotifyC accinfo;
    int d310flag = 0;

    if((accstatus[0]&Hw0) && !(vdr_status_flag & LOCSTATUS_ACC_ONO))
    {
        if(startup_mode != 0x40 && startup_mode != 0x80 && startup_mode != 0xC0)
            set_lcd_enable_disable(1);
        VDRBITSET(vdr_status_flag, LOCSTATUS_ACC_ONO);
        tmp_status_flag = 1;
        vdr_current_info.illegal_displacement_flag = 0;
        system("caraction unmutegf");
        accinfo.flag = 1;
        dev_DbusServer->sendNotify(&accinfo);
    }
    else if(!(accstatus[0]&Hw0) && (vdr_status_flag & LOCSTATUS_ACC_ONO))
    {
        VDRBITCLR(vdr_status_flag, LOCSTATUS_ACC_ONO);
        tmp_status_flag = 1;
        vdr_current_info.illegal_displacement = 0;
        set_lcd_enable_disable(0);
        system("caraction mutegf");
        accinfo.flag = 0;
        dev_DbusServer->sendNotify(&accinfo);
        shutdown_reboot_flag = 1;
    }
    
    if((accstatus[0]&Hw4) && (vdr_current_info.car_smoke_flag == 0))
    {   
        vdr_current_info.car_smoke_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_SMOKE_ALARM, 1);
        send_device_alarm_notify(DEV_ALARM_SMOKE_ALARM, 1);
    }
    else if(!(accstatus[0]&Hw4) && (vdr_current_info.car_smoke_flag == 1))
    {
        vdr_current_info.car_smoke_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_SMOKE_ALARM, 0);
        send_device_alarm_notify(DEV_ALARM_SMOKE_ALARM, 0);
    }
    
    if((accstatus[0]&Hw5) && (vdr_current_info.undervoltage_flag == 0))
    {  
        vdr_current_info.undervoltage_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_POWER_UNDER_VOLTAGE, 1);
    }
    else if(!(accstatus[0]&Hw5) && (vdr_current_info.undervoltage_flag == 1))
    {
        vdr_current_info.undervoltage_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_POWER_UNDER_VOLTAGE, 0);
    }
    
    if((accstatus[0]&Hw6) && (vdr_current_info.illegal_power_flag == 0))
    {  
        vdr_current_info.illegal_power_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_POWER_DOWN_ELECTRICITY, 1);
        VDRBITSET(vdr_status_flag, LOCSTATUS_CIRCUIT_OFF);
    }
    else if(!(accstatus[0]&Hw6) && (vdr_current_info.illegal_power_flag == 1))
    {
        vdr_current_info.illegal_power_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_POWER_DOWN_ELECTRICITY, 0);
        VDRBITCLR(vdr_status_flag, LOCSTATUS_CIRCUIT_OFF);
    }

    if((accstatus[0]&Hw3) && !(vdr_current_info.d310_can_ready_flag&Hw0))
    {        
        vdr_current_info.d310_can_ready_flag |= Hw0;  
        d310flag |= Hw0;
    }
    else if(!(accstatus[0]&Hw3))
    {
        vdr_current_info.d310_can_ready_flag &= 0xFE;   
    }
    
    if((accstatus[0]&Hw7) && !(vdr_current_info.d310_can_ready_flag&Hw1))
    {
        vdr_current_info.d310_can_ready_flag |= Hw1;
        d310flag |= Hw1;
    }
    else if(!(accstatus[0]&Hw7))
    {
        vdr_current_info.d310_can_ready_flag &= 0xFD;   
    }
    
    if((accstatus[1]&Hw7) && !(vdr_current_info.d310_can_ready_flag&Hw2))
    {
        vdr_current_info.d310_can_ready_flag |= Hw2;
        d310flag |= Hw2;
    }
    else if(!(accstatus[1]&Hw7))
    {
        vdr_current_info.d310_can_ready_flag &= 0xFB;  
    }

    if(d310flag)
        read_d310_can_info_thread(d310flag);

}

int  process_D310_dreg_IC_info(char *buf)
{
    string driverid;
    int result = 0;
    
    VDRDregICTable *table = new VDRDregICTable();
    int ret = table->RefreshTop();
    while(ret != -1)
    {
        table->GetStringValue(VDR_DREG_IC_LICENSE_NUM, driverid);
        if(memcmp(buf, driverid.c_str(),18)==0)
        {
            I2c_write_CM_Control_Module_data(0x03);
            dreg_info.wait_reply_flag = 1;
            dreg_info.command_type = 3;
            result = 1;
            break;
        }
        ret = table->RefreshNext();
    }
  
    delete table;
    return result;
}

void send_common_event_notfiy_dbus(int type, int value)
{    
    DevCommonEventNotifyC notify;
    notify.type = type;
    notify.value = value;
    dev_DbusServer->sendNotify(&notify);
}
void process_D310_CM_reply_info(int type)
{
    unsigned char replybuf[2];

    I2c_read_CM_reply_info_data(replybuf, 2);
    printf("===== mcu reply: type=%d %02x, %02x\n",type, replybuf[0],replybuf[1]);
    switch(type)
    {
        case 1:
            //激活
            if(replybuf[0] == 1)
            {
                dreg_info.wait_reply_flag = 0;
                dreg_info.active_disable = 1;
                VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW29);
                VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW30);
                tmp_status_flag = 9;    // 0x8500 应答0x0500
                write_d310_dreg_info_table();
                send_common_event_notfiy_dbus(1,0);
            }
            if(replybuf[0] == 3)
            {// 通讯失败
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW29);
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW30);
                tmp_status_flag = 9;  
                dreg_info.wait_reply_flag = 0;
            }
            break;
        case 2:
            //解锁
            if(replybuf[0] == 2)
            {
                dreg_info.wait_reply_flag = 0;
                dreg_info.active_disable = 0;
                VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW29);
                VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW30);
                tmp_status_flag = 9;    // 0x8500 应答0x0500
                write_d310_dreg_info_table();
                send_common_event_notfiy_dbus(2,0);
            }
            if(replybuf[0] == 3)
            {// 通讯失败
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW29);
                VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW30);
                tmp_status_flag = 9;
                dreg_info.wait_reply_flag = 0;
            }
            break;
        case 3:
            //启动
            if(replybuf[0] == 4)
            {
                dreg_info.engine_status = 1;
                dreg_info.wait_reply_flag = 0;

                {   // D310 渣土车举升控制
                    dreg_info.lift_status = 0;
                    dreg_info.wait_reply_flag = 1;
                    dreg_info.command_type = 5;
                    I2c_write_CM_Control_Module_data(0x06);
                }
            }
            if(replybuf[0] == 5)
            {// 启动控制失败
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
                VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
                tmp_status_flag = 1;
                dreg_info.engine_status = 0;
                dreg_info.wait_reply_flag = 0;
            }
            break;
        case 4:
            // 限速
            if(replybuf[0] == 6)
            {
                dreg_info.wait_reply_flag = 0;
                if(replybuf[1] == 0x09)
                {// 无限速
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
                    dreg_info.speed_status = 0;
                }
                else
                {// 平台限速
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
                    VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
                    dreg_info.speed_status = 1;
                }                

                write_d310_dreg_info_table();
            }
            break;
        case 5:
            //举升控制
            if(replybuf[0] == 7)
            {
                dreg_info.wait_reply_flag = 0;
            }
            break;
        default:
            break;
    }
    
}

void process_D310_CM_sensor_info()
{
    unsigned char sensorbuf[2];
    unsigned char tmp = -1;

    I2c_read_CM_sensor_info_data(sensorbuf, 2);
    printf("===== mcu sensor: %02x, %02x\n",sensorbuf[0],sensorbuf[1]);

    tmp = sensorbuf[0] & 0x0F;
    if((tmp == 1) && sensor_info.cap_onoff_flag == 0)
    {   //开
        sensor_info.cap_onoff_flag = 1;
        VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_CAP_ONOFF);
        tmp_status_flag = 1;
    }
    if((tmp == 4) && sensor_info.cap_onoff_flag == 1)
    {   //关
        sensor_info.cap_onoff_flag = 0;
        VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_CAP_ONOFF);
        tmp_status_flag = 1;
    }
    if(((tmp == 0) || (tmp == 5)) && sensor_info.cap_sensor_flag == 0)
    {   //环保盖传感器故障
        sensor_info.cap_sensor_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_CAP_FAILURE, 1);
    }
    if(((tmp == 1) || (tmp == 4)) && sensor_info.cap_sensor_flag == 1)
    {   //环保盖传感器故障解除
        sensor_info.cap_sensor_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_CAP_FAILURE, 0);
    }
    
    tmp = (sensorbuf[0]>>4) & 0x0F;
    if((tmp == 1) && sensor_info.lift_updown_flag == 0)
    {   //举升
        sensor_info.lift_updown_flag = 1;
        VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_LIFT_UPDOWN);
        tmp_status_flag = 1;
    }
    if((tmp == 4) && sensor_info.lift_updown_flag == 1)
    {   //落下
        sensor_info.lift_updown_flag = 0;
        VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_LIFT_UPDOWN);
        tmp_status_flag = 1;
    }
    if(((tmp == 0) || (tmp == 5)) && sensor_info.lift_sensor_flag == 0)
    {   //举升传感器故障
        sensor_info.lift_sensor_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_LIFT_FAILURE, 1);
    }
    if(((tmp == 1) || (tmp == 4)) && sensor_info.lift_sensor_flag == 1)
    {   //举升传感器故障解除
        sensor_info.lift_sensor_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_LIFT_FAILURE, 0);
    }

    if(sensor_info.lift_updown_flag==1 && dreg_info.lift_status==0 && sensor_info.lift_alarm_flag==0)
    {   // 不在规定区域卸渣土报警
        sensor_info.lift_alarm_flag = 1;
       
    }
    if(sensor_info.lift_updown_flag==0 && dreg_info.lift_status==0 && sensor_info.lift_alarm_flag==1)
    {   // 不在规定区域卸渣土报警解除
        sensor_info.lift_alarm_flag = 0;
       
    }

    tmp = sensorbuf[1] & 0x0F;
    if((tmp == 1) && sensor_info.carry_status_flag == 1)
    {   //空载
        vdr_current_info.carry_status = 0;
        sensor_info.carry_status_flag = 0;
        VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
        VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
        tmp_status_flag = 1;
        VDRBaseConfigTable *table = new VDRBaseConfigTable();
        table->SetIntValue(VDR_BASE_CONFIG_D2, vdr_current_info.carry_status);
        table->Commit();
        delete table;
    }
    if((tmp == 4) && sensor_info.carry_status_flag == 0)
    {   //满载
        vdr_current_info.carry_status = 3;
        sensor_info.carry_status_flag = 1;
        VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
        VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
        tmp_status_flag = 1;
        VDRBaseConfigTable *table = new VDRBaseConfigTable();
        table->SetIntValue(VDR_BASE_CONFIG_D2, vdr_current_info.carry_status);
        table->Commit();
        delete table;
    }
    if(((tmp == 0) || (tmp == 5)) && sensor_info.carry_sensor_flag == 0)
    {   //空、重载传感器故障
        sensor_info.carry_sensor_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_CARRY_FAILURE, 1);
    }
    if(((tmp == 1) || (tmp == 4)) && sensor_info.carry_sensor_flag == 1)
    {   //空、重载传感器故障解除
        sensor_info.carry_sensor_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_D310_CM_CARRY_FAILURE, 0);
    }

    if(sensor_info.carry_status_flag==1 && sensor_info.cap_onoff_flag==1 
        && vdr_current_info.vdr_sec_speed>5 && sensor_info.carry_alarm_flag == 0)
    {   // 重载盖开 速度>5 报警
        sensor_info.carry_alarm_flag = 1;
    }
    
    if(sensor_info.carry_status_flag==1 && sensor_info.cap_onoff_flag==0 
        && vdr_current_info.vdr_sec_speed>5 && sensor_info.carry_alarm_flag == 1)
    {   // 重载盖关 速度>5 报警 解除
        sensor_info.carry_alarm_flag = 0;
    }

}
void process_D310_CM_no_vecu_info()
{
    unsigned char value;
    
    I2c_read_CM_NO_VECU_data(value);
    printf("===== mcu no vecu: %02x\n",value);

    switch(value)
    {
        case 0x00:  // 无限速
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x01:  // 平台限速
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x02:  // 通讯中断 
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x03:  // 重载 环保盖未关闭
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x04:  // 举升传感器故障
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x05:  // 空重载传感器故障
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x06:  // 环保盖传感器故障
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITSET(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        case 0x07:  // 结束限速
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW24);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW25);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_D310_CM_HW26);
            break;
        default:
            break;
    }

    if(value != dreg_info.speed_status)
    {
        tmp_status_flag = 1;
        dreg_info.speed_status = value;
    }
          
}
void process_collision_side_turn_alarm()
{
    int t_flag=-1, c_flag=-1;

    if(cst_fd < 0)
    {
        sensor_fd = open("/dev/sensor_timer", O_RDONLY);
        if(sensor_fd < 0)
        {
            printf("open sensor_timer device error!\n");
            return ;
        }
        cst_fd = open("/dev/lis3dh", O_RDONLY);
        if(cst_fd < 0)
        {
            printf("open acceleration device error!\n");
            return ;
        }
        
        int sideturn_angle[2], collision_min = 128, tmp_collision_min;
        int collision_time;
        
        VDRAccelerationTable *table = new VDRAccelerationTable();
        table->GetIntValue(VDR_ACCELERATION_OFFSET_ANGLE, sideturn_angle[1]);
        table->GetIntValue(VDR_ACCELERATION_SIDE_TURN_ANGLE, sideturn_angle[0]);
        table->GetIntValue(VDR_ACCELERATION_COLLIDE_MIN, collision_min);//单位0.1g
        table->GetIntValue(VDR_ACCELERATION_COLLIDE_TIME, collision_time);
        delete table;

        if(collision_min%10 < 5)
            tmp_collision_min = collision_min /10;
        else
            tmp_collision_min = collision_min /10 + 1;
        
        ioctl(cst_fd, 0x01, &collision_time);  //set 
        ioctl(cst_fd, 0x06, &tmp_collision_min);  //set 
        ioctl(cst_fd, 0x07, sideturn_angle);  //set 
        
    }

    ioctl(cst_fd, 0x00, &t_flag);   // turnover flag
    ioctl(cst_fd, 0x03, &c_flag);   // collision alarm

    if((t_flag == 0) && (vdr_current_info.car_turn_over_flag == 0))  
    {
        vdr_current_info.car_turn_over_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_CAR_TURNOVER_ALARM, 1);
        send_device_alarm_notify(DEV_ALARM_TURNOVER_ALARM, 1);
        //if(vdr_current_info.report_mode != ALARM_MODE)
          //  change_report_time_distance_mode(ALARM_MODE);
    }

    if((c_flag == 0) && (vdr_current_info.car_collision_flag== 0))  
    {
        vdr_current_info.car_collision_flag = 1;
        change_alarm_flag_and_upload(ALARM_STATUS_CAR_COLLISION_ALARM, 1);
        send_device_alarm_notify(DEV_ALARM_COLLISION_ALARM, 1);
        //if(vdr_current_info.report_mode != ALARM_MODE)
          //  change_report_time_distance_mode(ALARM_MODE);
    }

    if((t_flag != 0) && (vdr_current_info.car_turn_over_flag == 1))  
    {
        vdr_current_info.car_turn_over_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_CAR_TURNOVER_ALARM, 0);
        send_device_alarm_notify(DEV_ALARM_TURNOVER_ALARM, 0);
        //if(vdr_current_info.report_mode == ALARM_MODE)
          //  change_report_time_distance_mode(DEFAULT_MODE);
    }

    if((c_flag != 0) && (vdr_current_info.car_collision_flag== 1))  
    {
        vdr_current_info.car_collision_flag = 0;
        change_alarm_flag_and_upload(ALARM_STATUS_CAR_COLLISION_ALARM, 0);
        send_device_alarm_notify(DEV_ALARM_COLLISION_ALARM, 0);
        //if(vdr_current_info.report_mode == ALARM_MODE)
          //  change_report_time_distance_mode(DEFAULT_MODE);

    }

}

void* read_speed_from_cpu_thread(void* )
{
    unsigned char tmpbuf[50];
    int len= 0, ret= -1, i=0, gps_speed=0, vdr_speed=0;

    while(1)
    {
        memset(tmpbuf, 0, sizeof(tmpbuf));
        while(1)
        {
            ret = read(speed_fd, tmpbuf+len, 10);
            if(ret == 0)
                break;
            len += ret;
        }
        // 1: vdr speed = gps speed;  2: vdr speed gps speed auto switch
        if((len > 0)&&(vdr_current_info.gps_mcu_speed_flag == 1))
        {
            //pthread_mutex_lock(&gps_mutex);
            gps_speed = gps_info.iVelocity;
            //pthread_mutex_unlock(&gps_mutex);
            if(gps_speed < 3)
                gps_speed = 0;
            
            for(i=0;i<len/2; i++)
            {
                vdr_speed += tmpbuf[i*2+1];
                tmpbuf[i*2+1] = gps_speed;
            }
            vdr_current_info.vdr_sec_speed = vdr_speed*2.0/len + 0.8;
        }
        if((len > 0)&&(vdr_current_info.gps_mcu_speed_flag == 2))
        {
            //pthread_mutex_lock(&gps_mutex);
            gps_speed = gps_info.iVelocity;
            //pthread_mutex_unlock(&gps_mutex);
            
            for(i=0;i<len/2; i++)
            {
                vdr_speed += tmpbuf[i*2+1];
            }
            
            if((vdr_speed == 0) && (gps_speed > 10))
            {
                for(i=0;i<len/2; i++)
                {
                    tmpbuf[i*2+1] = gps_speed;
                }
            }
        }
        
        if(len > 0)
        {
            pthread_mutex_lock(&unbuf_mutex);
            memcpy(un_speed_buf+un_speed_len, tmpbuf, len);
            un_speed_len += len;
            pthread_mutex_unlock(&unbuf_mutex);
            
        }
        #if 0
            printf("revlen=%d, unspeedlend=%d, time=%d", len,un_speed_len, time(NULL));
            for(int i=0; i<len; i++)
                printf("%d ", tmpbuf[i]);
            printf("\n");
        #endif
        len = 0;
        usleep(190000); 
    }
}

void get_navi_encrypt_status_handler(string &name, void *data, void *data2)
{
    int result;
    CNaviEncryptInfoReqC request;
    DBusReplyC *reply;
    
    result = sp_ClientGps->sendService(&request, reply, 5*1000);
    if(result < 0 || reply->getIdentifier() != REP_ID_NAVI_ENCRYPTINFO)
	{
		printf("Send Service Error. Error Code: %d\n", result);
        delete reply;
        DestroyTimer(naviencrypttimer);   
        return;
	}
    
    CNaviEncryptInfoRepC *encrypt_reply = (CNaviEncryptInfoRepC *)reply;
	printf("Received Navi Encryptinfo Reply! flag:%d\n",encrypt_reply->flag);
    if(encrypt_reply->flag == 0)
        VDRBITCLR(vdr_status_flag, LOCSTATUS_LONGLATIT_ENCRYPT);
    else if(encrypt_reply->flag == 1)
        VDRBITSET(vdr_status_flag, LOCSTATUS_LONGLATIT_ENCRYPT);

    delete reply;
    DestroyTimer(naviencrypttimer);
}

void read_mcu_data_handler(string &name, void *data, void *data2)
{
    unsigned char mcubuf[512];
    unsigned char accstatus[2];
    unsigned char alarmstatus;
    unsigned char analog_quantity[2];
    unsigned char pulse_sec[4];
    int speednum = 5;
    int tmptime = 0;
    int speed = 0;
    int ret = 0, len = 0, icount = 0;
    struct timeval tv;
    struct timezone tz;
    int i = 0;
    int line_time_flag = 0;

    if(hx_info_begin_flag)
    {
        //pthread_mutex_lock(&gps_mutex);
        if(hx_info_flag == hx_info_num)
        {
            if(hx_info_num == 0)
                hx_info_flag = 19;
            else
                hx_info_flag = hx_info_num-1; 
        }
        memcpy(&gps_info, &hx_info[hx_info_flag], sizeof(struct NaviGPSInfo));
        hx_info_begin_flag = 0;
        //pthread_mutex_unlock(&gps_mutex);
        hx_info_flag++;
        if(hx_info_flag >= 20)
            hx_info_flag = 0;
    }
    else
    {
        if(hx_info_num != hx_info_flag)
        {
            memcpy(&gps_info, &hx_info[hx_info_flag], sizeof(struct NaviGPSInfo));
            hx_info_flag++;
            if(hx_info_flag >= 20)
                hx_info_flag = 0;
        }
    }
    printf("------------num=%d, flg=%d, time=%d----\n",hx_info_num, hx_info_flag,gps_info.iTime);
    
    if(gps_info.position == 0)
    {
        gps_info.fX = 0;
        gps_info.fY = 0;
        gps_info.iOrientation = 0;
        gps_info.iVelocity = 0;
        gps_info.ialtitude = 0;
        //gps_info.mode = 0;
    }

    if(gd_inout_flag)
    {
   next:
        if(gd_inout_time != gps_info.iTime)
        {
            for(int i=0; i< 20; i++)
            {
                if(gd_inout_time == hx_info[i].iTime)
                {
                     memcpy(&gps_info, &hx_info[i], sizeof(struct NaviGPSInfo));
                     hx_info_flag = i+1;
                     line_time_flag = 1;
                     break;
                }
            }
            if(line_time_flag == 0)
            {
                if(hx_info_num == 0)
                    hx_info_num = 20;
                line_time_flag = gd_inout_time-hx_info[hx_info_num-1].iTime+1;
                printf("---sleep=%d-------\n",line_time_flag);
                if(line_time_flag > 0)
                {
                    sleep(2);
                    goto next;
                }
            }
        }
        gd_inout_flag = 0;
        printf("----gd=%d, hx=%d, flg=%d\n ",gd_inout_time, gps_info.iTime,hx_info_flag);
    }

    if(mileage_flag > 1)
        mileage_flag--;
    else if(mileage_flag == 1)
    {
        mileage_flag = 0;
        int init_mileage = 0;
        VDRDeployParaTable *tmp_table = new VDRDeployParaTable();
        tmp_table->GetIntValue(VDR_DEPLOY_PARA_DEPLOY_MILEAGE, init_mileage);
        delete tmp_table;
        I2c_write_clear_mileage_data(init_mileage/10);
    } 
    
    gettimeofday (&tv , &tz);
    tmptime = (int)tv.tv_sec;
    memset(mcubuf, 0, sizeof(mcubuf));

    if(check_time_flag)
    {
        mcudata_len = 0;
        memset(mcu_data, 0, sizeof(mcu_data));
        min_info.m_time = tmptime-1;
        check_time_flag = 0;
        hx_info_flag = hx_info_num;
    }

    if(mcudata_len)
    {
        memcpy(mcubuf, mcu_data, mcudata_len); 
    }
    pthread_mutex_lock(&unbuf_mutex);
    memcpy(mcubuf+mcudata_len, un_speed_buf, un_speed_len);
    len  += un_speed_len;
    memset(un_speed_buf, 0, sizeof(un_speed_buf));
    un_speed_len = 0;
    pthread_mutex_unlock(&unbuf_mutex);

   
    icount = (len + mcudata_len)/10; 
    ret = tmptime - min_info.m_time;
    printf("readmcu: in sec=%d, usec=%d, icount=%d, ret=%d, res=%d\n",(int)tv.tv_sec, (int)tv.tv_usec, icount, ret,ret-icount);
    if(ret > 5 || ret<-5)
    {
        mcudata_len = 0;
        memset(mcu_data, 0, sizeof(mcu_data));
        min_info.m_time = tmptime-1;
        ret = 1;
    }
    #if 1
    if(icount <= 0)
        return ; 
    
    mcudata_len = 0;
    
    if(ret > icount)
    {
        tmptime = min_info.m_time + icount;

        if((vdr_current_info.running_flag == 0) && (speed_zero_flag == 1))
        {//停运状态， speed=0，直接丢速度=0 数据
            if((ret>1) && (icount==1))
            {
                tmptime = min_info.m_time + 2;
                printf("reticountzero:.....\n");
            }
        }
        if((vdr_current_info.running_flag == 1) && (speed_zero_flag == 0))
        {//运行状态，speed !=0,  补数据
        
            if((ret-icount)>2)
            {
                memmove(mcubuf+10, mcubuf, icount*10);
                tmptime++;
                icount++;
                mcubuf[0] = tmp_data[8];
                mcubuf[1] = tmp_data[9];
                mcubuf[2] = tmp_data[8];
                mcubuf[3] = (tmp_data[7]+tmp_data[9])/2;
                mcubuf[4] = tmp_data[8];
                mcubuf[5] = (tmp_data[9]+mcubuf[11])/2;
                mcubuf[6] = mcubuf[10];
                mcubuf[7] = (mcubuf[11]+mcubuf[13])/2;
                mcubuf[8] = mcubuf[10];
                mcubuf[9] = mcubuf[11];
                printf("reticountda2: add data");
                for(int i=0; i<icount*10; i++)
                    printf("%d ", mcubuf[i]);
                printf("\n");

            }
            else
            {
                memset(tmp_data, 0, sizeof(tmp_data));
                memcpy(tmp_data, mcubuf+(icount-1)*10, 10); 
            }
        }
    }
    
    if(ret<icount && ret>=0)
    {
        memset(mcu_data, 0, sizeof(mcu_data));
        memcpy(mcu_data, mcubuf+ret*10, (icount-ret)*10); 
        mcudata_len = (icount-ret)*10; 
        icount = ret;
    }
    #else
        //printf("---i=%d, icount=%d----\n",i,icount);
        i = 0;
        icount = 1; 
    #endif
    if(first_getdata_flag)
    {
        tmptime = (int)tv.tv_sec;
        if((int)tv.tv_usec > 900000)
            tmptime++;
        
        first_getdata_flag = 0;
        vdr_current_info.acc_on_time = tmptime-icount+1;
        write_vdr_off_status_table(1);
    }

    add_info.analog_quantity = 0;
    
    memset(accstatus, 0, 2);
    memset(pulse_sec, 0, 4);
    I2c_read_acc_data(accstatus, 2);
    I2c_read_alarm_data(alarmstatus, 1);
    speed_distance.ext_signal = accstatus[1];
    I2c_read_analog_quantity_data(analog_quantity, 1);
    add_info.analog_quantity |= analog_quantity[0];
    add_info.analog_quantity |= (analog_quantity[1] << 16);
    //I2c_read_camera_status(camerastatus, 1);
    I2c_read_D310_pulse_per_second(pulse_sec,4);
    gui_data.pulse_sec = (pulse_sec[0]<<24)|(pulse_sec[1]<<16)|(pulse_sec[2]<<8)|pulse_sec[3];

    if(dreg_info.active_disable == 1)
    {
        process_D310_CM_sensor_info();
        process_D310_CM_no_vecu_info();
    }
    if(dreg_info.wait_reply_flag == 1)
        process_D310_CM_reply_info(dreg_info.command_type);
    
    while(i < icount)
    { 
        speed = parse_speed_status_info(mcubuf+i*10, speednum, tmptime-icount+1+i);
        process_speed_signal_info(speed, tmptime-icount+1+i);
        write_track_run_status_table(mcubuf+i*10, speednum, tmptime-icount+1+i);
        
        if(i == (icount-1))
        {
            process_alarm_status_info(alarmstatus);
            process_acc_status_info(accstatus);
           
        }

        process_collision_side_turn_alarm(); 
        process_gps_position_info(tmptime-icount+1+i);

        i++;
    }
    
    if(shutdown_reboot_flag == 1)
        process_acc_off_info(REBOOT_FLAG, tmptime);
    if(shutdown_reboot_flag == 2)
        process_acc_off_info(SOFT_SHUTDOWN_FLAG, tmptime);
    if(shutdown_reboot_flag == 3)
        process_acc_off_info(8, tmptime);

    
    //gettimeofday (&tv , &tz);
    //printf("readmcu: out sec=%d, usec=%d, icount=%d\n",(int)tv.tv_sec, (int)tv.tv_usec, icount);
    
}

int parse_drivers_data_Info(unsigned char *buffer)
{
    int ch_sum = -1, ret = -1;
    int itime = 0;
    char tmpbuf[22];
    vector<unsigned char> seq;
    string drivename;
    int isdreg = -1;

#ifdef IC_SUPPORT_UNKNOWN
    ret = memcmp(buffer+28,"HTKJ",4);                
    if(ret != 0)
    {
	    DeviceDriveLoginDbusNotifyC notify;
	    memcpy(notify.driverid, "unknown" ,7);
	    dev_DbusServer->sendNotify(&notify);
	    printf("unknown HTKJ\n");
	    return -1;
    }
#endif

    ch_sum = check_sum(buffer+32,127-32);
    ret = check_sum(buffer,127);
    if((ret != buffer[127]) && (ch_sum != buffer[127]))
    {
	    DeviceDriveLoginDbusNotifyC notify;
	    memcpy(notify.driverid, "unknown" ,7);
	    dev_DbusServer->sendNotify(&notify);
	    printf("unknown crc\n");
	    return -1;
    }

    if(buffer[50]>0x99 || buffer[51]>0x12 || buffer[51]<0x01 || buffer[52]>0x31 || buffer[52]<0x01)
    { //year mouth day 
	    DeviceDriveLoginDbusNotifyC notify;
	    memcpy(notify.driverid, "unknown" ,7);
	    dev_DbusServer->sendNotify(&notify);
	    printf("无效卡\n");
	    return -1;
    }

    itime = time(NULL);

    if(tirednotify->flag==2){
	    tirednotify->flag=3;
	    tirednotify->dura=0;
	    memcpy(tirednotify->driverid,pretired_param.VehicleDriverID,18);
	    dev_DbusServer->sendNotify(tirednotify);
	    tirednotify->flag=0;
    }

#ifdef _8925_8D1
	if(Flag_DriverLogin == false)
	{
		if(vdr_current_info.speed_flag == 0)
		{
			DriversLoginTable *table = new DriversLoginTable();
			table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
			table->SetIntValue(VDR_DRV_LOGIN_LOGOUT,1);
			vdr_current_info.login_time = itime;
			memcpy(pretired_param.VehicleDriverID,buffer+32,18);//驾驶证编号
			table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM,pretired_param.VehicleDriverID);
			seq.clear();
			seq.push_back(buffer[50]);
			seq.push_back(buffer[51]);
			seq.push_back(buffer[52]);
			table->SetSequenceValue(VDR_DRV_VAILD_DATA,seq);
            memset(tmpbuf, 0, 22);
			memcpy(tmpbuf,buffer+53,20);
			table->SetStringValue(VDR_DRV_QUALIFICATION_NUM,tmpbuf);
            memset(tmpbuf, 0, 22);
			memcpy(tmpbuf,buffer+73,16);
			drivename = boost::locale::conv::to_utf<char>(tmpbuf,"GBK");
			table->SetStringValue(VDR_DRV_DRIVER_NAME,drivename);
			table->Commit();
			delete table;
			printf("*****************LOGIN suceed!******************\n");
			Flag_DriverLogin = true;
			first_process_ic_card = 1;
			memcpy(pretired_param.VehicleDriverID,buffer+32,18);
			PreTired_Read(1,itime);
            change_report_time_distance_mode(DEFAULT_MODE);
            if(vdr_current_info.ignition_timer_flag == 1)
            {
                StopTimer(ignitiontimer);
                vdr_current_info.ignition_timer_flag = 0;
            }
           
            if((dreg_info.active_disable == 1) &&(dreg_info.engine_status == 0))
            {
                isdreg = process_D310_dreg_IC_info(pretired_param.VehicleDriverID);
            }

            JT808DriverInfoReqC request;
            DBusReplyC *reply = NULL;
            request.m_time_index = itime;
            printf("-----report index = %d--------------\n",itime);
            ret = sp_ClientJT808->sendService(&request,reply,1*1000);
            if(ret < 0)
            {
                printf("Send Service Error.Error Code: %d\n",ret);
            }
		}
		DeviceDriveLoginDbusNotifyC notify;
		memcpy(notify.driverid,buffer+32,18);
        if(isdreg != -1)
        {
            notify.command = 0x33;
            notify.type = isdreg;
        }
		dev_DbusServer->sendNotify(&notify);
	}
#else
    if(Flag_DriverLogin == true)
    {
	    if( 0 == memcmp(pretired_param.VehicleDriverID, buffer+32, 18))
        {
    		if(vdr_current_info.speed_flag == 0)
            {
    		    DriversLoginTable *table = new DriversLoginTable();
    		    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
    		    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 0);
    		    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM,pretired_param.VehicleDriverID);
    		    table->Commit();
    		    delete table;
                
    		    Flag_DriverLogin = false;
    		    memset(pretired_param.VehicleDriverID, '0', 18);//驾驶证编号all to set 0 invalid id
    		    PreTired_Read(1,0);
				change_report_time_distance_mode(DEFAULT_MODE);
                check_ignition_alarm_info();
       		    JT808DriverInfoReqC   request;
    		    DBusReplyC  *reply=NULL;
    		    request.m_time_index= itime;
    		    printf(" ---report index=%d------------\n", itime);
    		    ret = sp_ClientJT808->sendService(&request,reply,5*1000);
    		    if(ret < 0)
    		    {
    			    printf("Send Service Error. Error Code: %d\n",ret);
    		    }
    		}
		    DeviceDriveLoginDbusNotifyC notify;
		    memset(notify.driverid, '0' ,18);
		    dev_DbusServer->sendNotify(&notify);
	    }
        else
        {
		    //写数据表，前一个登出，后登陆
    		if(vdr_current_info.speed_flag == 0)
            {
    		    DriversLoginTable *table = new DriversLoginTable();
    		    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime++);
    		    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 0);
    		    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, pretired_param.VehicleDriverID);
    		    table->Commit();


    		    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
    		    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 1);
    		    vdr_current_info.login_time = itime;
    		    memcpy(pretired_param.VehicleDriverID, buffer+32, 18);  //驾驶证编号
    		    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, pretired_param.VehicleDriverID);
    		    seq.clear();
    		    seq.push_back(buffer[50]);
    		    seq.push_back(buffer[51]);
    		    seq.push_back(buffer[52]);
    		    table->SetSequenceValue(VDR_DRV_VAILD_DATA, seq);
                memset(tmpbuf, 0, 22);
    		    memcpy(tmpbuf, buffer+53, 20);    //从业资格证号
    		    table->SetStringValue(VDR_DRV_QUALIFICATION_NUM, tmpbuf);
                memset(tmpbuf, 0, 22);
    		    memcpy(tmpbuf, buffer+73, 16);    //驾驶员姓名
    		    drivename = boost::locale::conv::to_utf<char>(tmpbuf, "GBK");
    		    table->SetStringValue(VDR_DRV_DRIVER_NAME, drivename);
    		    table->Commit();
    		    delete table;


    		    Flag_DriverLogin = true;
    		    memcpy(pretired_param.VehicleDriverID,buffer+32, 18);  //驾驶证编号all to set 0 invalid id
    		    PreTired_Read(1,itime);
                if(vdr_current_info.ignition_timer_flag == 1)
                {
                    StopTimer(ignitiontimer);
                    vdr_current_info.ignition_timer_flag = 0;
                }
                if((dreg_info.active_disable == 1) &&(dreg_info.engine_status == 0))
                {
                    isdreg = process_D310_dreg_IC_info(pretired_param.VehicleDriverID);
                }
                
     		    JT808DriverInfoReqC   request;
    		    DBusReplyC  *reply=NULL;
    		    request.m_time_index= itime;
    		    printf(" ---report index=%d------------\n", itime);
    		    ret = sp_ClientJT808->sendService(&request,reply,5*1000);
    		    if(ret < 0)
    		    {
    			    printf("Send Service Error. Error Code: %d\n",ret);
    		    }
    		}

		    DeviceDriveLoginDbusNotifyC notify;
		    memcpy(notify.driverid, buffer+32,18);
            if(isdreg != -1)
            {
                notify.command = 0x33;
                notify.type = isdreg;
            }
		    dev_DbusServer->sendNotify(&notify);
	    }
    }
    else
    {
	    if(vdr_current_info.speed_flag == 0)
        {
		    DriversLoginTable *table = new DriversLoginTable();
		    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
		    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 1);
		    vdr_current_info.login_time = itime;
		    memcpy(pretired_param.VehicleDriverID, buffer+32, 18);  //驾驶证编号
		    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, pretired_param.VehicleDriverID);
		    seq.clear();
		    seq.push_back(buffer[50]);
		    seq.push_back(buffer[51]);
		    seq.push_back(buffer[52]);
		    table->SetSequenceValue(VDR_DRV_VAILD_DATA, seq);
            memset(tmpbuf, 0, 22);
		    memcpy(tmpbuf, buffer+53, 20);    //从业资格证号
		    table->SetStringValue(VDR_DRV_QUALIFICATION_NUM, tmpbuf);
            memset(tmpbuf, 0, 22);
		    memcpy(tmpbuf, buffer+73, 16);    //驾驶员姓名
		    drivename = boost::locale::conv::to_utf<char>(tmpbuf, "GBK");  //table use  utf
		    table->SetStringValue(VDR_DRV_DRIVER_NAME, drivename);
		    table->Commit();
		    delete table;
		    Flag_DriverLogin = true;
            change_report_time_distance_mode(DEFAULT_MODE);

		    memcpy(pretired_param.VehicleDriverID,buffer+32, 18);  //驾驶证编号all to set 0 invalid id
		    PreTired_Read(1,itime);
            if(vdr_current_info.ignition_timer_flag == 1)
            {
                StopTimer(ignitiontimer);
                vdr_current_info.ignition_timer_flag = 0;
            }
            if((dreg_info.active_disable == 1) &&(dreg_info.engine_status == 0))
            {
                isdreg = process_D310_dreg_IC_info(pretired_param.VehicleDriverID);
            }            
    	    JT808DriverInfoReqC   request;
    	    DBusReplyC  *reply=NULL;
    	    request.m_time_index= itime;
    	    printf(" ---report index=%d------------\n", itime);
    	    ret = sp_ClientJT808->sendService(&request,reply,5*1000);
    	    if(ret < 0)
    	    {
    		    printf("Send Service Error. Error Code: %d\n",ret);
    	    }            
	    }
	    DeviceDriveLoginDbusNotifyC notify;
	    memcpy(notify.driverid, buffer+32,18);
        if(isdreg != -1)
        {
            notify.command = 0x33;
            notify.type = isdreg;
        }
	    dev_DbusServer->sendNotify(&notify);
    }
#endif //_8925_8D1

    return 0;
}

void* driver_login_info_thread(void *)
{
    int fd = -1;
    int ret = -1;
    unsigned char buffer[256];
    fd_set rfds;
    struct timeval tv;

	fd = open("/dev/sle5542", O_RDONLY);
	if(fd < 0)
	{
		printf("open file error!\n");
		return NULL;
	}
    
	// 2.选择操作区域
	struct sle5542_ioctl_data sle5542;
	sle5542.userzone = DATA_ZONE;
	ret = ioctl(fd, SLE5542_READ_ZONE, &sle5542);
	if(ret < 0)
	{
		printf("error in ioctl!\n");
		close(fd);
		return NULL;
	}

	while(1)
	{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = 0;
		tv.tv_usec = 500*1000;

		ret = select(fd + 1, &rfds, NULL, NULL, &tv);

		if (ret == -1)
			perror("select()");
		else if (ret)
		{
			if(!flag_readdone)
			{
				if(++ICChkCount < 5)
					continue;
				ICChkCount = 12;

				lseek(fd, 0, SEEK_SET);
				memset(buffer,0,256);
				if(read(fd, buffer, 128)>0)
                {
                    #if 0
					for(int i = 0; i < 128; i++)
					{
						printf("%x  ", buffer[i]);
					}
					printf("\n");
                    #endif
					parse_drivers_data_Info(buffer);
				}
                else 
                {
					DeviceDriveLoginDbusNotifyC notify;
					memcpy(notify.driverid, "unknown" ,7);
					dev_DbusServer->sendNotify(&notify);
					printf("=======================读取卡信息失败=================================\n");
				}
				flag_readdone = 1;
			}
			else
				usleep(5000);
		}
		else
		{
#ifdef _8925_8D1	
			if(Flag_DriverLogin)
			{
				if(vdr_current_info.speed_flag==0)
                {	
    				if(++exit_card < 3)
    					continue;
                    
                    int time_now;
    				time_now = time(NULL);
    				DriversLoginTable *table = new DriversLoginTable();
    				table->SetIntValue(VDR_DRV_OCCUR_TIME,time_now);
    				table->SetIntValue(VDR_DRV_LOGIN_LOGOUT,0);
    				table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM,pretired_param.VehicleDriverID);
    				table->Commit();
    				delete table;
    				
    				memset(pretired_param.VehicleDriverID,'0',18);
    				PreTired_Read(1,0);
    				Flag_DriverLogin = false;
    				first_process_ic_card = 0;
                	change_report_time_distance_mode(DEFAULT_MODE);
                    check_ignition_alarm_info();

    				printf("======================exit the card=========================\n");
    				
    				DeviceDriveLoginDbusNotifyC notify;
    				memset(notify.driverid,'0',18);
    				dev_DbusServer->sendNotify(&notify);
    				JT808DriverInfoReqC request;
    				DBusReplyC *reply = NULL;
    				request.m_time_index = time_now;
    				printf("--------report index=%d------------------\n",time_now);
    				ret = sp_ClientJT808->sendService(&request,reply,1*1000);
    				if(ret < 0)
    				{
    					printf("Send Service Error.Error Code:%d\n",ret);
    				}
    			}
                
				if(Flag_DriverLogin && first_process_ic_card )
                {
    				first_process_ic_card = 0;	
    				DeviceDriveLoginDbusNotifyC notify;
    				memset(notify.driverid,'0',18);
    				dev_DbusServer->sendNotify(&notify);
				}
			}	
#endif
			ICChkCount = 0;
			flag_readdone = 0;
			exit_card = 0;
			sleep(1);
			continue;
		}
	}
    close(fd);
}
void RebootMachineHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_REBOOT_MACHINE)
        return;
    shutdown_reboot_flag = 1;
}
void ShutdownMachineHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_SHUTDOWN_MACHINE)
        return;
    shutdown_reboot_flag = 2;
}

void CheckTimeHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_CHECK_TIME)
        return;
    check_time_flag = 1;
}

void CarryStatusHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier() != REQ_ID_CARRY_STATUS)
        return;

    CarryStatusReqC *carry_req = (CarryStatusReqC *)request;
    
    if(carry_req->carry_status == vdr_current_info.carry_status)
        return;
    vdr_current_info.carry_status = carry_req->carry_status;
    switch(carry_req->carry_status)
    {
        case 0:
            //空车
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 1:
            //半载
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 2:
            //保留
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITCLR(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
        case 3:
            //满载
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_HIGH);
            VDRBITSET(vdr_status_flag, LOCSTATUS_CARRY_STATUS_LOW);
            break;
    }

    tmp_status_flag = 1;
    
}

void SpeedPluseFactorChangeHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_SPEED_RATIO_UPDATE)
        return;
    
    SpeedRatioUpdateReqC *pluse_req = (SpeedRatioUpdateReqC *)request;

    VDRDeployParaTable *table = new VDRDeployParaTable();
    if(pluse_req->speed_factor > 0)
    {
        table->SetIntValue(VDR_DEPLOY_PARA_SPEED_PULSE_FACTOR, pluse_req->speed_factor);
        I2c_write_speed_pluse_factor_data(pluse_req->speed_factor);
        vdr_current_info.speed_factor = pluse_req->speed_factor;
    }
    if(pluse_req->lap_factor > 0)
    {
        table->SetIntValue(VDR_DEPLOY_PARA_LAP_PULSE_FACTOR, pluse_req->lap_factor);
        I2c_write_lap_pluse_factor_data(pluse_req->lap_factor);
        vdr_current_info.lap_factor = pluse_req->lap_factor;
    }    
    table->Commit();
    delete table;
    
}

void GpsSpeedReplaceVDRSpeedHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_GPS_SPEED_REPLACE_VDR_SPEED)
        return;
    GPSReplaceVDRSpeedReqC *speed_req = (GPSReplaceVDRSpeedReqC *)request;

    if(speed_req->flag == 1)
        vdr_current_info.gps_mcu_speed_flag = 1;
    else
        vdr_current_info.gps_mcu_speed_flag = 0;

    VDRBaseConfigTable *table = new VDRBaseConfigTable();
    table->SetIntValue(VDR_BASE_CONFIG_D0, vdr_current_info.gps_mcu_speed_flag);
    table->Commit();
    delete table;
}
void WirelessIntensityHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_WIRELESS_INTENSITY)
        return;
    WirelessIntensityReqC *wireless = (WirelessIntensityReqC *)request;
    add_info.wireless_intensity = wireless->signal_strength;

    printf("gui req wireless=%d\n",add_info.wireless_intensity);
}
void EmergencyAlarmReqHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_GUI_EMERGENCY_ALARM)
        return;
    EmergencyAlarmReqC *alarm = (EmergencyAlarmReqC *)request;
    if(alarm->flag == 1)
    {
        process_alarm_status_info(0xFF);
    }
}
void DevCommonEventReqHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_DEV_COMMON_EVENT)
        return;
    DevCommonEventReqC *cevent = (DevCommonEventReqC *)request;
    printf("----DevCommonEventReqHandler--- %d %d\n",cevent->type,cevent->value);
    if(cevent->type == 1)
    {
        gnss_mode = cevent->value;
        send_change_gnss_mode_udp(&gnss_mode, 4);
        
        VDRRunParaTable *runtable = new VDRRunParaTable();
        runtable->SetIntValue(VDR_PARA_GNSS_LOC_MODE, cevent->value);
        runtable->Commit();
        delete runtable;
    }
    else if(cevent->type == 2)
    {
        vdr_current_info.left_right_flag = cevent->value;
    }
    else if(cevent->type == 3)
    {
        VDRDeployParaTable *itable = new VDRDeployParaTable();
        itable->SetIntValue(VDR_DEPLOY_PARA_DEPLOY_MILEAGE, cevent->value);
        itable->Commit();
        delete itable;
        I2c_write_clear_mileage_data(cevent->value/10);
    }
}

void INSGPSInfoAnswerReqHandler(DBusRequestC *request, DBusReplyC *&reply)
{
    if(request->getIdentifier()!=REQ_ID_INSGPS_INFO_ANSWER)
        return;
    DevINSGPSAnswerReqC *answer = (DevINSGPSAnswerReqC *)request;
    unsigned char sed_buf[20];
    if(answer->type == 0x60)
    {
        memset(sed_buf, 0, 20);
        sed_buf[0] = 0x55;
        sed_buf[1] = 0x7A;
        sed_buf[2] = 0x60;
        sed_buf[3] = 0x00;
        sed_buf[4] = 0x01;
        sed_buf[5] = 0x00;
        sed_buf[6] = answer->value;
        sed_buf[7] = check_sum(sed_buf,7);
        WriteComPort(sp_fd, sed_buf, 8);
    }
    else if(answer->type == 0x59)
    {
        memset(sed_buf, 0, 20);
        sed_buf[0] = 0x55;
        sed_buf[1] = 0x7A;
        sed_buf[2] = 0x59;
        sed_buf[3] = 0x00;
        sed_buf[4] = 0x01;
        sed_buf[5] = 0x00;
        sed_buf[6] = answer->value;
        sed_buf[7] = check_sum(sed_buf,7);
        WriteComPort(sp_fd, sed_buf, 8);
    }
    else if(answer->type == 0x57)
    {
        memset(insbuf, 0, 9);
        memcpy(insbuf, answer->actual, 8);
        memset(sed_buf, 0, 20);
        sed_buf[0] = 0x55;
        sed_buf[1] = 0x7A;
        sed_buf[2] = 0x57;
        sed_buf[3] = 0x00;
        sed_buf[4] = 0x08;
        sed_buf[5] = 0x00;
        memcpy(sed_buf+6, answer->actual, 8);
        sed_buf[14] = check_sum(sed_buf,13);
        WriteComPort(sp_fd, sed_buf, 15);
        insgps_begin = 2;
    }
        
}

static int data_up_flag = 0;
static int data_up_count = 0;
void *udp_send_read_can_data(void *)
{
    char buf[1300] = {0};
    unsigned char temp[50] = {0};
    unsigned char zerobuf[50] = {0};
    int i = 0,len = 0, ret = -1;
    int socket_can;
    int port = 2010;
    struct sockaddr_in sin;
    unsigned char canid[4];
    unsigned char tmpch;
    int data_count_flag = 0;

    tmpch = vdr_param.can_id_collect[3];
    if(tmpch == 0)
    {
        canid[0] = vdr_param.can_id_collect[4];
        canid[1] = vdr_param.can_id_collect[5];
        canid[2] = vdr_param.can_id_collect[6];
        canid[3] = vdr_param.can_id_collect[7];
    }
    else
    {
        tmpch = vdr_param.other_can_id_collect[3]; 
        if(tmpch == 0)
        {
            canid[0] = vdr_param.other_can_id_collect[4];
            canid[1] = vdr_param.other_can_id_collect[5];
            canid[2] = vdr_param.other_can_id_collect[6];
            canid[3] = vdr_param.other_can_id_collect[7];
        }
    }
    printf("can data id: %02x %02x %02x %02x===\n",canid[0],
        canid[1],canid[2],canid[3]);
    
    socket_can = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_can == -1)
    {
        perror("socket call failed");
        return NULL;
    }

    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");
    sin.sin_port = htons(port);

    bind(socket_can, (struct sockaddr*)&sin, sizeof(sin));
    memset(buf, 0, 1300);
    len = 7;
    while(1)
    {
        memset(temp, 0 , 50);
        I2c_read_can_data(temp, 12);
        
        if(!memcmp(temp,zerobuf,12))
        {
#if 1
            printf("000 can data:");
            for(i = 0; i < 12; i++)
            {
                printf("%02x ", temp[i]);
            }
            printf("\n");
#endif	
            if(data_up_flag)
            {
                data_up_count++;
                if(data_up_count > 50)
                {
                    data_up_count = 0;
                    break ;
                }
            }
            usleep(50*1000);
            continue;
        }
        else
        {
            printf("can data 1:");
            for(i = 0; i < 12; i++)
            {
                printf("%02x ", temp[i]);
            }
            printf("\n");
            
            data_up_flag ++;
            data_up_count = 0;
            if(data_up_flag == 1)
            {
                struct timeval tv;
                struct timezone tz;
                int itime = 0;
                struct tm p;
                
                gettimeofday (&tv , &tz);
                localtime_r(&tv.tv_sec, &p);
                buf[2] = VALUE_TO_BCD(p.tm_hour);
                buf[3] = VALUE_TO_BCD(p.tm_min);
                buf[4] = VALUE_TO_BCD(p.tm_sec);
                
                itime = (int)tv.tv_usec/1000;
                buf[5] = VALUE_TO_BCD(itime/100);
                buf[6] = VALUE_TO_BCD(itime%100);
            }

            if(!memcmp(temp+1,canid+1,3))
            {
                usleep(100*1000);
                continue;
            }
          
            memcpy(buf+len, temp, 12);
            buf[len] = buf[len] | 0x40;
            len += 12;
            data_count_flag ++;
            if(data_count_flag == 50)
            {
                buf[0] = (data_up_flag>>8)&0x00FF;
                buf[1] = data_up_flag&0x00FF;
          #if 1      
                printf("send can data:");
                for(i = 0; i < len; i++)
                {
                    printf("%02x ", buf[i]);
                }
          #endif      
                
                ret = sendto(socket_can, buf, len, 0, (struct sockaddr *)&sin, sizeof(sin));
                if(ret == -1)
                {
                    begin_can_test = 0;
                    perror("sendto can data call failed");
                    return NULL;
                }
                
                printf("\n data up flag=%d,count=%d,len=%d\n",data_up_flag,data_count_flag,len);
                data_count_flag = 0;
                data_up_flag = 0;
                memset(buf, 0, 1300);
                len = 7;
            }
            else
                usleep(100*1000);
        }
    }

    if(data_up_flag > 0)
    {
        buf[0] = (data_up_flag>>8)&0x00FF;
        buf[1] = data_up_flag&0x00FF;
        #if 1
        printf("send can data:");
        for(i = 0; i < len; i++)
        {
            printf("%02x ", buf[i]);
        }
        #endif
        
        data_up_flag = 0;
        data_count_flag = 0;
        ret = sendto(socket_can, buf, len, 0, (struct sockaddr *)&sin, sizeof(sin));
        if(ret == -1)
        {
            perror("sendto can data call failed");
        } 
        printf("\n data up flag=%d,count=%d,len=%d\n",data_up_flag,data_count_flag,len);
        
    }

    begin_can_test = 0;

    return NULL;
}


/*
 * This is the routine called by the work queue servers to
 * perform operations in parallel.
 */
void engine_routine (void *arg)
{
     WriteTableHandlerInterface* WriteTab= (WriteTableHandlerInterface*)arg;
     WriteTab->WriteData(NULL);
     delete (WriteTableHandlerInterface*)arg;
}


void *udp_recv_gpsinfo_thread(void *param)
{
    fd_set rfds;
    struct timeval tv;
    int retval;
    string timername="readmcu";
    //JT808DataTransmitReqC    request;

    struct sockaddr client;
    int n;
    struct NaviGPSInfo gps_buf;

    int len = sizeof(struct sockaddr);
    while(1)
    {
        FD_ZERO(&rfds);
        FD_SET(socket_recvfd, &rfds);

        tv.tv_sec = 0;
        tv.tv_usec = 500*1000;

        retval = select(socket_recvfd + 1, &rfds, NULL, NULL, &tv);
     
        if (retval == -1)
        {
            perror("select()");
            continue;
        }  
        else if (retval)
        {
            memset(&gps_buf, 0, sizeof(struct NaviGPSInfo));
            //gps_buf.mode = 0;
            n = recvfrom(socket_recvfd, &gps_buf, 4*15, 0, &client, (socklen_t *)&len);
            if(hx_info_num != 0)
            {
                if(gps_buf.iTime == hx_info[hx_info_num-1].iTime)
                    continue;
            }
#if 0
            printf("----recv-----position=%d, flon=%d, flat=%d, %d, %d, iOrientation=%d, vel=%d, s=%d, w=%d, alt=%d, mode=%d, gps=%d, bd=%d\n",
                gps_buf.position, gps_buf.fX,gps_buf.fY,gps_buf.iTime, gps_buf.iSysTime,gps_buf.iOrientation, gps_buf.iVelocity,gps_buf.ilatitude, 
                gps_buf.ilongitude,gps_buf.ialtitude, gps_buf.mode,gps_buf.gpsnum, gps_buf.bdnum);

            //pthread_mutex_lock(&gps_mutex);
            //memcpy(&gps_info, &gps_buf, sizeof(struct NaviGPSInfo));
            //pthread_mutex_unlock(&gps_mutex);
#endif 
            memset(&hx_info[hx_info_num], 0, sizeof(struct NaviGPSInfo));
            if(gps_buf.position == 0)
            {
                gps_buf.fX = 0;
                gps_buf.fY = 0;
                gps_buf.iOrientation = 0;
                gps_buf.iVelocity = 0;
                gps_buf.ialtitude = 0;
                //gps_buf.mode = 0;
            }
            //pthread_mutex_lock(&gps_mutex);
            hx_info_begin_flag = 1;
            memcpy(&hx_info[hx_info_num], &gps_buf, sizeof(struct NaviGPSInfo));
            //pthread_mutex_unlock(&gps_mutex);
            hx_info_num++;
            if(hx_info_num >= 20)
                hx_info_num = 0;

            if((gps_buf.antenna == 306) && (vdr_current_info.gnss_line_flag == 0))
            {
                //request.type = 0xF1;    //notify jt808 gnss line unconnect, jt808 in  dead zone process
                //request.total = 1;
                //sp_ClientJT808->sendService(&request, reply, 5*1000);
                
                vdr_current_info.gnss_line_flag = 1;
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_UNCONNECT, 1);
                vdr_current_info.gnss_circuit_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_SHORT_CIRCUIT, 0);
            }
            else if((gps_buf.antenna == 305) && (vdr_current_info.gnss_circuit_flag == 0))
            {
                vdr_current_info.gnss_line_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_UNCONNECT, 0);
                vdr_current_info.gnss_circuit_flag = 1;
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_SHORT_CIRCUIT, 1);
            }
            else if((gps_buf.antenna == 304) && (vdr_current_info.gnss_line_flag != 0 || vdr_current_info.gnss_circuit_flag != 0))
            {
                //request.type = 0xF1;  //notify jt808 gnss line reconnect, jt808 out  dead zone process
                //request.total = 0;
                //sp_ClientJT808->sendService(&request, reply, 5*1000);
                
                vdr_current_info.gnss_line_flag = 0;
                vdr_current_info.gnss_circuit_flag = 0;
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_UNCONNECT, 0);
                change_alarm_flag_and_upload(ALARM_STATUS_GNSS_SHORT_CIRCUIT, 0);
            }
#if 1            
            printf("position=%d, fx=%d, fy=%d, %d,%d,iOrientation=%d,iVel=%d,ilat=%d,ilon=%d,ialt=%d mode=%d gsp=%d,bd=%d,line=%d, %d\n",
                gps_buf.position,gps_buf.fX,gps_buf.fY,gps_buf.iTime,gps_buf.iSysTime,gps_buf.iOrientation,gps_buf.iVelocity,
                gps_buf.ilatitude, gps_buf.ilongitude,gps_buf.ialtitude,gps_buf.mode,gps_buf.gpsnum,gps_buf.bdnum,gps_buf.antenna,hx_info_num);
#endif
            #ifdef BLIND_ZONE
            //read_mcu_data_handler(timername,NULL, NULL);
            #endif
        }
    }

    return NULL;
}

int udp_socket_recv()
{
    socket_recvfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_recvfd == -1)
    {
        perror("socket call failed");
        return -1;
    }

    int port_recv = 1013;
    bzero(&addr_recv, sizeof(addr_recv));
    addr_recv.sin_family = AF_INET;
    addr_recv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr_recv.sin_port = htons(port_recv);

    bind(socket_recvfd, (struct sockaddr*)&addr_recv, sizeof(addr_recv));

    int ret = -1;
    pthread_t sock_recv;
    ret = pthread_create(&sock_recv, NULL, udp_recv_gpsinfo_thread, (void *)&addr_recv);
    if(ret < 0)
    {
    printf("create handle_image error!\n");
    return ret;
    }
    pthread_detach(sock_recv);

    return 0;
}

int send_speed_udp_socket()
{
    int port =  8900;

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);

    socket_dp = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_dp == -1)
    {
        perror("socket call failed");
        return -1;
    }

    return socket_dp;
}
int parse_ic_card_info(unsigned char *buffer)
{
    vector<unsigned char> seq;
    string drivename;
    int len = 0, i= 0;
    int itime = 0;
    DeviceDriveLoginDbusNotifyC notify;
    char tmpbuf[22];

    itime = time(NULL);

    memset(tmpbuf, 0, 22);
    len = buffer[0];
    memcpy(tmpbuf, buffer+1, len);    //驾驶员姓名
    drivename = boost::locale::conv::to_utf<char>(tmpbuf, "GBK"); //table use  utf
    memcpy(iccard_info.drvname, drivename.c_str(), drivename.size());
    i = 1 + len;
    memcpy(iccard_info.qualification, buffer+i, 20);    //从业资格证号
    i += 20;   
    len = buffer[i];
    memset(tmpbuf, 0, 22);
    memcpy(tmpbuf, buffer+i+1, len);    //发证机构名称
    drivename = boost::locale::conv::to_utf<char>(tmpbuf, "GBK"); //table use  utf
    memcpy(iccard_info.authority, drivename.c_str(), drivename.size());
    i = i + len + 1; 
    memcpy(iccard_info.validate, buffer+i, 4);        //有效日期
    iccard_info.status = 1;
    
    notify.command = 0x41;
    notify.type = 0x00;
    memcpy(notify.driverid, iccard_info.qualification ,20);
    dev_DbusServer->sendNotify(&notify);

    DriversLoginTable *table = new DriversLoginTable();
    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 1);    
    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, iccard_info.qualification);
    table->SetStringValue(VDR_DRV_QUALIFICATION_NUM, iccard_info.qualification); 
    table->SetStringValue(VDR_DRV_DRIVER_NAME, iccard_info.drvname);
    table->SetStringValue(VDR_DRV_CERTIFYING_AUTHORITY, iccard_info.authority); 
    seq.clear();
    seq.push_back(iccard_info.validate[0]);
    seq.push_back(iccard_info.validate[1]);
    seq.push_back(iccard_info.validate[2]);
    seq.push_back(iccard_info.validate[3]);
    table->SetSequenceValue(VDR_DRV_VAILD_DATA, seq);
    table->SetIntValue(VDR_DRV_IC_CARD_READ_RESULT, 0x00); 
    table->Commit();
    delete table;
    
    JT808DriverInfoReqC   request;
    DBusReplyC  *reply=NULL;
    int ret = -1;
    
    request.m_time_index= itime;
    ret = sp_ClientJT808->sendService(&request,reply,5*1000);
    if(ret < 0)
    {
        printf("Send Service Error. Error Code: %d\n",ret);
    }

    return 0;
}
//HB 雄帝IC卡
void* read_serial_IC_card_thread(void *)
{
    int len = 0, rev_len=0;
    unsigned char rev_buf[MAX_LEN];
    unsigned char sed_buf[128];
    int itime = -1;
    vector<unsigned char> seq;
    int j=0;

    iccard_fd = OpenComPort(5, 9600, 8,  "1", 'N');
    if(iccard_fd < 0)
    {
        printf("Can not open the ic card serial port\n");
        return 0;
    }

    memset(rev_buf, 0, sizeof(rev_buf));

    JT808DataTransmitReqC datetransmitreq;
    DBusReplyC  *reply=NULL;
    int ret = -1;
    DeviceDriveLoginDbusNotifyC notify;

    while(1)
    {
        if(rev_len > MAX_LEN/2)
            memset(rev_buf, 0, sizeof(rev_buf));

        len = ReadComPort(iccard_fd, rev_buf+rev_len, MAX_LEN);
        if(len < 0)
        {
            printf("ReadComPort failed \n");
            break;
        }
        else 
        {
            rev_len += len;
            printf("comread: 0=%02x, %d=%02x\n",rev_buf[0],rev_len-1, rev_buf[rev_len-1]);
            if(rev_buf[0] != 0x7E && rev_buf[rev_len-1] == 0x7E)
            {
                memset(rev_buf, 0, sizeof(rev_buf));
                rev_len = 0;
            }
            if(rev_buf[0] == 0x7E && rev_buf[rev_len-1] == 0x7E && rev_len>7)
            {     
                j=0;
                printf("total data:");
                for(j=0; j<rev_len;j++)
                {
                    printf("%02x ",rev_buf[j]);
                }
                printf("\n");

                if(rev_buf[7] == 0x40 && rev_buf[8] == 0x00)
                {
                    memset(&iccard_info, 0, sizeof(struct ICcardInfo));
                    iccard_info.version[0] = rev_buf[2];
                    iccard_info.version[1] = rev_buf[3];
                    iccard_info.manuid[0] = rev_buf[4];
                    iccard_info.manuid[1] = rev_buf[5];
                    iccard_info.type  = rev_buf[6];
                    
                    datetransmitreq.type = 0x0B;
                    memset(datetransmitreq.buf, 0, 128);
                    memcpy(datetransmitreq.buf, rev_buf+9,rev_len-9);
                    datetransmitreq.total = rev_len-9;
                    ret = sp_ClientJT808->sendService(&datetransmitreq,reply,5*1000);
                    if(ret < 0)
                    {
                        printf("Send data transmit Error. Error Code: %d\n",ret);
                    }
                }
                else if(rev_buf[7] == 0x40 && rev_buf[8] != 0x00)
                {
                    notify.command = 0x40;
                    notify.type = rev_buf[8];
                    dev_DbusServer->sendNotify(&notify);
                    memset(sed_buf, 0, 128);
                    sed_buf[0] = 0x7E;
                    sed_buf[1] = 0x00;
                    sed_buf[2] = iccard_info.version[0];
                    sed_buf[3] = iccard_info.version[1];
                    sed_buf[4] = iccard_info.manuid[0];
                    sed_buf[5] = iccard_info.manuid[1];
                    sed_buf[6] = iccard_info.type;
                    sed_buf[7] = 0x40;
                    sed_buf[8] = 0x03;
                    sed_buf[9] = 0x7E;
                    sed_buf[1] = check_external_dev_sum(sed_buf+4, 5) & 0x00FF;
                    WriteComPort(iccard_fd, sed_buf, sizeof(sed_buf));
                }
                else if(rev_buf[7] == 0x41)
                {
                    memset(sed_buf, 0, 128);
                    sed_buf[0] = 0x7E;
                    sed_buf[1] = 0x00;
                    sed_buf[2] = iccard_info.version[0];
                    sed_buf[3] = iccard_info.version[1];
                    sed_buf[4] = iccard_info.manuid[0];
                    sed_buf[5] = iccard_info.manuid[1];
                    sed_buf[6] = iccard_info.type;
                    sed_buf[7] = 0x41;
                    sed_buf[8] = 0x7E;
                    sed_buf[1] = check_external_dev_sum(sed_buf+4, 4) & 0x00FF;
                    WriteComPort(iccard_fd, sed_buf, sizeof(sed_buf));

                    if(rev_len > 80)
                    {
                        j=1;
                        printf("over data:");
                        for(j=0; j<rev_len;j++)
                        {
                            if(rev_buf[j]== 0x7E)
                                break;
                            printf("%02x ",rev_buf[j]);
                        }
                    }
                    else
                        j = 0;
                    
                    if(rev_buf[j+8] == 0x00)
                        parse_ic_card_info(rev_buf+j+9);
                  /*  else
                    {
                        itime = time(NULL);
                        DriversLoginTable *table = new DriversLoginTable();
                        table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
                        table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 1);    
                        table->SetIntValue(VDR_DRV_IC_CARD_READ_RESULT, rev_buf[8]); 
                        table->Commit();
                        delete table;
                        
                        notify.command = 0x41;
                        notify.type = rev_buf[8];
                        dev_DbusServer->sendNotify(&notify); 
                        
                        JT808DriverInfoReqC   request;
                        DBusReplyC  *reply=NULL;
                        int ret = -1;
                        
                        request.m_time_index= itime;
                        ret = sp_ClientJT808->sendService(&request,reply,5*1000);
                        if(ret < 0)
                        {
                            printf("Send Service Error. Error Code: %d\n",ret);
                        }

                    }
                    */
                }
                else if(rev_buf[7] == 0x42)
                {
                    itime = time(NULL);
                    WriteComPort(iccard_fd, rev_buf, sizeof(rev_buf)); //
                    
                    notify.command = 0x42;
                    notify.type = 0x00;
                    memcpy(notify.driverid, iccard_info.qualification ,20);
                    dev_DbusServer->sendNotify(&notify);    

                    iccard_info.status = 2;
                    DriversLoginTable *table = new DriversLoginTable();
                    table->SetIntValue(VDR_DRV_OCCUR_TIME, itime);
                    table->SetIntValue(VDR_DRV_LOGIN_LOGOUT, 0);    
                    table->SetStringValue(VDR_DRV_DRIVER_LICENSE_NUM, iccard_info.qualification);
                    table->SetStringValue(VDR_DRV_QUALIFICATION_NUM, iccard_info.qualification); 
                    table->SetStringValue(VDR_DRV_DRIVER_NAME, iccard_info.drvname);
                    table->SetStringValue(VDR_DRV_CERTIFYING_AUTHORITY, iccard_info.authority); 
                    seq.clear();
                    seq.push_back(iccard_info.validate[0]);
                    seq.push_back(iccard_info.validate[1]);
                    seq.push_back(iccard_info.validate[2]);
                    seq.push_back(iccard_info.validate[3]);
                    table->SetSequenceValue(VDR_DRV_VAILD_DATA, seq);
                    table->SetIntValue(VDR_DRV_IC_CARD_READ_RESULT, 0x00); 
                    table->Commit();
                    delete table;
                    
                    
                    JT808DriverInfoReqC   request;
                    DBusReplyC  *reply=NULL;
                    int ret = -1;
                    
                    request.m_time_index= itime;
                    ret = sp_ClientJT808->sendService(&request,reply,5*1000);
                    if(ret < 0)
                    {
                        printf("Send Service Error. Error Code: %d\n",ret);
                    }
                }
                
                len = 0;
                rev_len = 0;
                memset(rev_buf, 0, sizeof(rev_buf));
            }
        }
    }
    if(iccard_fd)
        CloseComPort(iccard_fd);
    return 0;
}

void read_startingup_info_thread()
{
    int num = 0, i=0;
    unsigned char buf[2];

    memset(startup_buf, 0, sizeof(startup_buf));
    memset(buf, 0, 2);
    I2c_read_startingup_data_num(buf, 2);
    printf("buf0=%d,buf1=%d\n",buf[0],buf[1]);
    num = buf[1];
    num |= (buf[0] << 8);
    num = num/5;
    printf("now time=%d, have %d speed data to read...\n",(int)time(NULL),num);
    startup_time_flage = time(NULL);
    startup_data_num = num;
    if(num == 0 || num > 200)
        return ;
    if(num > 50)
        startup_data_num = num = 50;
    while(1)
    {
        I2c_read_startingup_data_info(startup_buf+i*10, 10);
#if 1
        for(int j=0; j<5; j++)
            printf("%d ", startup_buf[i*10+j*2+1]);
        printf(" i=%d,num=%d\n",i,num);
#endif
        i++;
        if(i==num)
            break;  
    }
    startup_time_flage = time(NULL);
    printf("read starting up speed i=%d, time=%d ...\n",i,(int)time(NULL));
    int speed=0;
    float distance = 0;
    speed_zero_count = 0;
    speed_data_count = 0;
    
    for(i=0;i<num*10;)
    {
        speed = startup_buf[i+1]+startup_buf[i+3]+startup_buf[i+5]+startup_buf[i+7]
            +startup_buf[i+9];
        if(speed == 0)
        {
            vdr_current_info.stop_timer_flag = 1;
            vdr_current_info.start_timer_flag = 0;
            speed_data_count = 0;
            speed_zero_count++;
            if(speed_zero_count == 1)
            {
                pretired_param_temp.StoppingTime=startup_time_flage-(num-i/10)+1;
            }
            if(speed_zero_count == 10)
            {
                if(!(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF))
                {
                    VDRBITSET(vdr_status_flag, LOCSTATUS_CAR_WORK_OFF);
                    vdr_current_info.running_flag=0;
                    write_track_accident_from_buf(1, i/10-num+1, loc_info);
                }
            }
        }
        else
        {
            vdr_current_info.stop_timer_flag = 0;
            vdr_current_info.start_timer_flag = 1;
            distance = speed/18;
            speed_data_count ++;
            speed_zero_count = 0;
            if(speed_data_count == 10)
            {
                if(vdr_status_flag & LOCSTATUS_CAR_WORK_OFF)
                {
                    VDRBITCLR(vdr_status_flag, LOCSTATUS_CAR_WORK_OFF);
                    vdr_current_info.running_flag=1;
                    if(vdr_current_info.park_timer_flag)
                    {
                        StopTimer(parktimer);
                        vdr_current_info.park_timer_flag = 0; 
                    }
                }
            }
        }
        i+=10;
    }
    tempmileage += distance; 
    printf("end read starting up speed distance=%f, time=%d zero=%d, data=%d...\n",distance,(int)time(NULL),
        speed_zero_count, speed_data_count);
}

static void SignalHandler(int sig)
{
    LogUtility::Log(LOG_LEVEL_INFO, "devicemanager SignalHandler catch signal 0x%x.", sig);
}

void* read_caninfo_from_mcu_thread(void* )
{
    unsigned char upparam[7];
    int bytes = 0;
    int packets = 0;
    JT808DataTransmitReqC cantransreq;

    memset(upparam,0,7);

    cantransreq.type = 0xF8;
    while(1)
    {
        I2c_read_can_up_param_info(upparam, 6);
        bytes = (upparam[1]<<8) | upparam[0];
        if(bytes == 0)
        {
            sleep(1);
            continue;
        }
        else
        {
            memset(cantransreq.buf, 0, 512);
            packets = upparam[2];
            cantransreq.buf[0] = upparam[3];
            cantransreq.buf[1] = upparam[4];

            printf("receive from mcu: %02x %02x %02x %02x %02x %02x\n",upparam[0],upparam[1],
                upparam[2],upparam[3],upparam[4],upparam[5]);
            printf("bytes =%d, packets=%d\n",bytes,packets);
            int j=0;
            for(int i=0; i<packets;i++)
            {
                I2c_read_can_up_data_info(cantransreq.buf+2+i*7, 7);
                for(j=0; j<7;j++)
                {
                    printf("%02x ",cantransreq.buf[2+i*7+j]);
                }
                printf("\n");
            }
            cantransreq.total = 2+bytes;
            printf("can up data: ");
            for(int j=0; j<cantransreq.total; j++)
                printf(" %02x ",cantransreq.buf[j]);
            printf("\n");
            sp_ClientJT808->sendService(&cantransreq);
        }
    }

}
void read_d310_can_info_thread(int vflag )
{
    int speed_factor=0, tcp_port=0,lap_factor=0;
    unsigned char main_server[17];
    unsigned char buf[20];
    char carvin[18];
    memset(carvin, 0, 18);

    printf("----vflag=%d---\n",vflag);
    if(vflag&Hw0)
    {
        memset(buf, 0, 20);
        I2c_read_D310_can_pulsefactor_data(buf, 8);
        speed_factor = (buf[0]<<8)|buf[1];
        lap_factor = (buf[2]<<8)|buf[3];
    }
    if(vflag&Hw1)
    {
        memset(carvin, 0, 18);
        I2c_read_D310_can_carvin_data((unsigned char *)carvin, 17);
    }
    if(vflag&Hw2)
    {
        memset(buf, 0, 20);
        I2c_read_D310_can_carip_data(buf, 18);
        memset(main_server, 0, 17);
          
        int j=0, flag=0,i=0;
        if(buf[0] == '0')
            flag = 1;
        for(i=0; i<12; i++)
        {
            if((i!=0) &&(i%3 == 0))
            {
                main_server[j] ='.';
                j++;
                flag = 1;
            }

            if(buf[i] == '0')
            {
                if(flag>0 && flag<3)
                {
                    flag++;
                    continue;
                }           
            }
            
            main_server[j] = buf[i];
            j++;
            flag = 0;
        }  
        tcp_port = (buf[12]-0x30)*100000 +(buf[13]-0x30)*10000+(buf[14]-0x30)*1000+(buf[15]-0x30)*100+
        (buf[16]-0x30)*10+(buf[17]-0x30);   
        
        JT808ModifySettingReqC request;
        request.m_main_server = (char *)malloc(j+1);
        memcpy(request.m_main_server, main_server, j+1);
        request.m_port = tcp_port;
        sp_ClientJT808->sendService(&request);
    }

    printf("--d310 can=%d,%d,%s,%s,%d\n",speed_factor,lap_factor,carvin,main_server,tcp_port);

    if((vflag&Hw0) || (vflag&Hw1))
    {
        VDRDeployParaTable *table = new VDRDeployParaTable();
        if(speed_factor != 0)
        {
            table->SetIntValue(VDR_DEPLOY_PARA_SPEED_PULSE_FACTOR, speed_factor);
            vdr_current_info.speed_factor = speed_factor;
        }
        if(lap_factor != 0)
        {
            table->SetIntValue(VDR_DEPLOY_PARA_LAP_PULSE_FACTOR, lap_factor);
            vdr_current_info.lap_factor = lap_factor;
        }
        if(carvin[0] != 0x00)
            table->SetStringValue(VDR_DEPLOY_PARA_CAR_VIN, carvin);
        table->Commit();
        delete table;
    }

}

void prepare_09H_buf_data( int itime, struct NaviGPSInfo *locstatus)
{
    int ihoure = 0, imin = 0, offset=0;
    struct tm p;
    const time_t ttime = itime;
    int tmplongitude=0, tmplatitude=0;

    ihoure = HOUR_ALINE(itime);
    localtime_r(&ttime, &p);
    imin = p.tm_min;
    
    //printf("---hour09=%d, houre=%d,imin=%d,itime=%d,speed=%d\n",houre_09, ihoure,imin,itime,min_info.t_speed);
    if(ihoure > houre_09)
    {
        memset(buf_09, 0xFF, 666);
        buf_09[0]= VALUE_TO_BCD(p.tm_year % 100);
        buf_09[1]= VALUE_TO_BCD(p.tm_mon + 1);
        buf_09[2]= VALUE_TO_BCD(p.tm_mday);
        buf_09[3]= VALUE_TO_BCD(p.tm_hour);
        buf_09[4]= 0;
        buf_09[5]= 0;
        write_09H_vdr_file();
        houre_09 = ihoure;
    }
    
    if(vdr_status_flag & 0x02)
    {
        tmplongitude = locstatus->fX*6/10;
        if(vdr_status_flag & 0x08)
        {
            tmplongitude = -tmplongitude;
        }
        tmplatitude = locstatus->fY*6/10;
        if(vdr_status_flag & 0x04)
        {
            tmplatitude = -tmplatitude;
        }
        buf_09[6+imin*11] = (tmplongitude>>24) & 0xFF;
        buf_09[6+imin*11 + 1] = (tmplongitude >> 16) & 0xFF;
        buf_09[6+imin*11 + 2] = (tmplongitude >> 8) & 0xFF;
        buf_09[6+imin*11 + 3] = tmplongitude & 0xFF;
        buf_09[6+imin*11 + 4] = (tmplatitude >> 24) & 0xFF;
        buf_09[6+imin*11 + 5] = (tmplatitude >> 16) & 0xFF;
        buf_09[6+imin*11 + 6] = (tmplatitude >> 8) & 0xFF;
        buf_09[6+imin*11 + 7] = tmplatitude & 0xFF;
        buf_09[6+imin*11 + 8] = (locstatus->ialtitude >> 8) & 0xFF;
        buf_09[6+imin*11 + 9] = locstatus->ialtitude & 0xFF;
        buf_09[6+imin*11 + 10] = min_info.t_speed/10;
    }
    else
    {
        buf_09[6+imin*11] =  0x7F;
        buf_09[6+imin*11 + 1] = 0xFF;
        buf_09[6+imin*11 + 2] = 0xFF;
        buf_09[6+imin*11 + 3] = 0xFF;
        buf_09[6+imin*11 + 4] = 0x7F;
        buf_09[6+imin*11 + 5] = 0xFF;
        buf_09[6+imin*11 + 6] = 0xFF;
        buf_09[6+imin*11 + 7] = 0xFF;
        buf_09[6+imin*11 + 8] = 0x7F;
        buf_09[6+imin*11 + 9] = 0xFF;
        buf_09[6+imin*11 + 10] = min_info.t_speed/10;
    }
    
    
    offset = 10 + (current_09-1)*666 + 6 + imin*11;    
    //printf("---offset=%d--imin=%d--\n", offset, imin);
    fseek(fp_09, offset, SEEK_SET);
    fwrite(buf_09+6+imin*11, 11, 1, fp_09); 
    fflush(fp_09);

}
int write_09H_vdr_file()
{
    long offset = 0;
    
    if(total_09 != 361)
    {
        total_09++;
        current_09++;
    }
    else
    {
        if(current_09 == 361)
            current_09 = 0;

        current_09++;
    }
        
    fseek(fp_09, 0, SEEK_SET);
    fwrite(&total_09, 4, 1, fp_09);
    fwrite(&current_09, 4, 1, fp_09);

    offset = 10 + (current_09-1)*666;    
    fseek(fp_09, offset, SEEK_SET);
    fwrite(buf_09, 666, 1, fp_09); 
    fflush(fp_09);
    //printf("---write_09H_vdr_file : total=%d, cur=%d----\n",total_09,current_09);
    return 0;
}

int write_08H_vdr_file()
{
    long offset = 0;
    
    if(total_08 != 2890)
    {
        total_08++;
        current_08++;
    }
    else
    {
        if(current_08 == 2890)
            current_08 = 0;

        current_08++;
    }
    //printf("---write_08H_vdr_file : total=%d, cur=%d-time=%d---\n",total_08,current_08,time(NULL));
    
    fseek(fp_08, 0, SEEK_SET);
    fwrite(&total_08, 4, 1, fp_08);
    fwrite(&current_08, 4, 1, fp_08);

    offset = 10 + (current_08-1)*126;    
    fseek(fp_08, offset, SEEK_SET);
    fwrite(buf_08, 126, 1, fp_08); 
    fflush(fp_08);

    return 0;
}

int open_08H_09H_file()
{
    char tmp[6];
    int offset = 0;
    
    fp_08 = fopen("/nand4/tmp08.vdr", "a+");
    fclose(fp_08);
    fp_09 = fopen("/nand4/tmp09.vdr", "a+");
    fclose(fp_09);
    
    fp_08 = fopen("/nand4/tmp08.vdr", "r+");
    fp_09 = fopen("/nand4/tmp09.vdr", "r+");
    
	if(fp_08 == NULL) 
	{
		printf("Can't open tmp08.vdr %s\n", strerror(errno));
		return -1;
	}
	if(fp_09 == NULL) 
	{
		printf("Can't open tmp09.vdr %s\n", strerror(errno));
		return -1;
	}
    memset(buf_08, 0xFF, 126);
    memset(buf_09, 0xFF, 666);
    
    fread(&total_08, 4,1, fp_08);
    fread(&current_08, 4,1, fp_08);
    fread(&total_09, 4,1, fp_09);
    fread(&current_09, 4,1, fp_09);

    if(current_09 == 0)
        houre_09 = 0;
    else
    {
        offset = 10 + (current_09-1)*666;    
        fseek(fp_09, offset, SEEK_SET);
        fread(tmp, 6, 1, fp_09);
        
        struct tm lt;
        lt.tm_year = BCD_TO_VALUE(tmp[0]) + 100;
        lt.tm_mon = BCD_TO_VALUE(tmp[1]) - 1;
        lt.tm_mday = BCD_TO_VALUE(tmp[2]);
        lt.tm_hour = BCD_TO_VALUE(tmp[3]);
        lt.tm_min = BCD_TO_VALUE(tmp[4]);
        lt.tm_sec = BCD_TO_VALUE(tmp[5]);
        houre_09 = (unsigned int)mktime(&lt);
    }

    printf("08 file len=%d, current=%d 09 file len=%d, current=%d, hour=%d\n",
        total_08, current_08, total_09, current_09, houre_09);
    
	return 0;	
}

int open_loc_0809_file()
{
    num_loc = 5;
    fp_loc = fopen("/nand4/loc0809.vdr", "a+");
    fclose(fp_loc);
    
    fp_loc = fopen("/nand4/loc0809.vdr", "r+");
    
	if(fp_loc == NULL) 
	{
		printf("Can't open loc0809.vdr %s\n", strerror(errno));
		return -1;
	}
    fread(loc_buf,sizeof(struct LocStatusData),num_loc,fp_loc);


    fp_run = fopen("/nand4/locrun.vdr", "a+");
    fclose(fp_run);
    
    fp_run = fopen("/nand4/locrun.vdr", "r+");
    
    if(fp_run == NULL) 
    {
        printf("Can't open locrun.vdr %s\n", strerror(errno));
        return -1;
    }  
    
    fread(&total_run, 4,1, fp_run);
    fread(&current_run, 4,1, fp_run);

	return 0;	
}

void data_up_pass_through_info(unsigned char *msgbuf)
{
    int len = 0;
    JT808DataTransmitReqC datatransreq;
    DBusReplyC  *reply=NULL;
    int ret = -1;
    
    len = msgbuf[3]<<8 | msgbuf[4];
    printf("======len=%d-----\n",len);
    for(int j=0; j<len+7;j++)
    {
        
        printf(" 0x%02x ",msgbuf[j]);
    }
    printf("\n");

    datatransreq.type = 0x41;
    memset(datatransreq.buf, 0, 512);
    memcpy(datatransreq.buf, msgbuf+6, len);
    datatransreq.total = len;
    ret = sp_ClientJT808->sendService(&datatransreq,reply,5*1000);
    if(ret < 0)
    {
        printf("Send data transmit Error. Error Code: %d\n",ret);
    }
    return;
}

// 把经纬度的度格式转换为度分，
// ddmm.mmmm <- dd.ddddddd or dddmm.mmmm <- ddd.ddddddddd
void doubleformat2buf(double x, unsigned char * buf)
{
    int deg;
    double frac;
    double y;

    deg = (int)(x)*100;
    frac = (x - (int)x)*60;

    y = deg+frac;
    
    gcvt(y, 12, (char *)buf);
}

void* send_insgps_position_info_thread(void *)
{
    unsigned char sed_buf[50];
    unsigned char xybuf[20];
    int temp,icount;
    
    memset(sed_buf, 0, 50);
    memset(xybuf,0,20);

    while(1)
    {
        icount = 5;
        sed_buf[0] = 0x55;
        sed_buf[1] = 0x7A;
        sed_buf[2] = 0x58;
        sed_buf[3] = 0x00;
        sed_buf[4] = 0x20;
        sed_buf[5] = 0x00;
        doubleformat2buf((double)gui_data.longitude/1000000, xybuf);
        memcpy(xybuf+strlen((char *)xybuf),"000000000",12-strlen((char *)xybuf));
        memcpy(sed_buf+6,xybuf,12);
        //printf("longitude=%s,%s\n",sed_buf+6,xybuf);
        doubleformat2buf((double)gui_data.latitude/1000000, xybuf);
        memcpy(xybuf+strlen((char *)xybuf),"000000000",12-strlen((char *)xybuf));
        memcpy(sed_buf+18,xybuf,12);
        //printf("latitude=%s,%s\n",sed_buf+18,xybuf);
        
        temp = gui_data.vdr_speed*100;
        sed_buf[30] = (temp>>24) & 0xFF;
        sed_buf[31] = (temp>>16) & 0xFF;
        sed_buf[32] = (temp>>8) & 0xFF;
        sed_buf[33] = temp & 0xFF;
        temp = vdr_current_info.total_mileage;
        sed_buf[34] = (temp>>24) & 0xFF;
        sed_buf[35] = (temp>>16) & 0xFF;
        sed_buf[36] = (temp>>8) & 0xFF;
        sed_buf[37] = temp & 0xFF;
        sed_buf[38] = check_sum(sed_buf,38);

#if 1
        WriteComPort(sp_fd, sed_buf, 39);
        sleep(1);
#else
        while(icount)
        {
            icount--;
            WriteComPort(sp_fd, sed_buf, 39);
            usleep(200*1000);
        }
#endif
        if(insgps_begin == 2)
        {
            memset(sed_buf, 0, 50);
            sed_buf[0] = 0x55;
            sed_buf[1] = 0x7A;
            sed_buf[2] = 0x57;
            sed_buf[3] = 0x00;
            sed_buf[4] = 0x08;
            sed_buf[5] = 0x00;
            memcpy(sed_buf+6, insbuf, 8);
            sed_buf[14] = check_sum(sed_buf,13);
            WriteComPort(sp_fd, sed_buf, 15);
        }
    }
}
void process_insgps_info(unsigned char *buf, unsigned char flag)
{
    DevINSGPSInfoDbusNotifyC notify;
    if(flag == 0x00)
    {
        if(buf[2] == 0x60)
        {
            memset(notify.distribute, 0, 8);
            memcpy(notify.distribute, buf+6, 8);
            notify.type = 0x60;
            dev_DbusServer->sendNotify(&notify);
            printf("distribute=%s\n",notify.distribute);
            if(insgps_begin == 0)
            {
                insgps_begin = 1;
                pthread_create(&insgps_tid, NULL,send_insgps_position_info_thread, NULL);
            }
        }
        else if(buf[2] == 0x59)
        {
        
            memset(notify.distribute, 0, 8);
            memset(notify.actual, 0, 8);
            memcpy(notify.distribute, buf+6, 8);
            memcpy(notify.actual, buf+14, 8);
            notify.type = 0x59;
            dev_DbusServer->sendNotify(&notify);
        }
        else if(buf[2] == 0x57)
        {
            if(insgps_begin == 2)
                insgps_begin = 1;
            printf(" insgps 0x57 over\n");
        }
    }

    if(buf[2] == 0x58)
    {
        if(insgps_begin != 0)
        {
            pthread_cancel(insgps_tid);
            insgps_begin = 0;
        }
    }
}
int main(int argc, char * argv [ ])
{
    int status;
    DBusApplicationC app(argc, argv);
    
	tirednotify=new DeviceTiredDbusNotifyC();
    pthread_t drv_tid, ndrv_tid;
    pthread_t read_cpu;
    pthread_t read_mcu;
    
    signal(SIGUSR1,SignalHandler);
    signal(SIGPIPE,SignalHandler);

    status = workq_init (&workq_pretired, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work pretired queue");
    status = workq_init (&workq_tired, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work tired queue");
    status = workq_init (&workq_runst, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work runst queue");
    status = workq_init (&workq_locst, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work locst queue");
    status = workq_init (&workq_accident, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work accident queue");
    status = workq_init (&workq_dayinfo, 1, engine_routine);
    if (status != 0)
        err_abort (status, "Init work dayinfo queue");

    memset(&pretired_param,0,sizeof(struct PRETIRED_DRIVING));
    memset(&pretired_param_temp,0,sizeof(struct PRETIRED_DRIVING));

    memset(&speed_distance,0,sizeof(struct SpeedMileageInfo));
    memset(&speed_info,0,sizeof(struct SpeedStatusJudge));
    memset(&gps_info,0,sizeof(struct NaviGPSInfo));
    memset(&norm_info,0,sizeof(struct NaviGPSInfo));
    memset(&min_info,0,sizeof(struct MinSpeedLocInfo));
    memset(&gui_data,0,sizeof(struct BaseData));

    pthread_mutex_init(&udp_mutex, NULL);
    pthread_mutex_init(&gps_mutex, NULL);
    pthread_mutex_init (&engine_list_mutex_run, NULL);
    pthread_mutex_init (&engine_list_mutex_loc, NULL);

    dev_DbusServer= new DBusServerC(DEVICE_MANAGER_DBUS_SERVER_ID, DEVICE_MANAGER_DBUS_SERVER_OBJ);
    dev_DbusServer->registerService(REQ_ID_REBOOT_MACHINE,RebootMachineHandler,RebootMachineReqC::factory);
    dev_DbusServer->registerService(REQ_ID_SHUTDOWN_MACHINE,ShutdownMachineHandler,ShutdownMachineReqC::factory);
    dev_DbusServer->registerService(REQ_ID_CHECK_TIME,CheckTimeHandler,CheckTimeReqC::factory);
    dev_DbusServer->registerService(REQ_ID_CARRY_STATUS,CarryStatusHandler,CarryStatusReqC::factory);
    dev_DbusServer->registerService(REQ_ID_SPEED_RATIO_UPDATE,SpeedPluseFactorChangeHandler,SpeedRatioUpdateReqC::factory);
    dev_DbusServer->registerService(REQ_ID_GPS_SPEED_REPLACE_VDR_SPEED,GpsSpeedReplaceVDRSpeedHandler,GPSReplaceVDRSpeedReqC::factory);
    dev_DbusServer->registerService(REQ_ID_WIRELESS_INTENSITY,WirelessIntensityHandler,WirelessIntensityReqC::factory);
    dev_DbusServer->registerService(REQ_ID_GUI_EMERGENCY_ALARM,EmergencyAlarmReqHandler,EmergencyAlarmReqC::factory);
    dev_DbusServer->registerService(REQ_ID_DEV_COMMON_EVENT,DevCommonEventReqHandler,DevCommonEventReqC::factory);
    dev_DbusServer->registerService(REQ_ID_INSGPS_INFO_ANSWER,INSGPSInfoAnswerReqHandler,DevINSGPSAnswerReqC::factory);
    if(dev_DbusServer->connect() < 0)
    {
        printf("Cann't connect to device manager DBUS server.\n");
        return -1;
    }

	sp_ClientGps = new DBusClientC((char*)"com.YunTu.Navi", (char*)"/Navi");
	sp_ClientGps->registerReply(REP_ID_NAVI_ENCRYPTINFO,CNaviEncryptInfoRepC::factory);
	//sp_ClientGps->registerNotify(NOTF_ID_NAVI_GPSINFO_REPORT,CNaviGPSInfoReportNotifyHandler,CNaviGPSInfoReportNotifyC::factory);
	sp_ClientGps->registerNotify(NOTF_ID_NAVI_ELECTRONIC_FENCE_STATE,CNaviElectronicFenceStatetNotifyHandler,CNaviElectronicFenceStateNotifyC::factory);

    if(sp_ClientGps ->connect() < 0)
    {
        printf("Cann't connect to gps DBUS server.\n");
        return -1;
    }
    
	sp_ClientJT808 = new DBusClientC(JT808_DBUS_SERVER_ID, JT808_DBUS_SERVER_OBJ);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_LOC_REPORT,JT808LocReportDbusNotifyHandler,JT808LocReportDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_TEMP_LOC_REPORT,JT808TempLocReportDbusNotifyHandler,JT808TempLocReportDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_SETTING_UPDATE,JT808SettingUpdateDbusNotifyHandler,JT808SettingUpdateDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_PARKING_TEST, JT808ParkingTestDbusNotifyHandler,JT808ParkingDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_DATA_TRANSMIT, JT808DataTransmitDbusNotifyHandler,JT808DataTransmitNotifyC::factory);
	sp_ClientJT808->registerNotify(NOTF_ID_NAVI_GNSS_ATTR_CHANGE, JT808GNSSChangeDbusNotifyHandler,CGNSSAttrChangeNotifyC::factory);
    sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_AUTHENTICATION, JT808AuthenticationDbusNotifyHandler,JT808AuthenticationDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_LOCTIME, JT808LocTimeDbusNotifyHandler,JT808LocTimeDbusNotifyC::factory);
	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_ACCOFF, JT808AccOffDbusNotifyHandler,JT808AccOffDbusNotifyC::factory);

	sp_ClientJT808->registerNotify(JT808_DBUS_NTF_ID_DREG_COMMON, JT808DregCommonNotifyHandler,JT808DregCommonNotifyC::factory);

    if(sp_ClientJT808->connect()<0)
    {
        printf("Cann't connect to jt808 DBUS server.\n");
        return -1;
    }

    sp_ClientJT8082 = new DBusClientC(JT8082_DBUS_SERVER_ID, JT8082_DBUS_SERVER_OBJ);
    sp_ClientJT8082->registerNotify(JT8082_DBUS_NTF_ID_LOC_REPORT,JT808LocReportDbusNotifyHandler,JT8082LocReportDbusNotifyC::factory);
    sp_ClientJT8082->registerNotify(JT8082_DBUS_NTF_ID_TEMP_LOC_REPORT,JT808TempLocReportDbusNotifyHandler,JT8082TempLocReportDbusNotifyC::factory);
    sp_ClientJT8082->registerNotify(JT8082_DBUS_NTF_ID_SETTING_UPDATE,JT808SettingUpdateDbusNotifyHandler,JT8082SettingUpdateDbusNotifyC::factory);
    sp_ClientJT8082->registerNotify(JT8082_DBUS_NTF_ID_PARKING_TEST, JT808ParkingTestDbusNotifyHandler,JT8082ParkingDbusNotifyC::factory);
    sp_ClientJT8082->registerNotify(JT8082_DBUS_NTF_ID_DATA_TRANSMIT, JT808DataTransmitDbusNotifyHandler,JT8082DataTransmitNotifyC::factory);

    if(sp_ClientJT8082->connect()<0)
    {
        printf("Cann't connect to jt808 DBUS server.\n");
        return -1;
    }

//	LogUtility::SetStdoutLevel(LOG_LEVEL_ERROR);
    serial_protocol_init();
    read_deploy_para_table_set_pluse();
    read_deploy_status_config_table();
    
    statustimer = CreateTimer("speedstatus",300*1000,(void *)STATUS_TIMER_DATA,car_start_stop_run_handler, false);
    parktimer  = CreateTimer("parking",    1000, (void *)PARK_TIMER_DATA,  overtime_overspeed_alarm_handler, false);
    sleeptimer = CreateTimer("sleeping",   1000, (void *)SLEEP_TIMER_DATA, overtime_overspeed_alarm_handler, false);
    linetimer  = CreateTimer("lineruntime",1000, (void *)LINE_TIMER_DATA,  overtime_overspeed_alarm_handler, false); 
    speedtimer = CreateTimer("overspeed",  1000, (void *)SPEED_TIMER_DATA, overtime_overspeed_alarm_handler, false);
    uploadtimer = CreateTimer("upposition",1000, (void *)UPLOAD_TIMER_DATA, report_position_handler, true); 
    tmpuploadtimer = CreateTimer("tmpupposition",1000, (void *)TUPLOAD_TIMER_DATA, report_position_handler, false); 
    mcutimer = CreateTimer("readmcu", 1000, (void *)MCU_TIMER_DATA, read_mcu_data_handler, true);
    picturetimer = CreateTimer("picture", 5000, (void *)PICTURE_TIMER_DATA, take_picture_control_handler, true);
    naviencrypttimer = CreateTimer("naviencrypt", 180*1000, (void *)NAVI_ENCRYPT_DATA, get_navi_encrypt_status_handler, true);
    vsstimer = CreateTimer("vssalarm", 60*1000, (void *)VSSALARM_TIMER_DATA, car_start_stop_run_handler, false);
    ignitiontimer = CreateTimer("ignition", 600*1000, (void *)IGNITION_TIMER_DATA, car_start_stop_run_handler, false);

    udp_socket_recv();
    send_speed_udp_socket();

    read_vdr_run_param_table();
    if(vdr_param.gnss_locate_mode != 3)
        send_change_gnss_mode_udp(&vdr_param.gnss_locate_mode, 4);
    open_loc_0809_file();
    read_vdr_day_info_table();
    read_vdr_off_status_table();
    read_track_speed_info_table();  
    read_base_config_table();
    read_startingup_info_thread();
    I2c_read_startup_mode_data(startup_mode, 1);
    read_d310_dreg_info_table();
    
    if(vdr_alarm_flag & ALARM_STATUS_EMERGENCY_ALARM) 
        change_report_time_distance_mode(ALARM_MODE);
    else
        change_report_time_distance_mode(DEFAULT_MODE);

    speed_fd = open("/dev/speed", O_RDONLY);
	if(!speed_fd)
		printf("open /dev/speed err\n");
    
    //StartTimer(naviencrypttimer);
    StartTimer(mcutimer);

    pthread_create(&drv_tid, NULL, driver_login_info_thread, NULL);
    
    pthread_mutex_init(&unbuf_mutex, NULL);
    pthread_create(&read_cpu, NULL, read_speed_from_cpu_thread, NULL);
    open_08H_09H_file();

    //pthread_create(&read_mcu, NULL, read_caninfo_from_mcu_thread, NULL);
    pthread_create(&ndrv_tid, NULL,read_serial_IC_card_thread, NULL);

    return app.loop();
}
