
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "../mad/mad.h"
#include "../mad/stream.h"
#include "../mad/frame.h"
#include "../mad/synth.h"

#include "../socket_server/socket_server.h"
#include "../spiraw/spiraw.h"
#include "../sound/sound.h"

#include "playerconfig.h"

#include "gpio.h"
#include "key.h"
#include "gsensor.h"
#include "spimic.h"

static long bufUnderrunCt;
static char buffer[MAX_READ_SIZE];

void ICACHE_FLASH_ATTR
key_pa_init()
{
    pa_init();
    key_all_init();
}

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

/******************************************************************************
 * FunctionName : MP3 - MAD Decoder
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/

//Reformat the 16-bit mono sample to a format we can send to I2S.
static int ICACHE_FLASH_ATTR
sampToI2s(short s) {
    //We can send a 32-bit sample to the I2S subsystem and the DAC will neatly split it up in 2
    //16-bit analog values, one for left and one for right.
    
    //Duplicate 16-bit sample to both the L and R channel
    int samp=s;
    samp=(samp)&0xffff;
    samp=(samp<<16)|samp;
    return samp;
}

 //Array with 32-bit values which have one bit more set to '1' in every consecutive array index value
const unsigned int ICACHE_RODATA_ATTR fakePwm[]={ 0x00000010, 0x00000410, 0x00400410, 0x00400C10, 0x00500C10, 0x00D00C10, 0x20D00C10, 0x21D00C10, 0x21D80C10,
    0xA1D80C10, 0xA1D80D10, 0xA1D80D30, 0xA1DC0D30, 0xA1DC8D30, 0xB1DC8D30, 0xB9DC8D30, 0xB9FC8D30, 0xBDFC8D30, 0xBDFE8D30,
    0xBDFE8D32, 0xBDFE8D33, 0xBDFECD33, 0xFDFECD33, 0xFDFECD73, 0xFDFEDD73, 0xFFFEDD73, 0xFFFEDD7B, 0xFFFEFD7B, 0xFFFFFD7B,
    0xFFFFFDFB, 0xFFFFFFFB, 0xFFFFFFFF};
#ifdef PWM_HACK

static int ICACHE_FLASH_ATTR
sampToI2sPwm(short s) {
    //Okay, when this is enabled it means a speaker is connected *directly* to the data output. Instead of
    //having a nice PCM signal, we fake a PWM signal here.
    static int err=0;
    int samp=s;
    samp=(samp+32768);    //to unsigned
    samp-=err;            //Add the error we made when rounding the previous sample (error diffusion)
    //clip value
    if (samp>65535) samp=65535;
    if (samp<0) samp=0;
    //send pwm value for sample value
    samp=fakePwm[samp>>11];
    err=(samp&0x7ff);    //Save rounding error.
    return samp;
}
#endif

//This routine is called by the NXP modifications of libmad. It passes us (for the mono synth)
//32 16-bit samples.
void ICACHE_FLASH_ATTR
render_sample_block(short *short_sample_buff, int no_samples) {
    //Signed 16.16 fixed point number: the amount of samples we need to add or delete
    //in every 32-sample
    static int sampAddDel=0;
    //Remainder of sampAddDel cumulatives
    static int sampErr=0;
    int i;
    int samp;
    
#ifdef ADD_DEL_SAMPLES
    sampAddDel=recalcAddDelSamp(sampAddDel);
#endif
    
    sampErr+=sampAddDel;
    for (i=0; i<no_samples; i++) {
		
		short tmp = short_sample_buff[i];
        short_sample_buff[i] = tmp >> (10 - mp3_volume);	
		
#if defined(PWM_HACK)
        samp=sampToI2sPwm(short_sample_buff[i]);
#elif defined(DELTA_SIGMA_HACK)
        samp=sampToI2sDeltaSigma(short_sample_buff[i]);
#else
        samp=sampToI2s(short_sample_buff[i]);
#endif
        //Dependent on the amount of buffer we have too much or too little, we're going to add or remove
        //samples. This basically does error diffusion on the sample added or removed.
        if (sampErr>(1<<24)) {
            sampErr-=(1<<24);
            //...and don't output an i2s sample
        } else if (sampErr<-(1<<24)) {
            sampErr+=(1<<24);
            //..and output 2 samples instead of one.
            i2sPushSample(samp);
            i2sPushSample(samp);
        } else {
            //Just output the sample.
            i2sPushSample(samp);
        }
    }
}

//Called by the NXP modificationss of libmad. Sets the needed output sample rate.
static oldRate=0;
void  ICACHE_FLASH_ATTR
set_dac_sample_rate(int rate) {
    if (rate==oldRate) return;
    oldRate=rate;
    //printf("Rate %d\n", rate);
    
#ifdef ALLOW_VARY_SAMPLE_BITS
    i2sSetRate(rate, 0);
#else
    i2sSetRate(rate, 1);
#endif
}


static void ICACHE_FLASH_ATTR
test_play()
{
	int i;
    int low;
    int high;
    int samp;
    char *test_buffer;
	
	test_buffer = (char *)fakePwm;
	
	for (i = 0; i < 64; i += 2) {
		low  = test_buffer[i];
		high = test_buffer[i+1];
		samp = (high << 8) | low;
		//printf("%x\n", samp);
 	    i2sPushSample(samp);
	}
}

uint16 gx, gy, gz;
uint16 px, py, pz;
os_timer_t i2c_timer;
int still_count = 0, mcount = 0;
int music_stop = 0;
int thresh = 120;

LOCAL void ICACHE_FLASH_ATTR 
i2c_timer_cb(void *arg) {
	
    user_gsensor_read_x(&gx);
    user_gsensor_read_y(&gy);
    user_gsensor_read_z(&gz);
	
	if (px == 0 && py == 0 && pz == 0) {
		px = gx;
		py = gy;
		pz = gz;
		return;
	} else {
        if ((abs(gx - px) + abs(gy - py) + abs(gz - pz)) < thresh) {
    	    if ((abs(gx - px) < thresh && abs(gy - py) < thresh && abs(gz - pz) < thresh)) {
		        still_count++;
		        if (still_count > 200) {
				    //player_suspend();			
				    music_stop  = 1;
				    still_count = 0;
		        } 	
    	    }
	    } else {
			if (music_stop == 1) {
				//player_resume();
				music_stop = 0; 				 
			}		
			still_count = 0;			 
    	}	
		px = gx;
		py = gy;
		pz = gz;
	}
}

void ICACHE_FLASH_ATTR
test_gsensor(void)
{
	px = py = pz = 0;
	gx = gy = gz = 0;
    os_timer_disarm(&i2c_timer);
    os_timer_setfn(&i2c_timer, i2c_timer_cb, NULL); /* Set callback for timer */
    os_timer_arm(&i2c_timer, 500 /* call every 50ms */, 1 /* repeat */);
}

