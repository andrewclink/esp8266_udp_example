//
//  network
//
//  Created by Andrew Clink on 2014-12-26.
//  Copyright (c) 2014 Design Elements. All rights reserved.
//
#include <stdio.h>
#include <stdlib.h>
#include <osapi.h>
#include <user_interface.h>
#include <espconn.h>

#include "def.h"
#include "network.h"

#define network_task_queue_size 16
static os_event_t network_task_queue[network_task_queue_size]; 
static connstate_t connection_state = connstate_disconnected;  /// State of the attempt to remain connected
static os_timer_t network_timer;                               /// Timer to manage network_monitor


// Network Receive Callback
static void ICACHE_FLASH_ATTR udpNetworkRecvCb(void *arg, char *data, unsigned short len);


// Static functions
static void network_task(os_event_t * event);
static void network_monitor(void);



void network_init(void)
{
  // Start network monitor, which will connect to the network
  network_monitor();
  
  // Start the network task, which will be pumped when 
  // network_monitor detects a valid connection
  //
  system_os_task(&network_task, 1, network_task_queue, network_task_queue_size);
}

void network_connect(void)
{
  // Set these dynamically
  char ssid[32] = "debug1";
  char pass[64] = "andrewclink";
  
  // Configure wifi connection
  struct station_config config;
  os_memcpy(&config.ssid, ssid, 32);
  os_memcpy(&config.password, pass, 64);
  
  // Set station mode & load config
  wifi_set_opmode(1); // STA mode
  wifi_station_set_config(&config);
  
  connection_state = connstate_connecting;
  
}

static void network_monitor(void)
{
  static int tries = 0;
  struct ip_info ipconfig;
  
  os_timer_disarm(&network_timer);
  
  uint8_t station_state = wifi_station_get_connect_status();  // Get Layer 2 info
  wifi_get_ip_info(STATION_IF, &ipconfig);                    // Get Layer 3 info
  
  os_printf("network_monitor: connection: %d; station: %d\n", connection_state, station_state);
  
  // Check network state
  switch(connection_state)
  {
    default:
    case connstate_disconnected: 
    {
      // Connect
      network_connect();
      break;
    }
    
    
    case connstate_connecting:
    {
      // If the timer has triggered again despite having attempted to connect,
      // we are waiting for an IP. 
      //
      // If the station status is STATION_WRONG_PASSWORD, NO_AP_FOUND, or STATION_CONNECT_FAIL,
      // we have failed to connect and need to handle the error some other 
      // way (such as establishing a softAP)
      //
      // If the number of tries exceeds the threshold, obtaining an address has
      // timed out.
      
      switch(station_state)
      {
        default:
        case STATION_WRONG_PASSWORD:
        case STATION_NO_AP_FOUND:
        case STATION_CONNECT_FAIL:
        {
          os_printf("Could not connect: %d; Giving Up\n", station_state);
          return;
        }
        
        case STATION_CONNECTING:
        {
          // Simply wait. We're going to assume that the station will not
          // remain in this state indefinitely, so we won't increment tries.
          break;
        }
        
        case STATION_GOT_IP:
        {
          if (ipconfig.ip.addr != 0)
          {
            // Change state:
            // The state has progressed as such:
            // Disconnected -> Connecting -> Connected
            //
            os_printf("\nConnected!\n");
            connection_state = connstate_connected;
            
            system_os_post(1, sig_wifiConnected, 0);
          }
          break;
        }
      }
      
      break;
    }


    
    case connstate_connected:
    case connstate_socketsOK:
    {
      // Verify that the connection has been maintained.
      //
      if (STATION_GOT_IP != station_state)
      {
        os_printf("station_state = %d (not GOT_IP); reconnecting\n", station_state);
        connection_state = connstate_disconnected;
      }
      
      
      break;
    }
  }
  
  os_timer_setfn(&network_timer, (os_timer_func_t *)network_monitor, NULL);
  os_timer_arm(&network_timer, 1000, 0);
  
}

static void ICACHE_FLASH_ATTR udpNetworkRecvCb(void *arg, char *data, unsigned short len) 
{
  
}

static void network_task(os_event_t * event)
{
  static struct espconn connection;
  static esp_udp connection_udp;

  if (event->sig == sig_wifiConnected)
  {
    // Initialize sockets
    os_printf("Initializing sockets\n");
    connection.type=ESPCONN_UDP;                                    // We want to make a UDP connection
    connection.state=ESPCONN_NONE;                                  // Set default state to none
    connection.proto.udp = &connection_udp;
    connection.proto.udp->local_port=2023;                          // Set local port to 2222
    connection.proto.udp->remote_port=2023;                         // Set remote port
    connection.proto.udp->remote_ip[0]=225;                         // The other ESP8266 IP
    connection.proto.udp->remote_ip[1]=0;                           //   The other ESP8266 IP
    connection.proto.udp->remote_ip[2]=0;                           //   The other ESP8266 IP
    connection.proto.udp->remote_ip[3]=77;                          //   The other ESP8266 IP

    if(espconn_create(&connection) < 0) 
    {
      os_printf("Error creating connection\n");
      // @TODO: At this point, connection_state will not become connstate_socketsOK.
      // This needs to be detected and mitigated.
      return;
    }

    // espconn_regist_recvcb(&connection, udpNetworkRecvCb);
    os_printf("UDP connection set\n\r");
    connection_state = connstate_socketsOK;
  }
  
  if (connstate_socketsOK)
  {
    static uint8_t value = 128;
    uint8_t buffer[] = {0x03, 0x00, value++};
    
    espconn_sent(&connection, buffer, 3);
    os_delay_us(1000 * 20); 
    system_os_post(1, 0xff, 0); // Pump this task
  }
  
}

