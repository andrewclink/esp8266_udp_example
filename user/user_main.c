#include <ets_sys.h>
#include <osapi.h>
#include <os_type.h>
#include <user_interface.h>

#include "def.h"
#include "driver/uart.h"

static connstate_t connection_state = connstate_disconnected;
static os_timer_t network_timer;


os_event_t uart_recv_queue[32];


void uart_recv_task(os_event_t * event)
{
  switch(event->sig)
  {
    case SIG_UART0_RX:
    {
      os_printf("r: %c\n", event->par);
      break;
    }

    default:
      break;
  }
  
  
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
}

void network_monitor(void)
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
            connection_state = connstate_connected;
          }
          break;
        }
      }
      
      break;
    }


    
    case connstate_connected:
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

void user_init(void)
{
  uart_init(BIT_RATE_115200, BIT_RATE_115200); 
  os_install_putc1((void *)uart0_write_char);
  
  os_delay_us(1000000);
  uart0_sendStr("\r\nBooting...\r\n");

  
  // Start network monitor, which will connect to the network
  network_monitor();
  
  // install uart receive task
  system_os_task(uart_recv_task, 0, uart_recv_queue, 32);
  
  
}