void ICACHE_FLASH_ATTR
start_spi_sound(void)
{
    sound_init();
    sound_play();
}

void ICACHE_FLASH_ATTR
user_set_softap_config(void)
{
   struct softap_config config;

   wifi_softap_get_config(&config); // Get config first.
   
   memset(config.ssid, 0, 32);
   memset(config.password, 0, 64);
   memcpy(config.ssid, "ESP8266", 7);
   memcpy(config.password, "12345678", 8);
   config.authmode        = AUTH_WPA_WPA2_PSK;
   config.ssid_len        = 0;// or its actual length
   config.beacon_interval = 3000;
   config.max_connection  = 4; // how many stations can connect to ESP8266 softAP at most.

   wifi_softap_set_config(&config);// Set ESP8266 softap config .
   
}

void ICACHE_FLASH_ATTR
user_start_dhcpd(void)
{
    wifi_softap_dhcps_stop(); // disable soft-AP DHCP server
    struct ip_info info;
	
    IP4_ADDR(&info.ip, 192, 168, 5, 1); // set IP
    IP4_ADDR(&info.gw, 192, 168, 5, 1); // set gateway
    IP4_ADDR(&info.netmask, 255, 255, 255, 0); // set netmask
    wifi_set_ip_info(SOFTAP_IF, &info);
	
    struct dhcps_lease dhcp_lease;
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 5, 100);
    IP4_ADDR(&dhcp_lease.end_ip,   192, 168, 5, 105);
    wifi_softap_set_dhcps_lease(&dhcp_lease);
    wifi_softap_dhcps_start(); // enable soft-AP 
}

void ICACHE_FLASH_ATTR
run_socket_server(void)
{
	//wifi_set_opmode(STATIONAP_MODE);
	wifi_set_opmode(SOFTAP_MODE);

    // ESP8266 softAP set config.
    user_set_softap_config();
	user_start_dhcpd();
 	start_socket_server(NULL);
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_init(void)
{
    key_pa_init();

    // disable WiFi
    //wifi_set_opmode_current(NULL_MODE);
    // disable DHCP server
    //wifi_softap_dhcps_stop();
 
    //uart_div_modify(0, UART_CLK_FREQ / 115200);
    //SET_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);
    //CLEAR_PERI_REG_MASK(UART_CONF0(0), UART_RXFIFO_RST | UART_TXFIFO_RST);	
	
    os_printf("Hello World! Ray!\n");
    os_printf("SDK version:%s\n", system_get_sdk_version());
	
    spiMicInit();
	//test_play();
    start_spi_sound();

    //run_socket_server();
}

