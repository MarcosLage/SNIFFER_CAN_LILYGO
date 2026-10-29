#include <Arduino.h>
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_timer.h"
// Owner-supplied pinout: XY_32_CAN+RS485 V1.1 (2024-04-25).
constexpr int RX_PIN=26, TX_PIN=27, SE_PIN=23;
struct Record { int64_t us; twai_message_t m; };
QueueHandle_t queue;
SemaphoreHandle_t lockRx;
bool active=false, installed=false;
uint32_t bitrate=250000;
int64_t epoch=0;
uint64_t received=0, lost=0, written=0;
String input;
bool overflow=false;
// Use IDF GPIO API to preload the output latch before enabling the output.
// Arduino ESP32 3.x digitalWrite before pinMode is ignored.
void disableTransceiver() { gpio_set_level((gpio_num_t)SE_PIN,1); }
void parkCanPins() {
  gpio_reset_pin((gpio_num_t)SE_PIN);
  gpio_set_level((gpio_num_t)SE_PIN,1);
  gpio_set_direction((gpio_num_t)SE_PIN,GPIO_MODE_OUTPUT);
  gpio_reset_pin((gpio_num_t)TX_PIN);
  gpio_set_level((gpio_num_t)TX_PIN,1);
  gpio_set_direction((gpio_num_t)TX_PIN,GPIO_MODE_OUTPUT);
}
bool timing(uint32_t b, twai_timing_config_t &t) {
  switch(b) {
    case 125000: t=TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 250000: t=TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 500000: t=TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 1000000: t=TWAI_TIMING_CONFIG_1MBITS(); return true;
    default: return false;
  }
}
void rxTask(void*) {
  for(;;) {
    xSemaphoreTake(lockRx,portMAX_DELAY);
    Record r;
    bool got=active && twai_receive(&r.m,0)==ESP_OK;
    if(got) {
      r.us=esp_timer_get_time()-epoch; ++received;
      if(xQueueSend(queue,&r,0)!=pdTRUE) ++lost;
    }
    xSemaphoreGive(lockRx);
    if(!got) vTaskDelay(1);
  }
}
void outputOne() {
  Record r;
  if(xQueueReceive(queue,&r,0)!=pdTRUE) return;
  char data[17]={}, line[100];
  unsigned n=r.m.rtr?0:min((unsigned)r.m.data_length_code,8U);
  for(unsigned i=0;i<n;++i) snprintf(data+2*i,3,"%02X",r.m.data[i]);
  snprintf(line,sizeof(line),"%lld,%0*lX,%u,%u,%u,%s\n",
    (long long)r.us,r.m.extd?8:3,(unsigned long)r.m.identifier,
    (unsigned)r.m.extd,(unsigned)r.m.rtr,(unsigned)r.m.data_length_code,data);
  Serial.print(line); ++written;
}
void status() {
  twai_status_info_t s={};
  xSemaphoreTake(lockRx,portMAX_DELAY);
  if(installed) twai_get_status_info(&s);
  uint64_t rx=received,drop=lost;
  xSemaphoreGive(lockRx);
  Serial.printf("#STATUS,us=%lld,running=%u,bitrate=%lu,received=%llu,written=%llu,app_dropped=%llu,driver_missed=%lu,hw_overrun=%lu,bus_errors=%lu,rec=%lu,tec=%lu,pending_driver=%lu,pending_app=%u\n",
    (long long)(epoch?esp_timer_get_time()-epoch:0),(unsigned)active,(unsigned long)bitrate,
    (unsigned long long)rx,(unsigned long long)written,(unsigned long long)drop,
    (unsigned long)s.rx_missed_count,(unsigned long)s.rx_overrun_count,
    (unsigned long)s.bus_error_count,(unsigned long)s.rx_error_counter,
    (unsigned long)s.tx_error_counter,(unsigned long)s.msgs_to_rx,(unsigned)uxQueueMessagesWaiting(queue));
}
void startCapture() {
  if(active) { Serial.println("#ERROR,already_running"); return; }
  xSemaphoreTake(lockRx,portMAX_DELAY);
  disableTransceiver();
  if(installed) { twai_driver_uninstall(); installed=false; }
  parkCanPins();
  twai_timing_config_t t; timing(bitrate,t);
  twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN,(gpio_num_t)RX_PIN,TWAI_MODE_LISTEN_ONLY);
  g.tx_queue_len=0; g.rx_queue_len=256;
  twai_filter_config_t f=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t e=twai_driver_install(&g,&t,&f);
  installed=e==ESP_OK;
  xQueueReset(queue); received=lost=written=0; epoch=esp_timer_get_time();
  Serial.printf("#SESSION,format=1,board=XY_32_CAN+RS485,revision=V1.1,pcb_date=2024-04-25,firmware=0.1.1,bitrate=%lu,mode=listen_only,timestamp=task_dequeue_us\n",(unsigned long)bitrate);
  Serial.println("timestamp_us,id_hex,extended,rtr,dlc,data_hex");
  if(e==ESP_OK) e=twai_start();
  active=e==ESP_OK;
  if(active) gpio_set_level((gpio_num_t)SE_PIN,0);
  else disableTransceiver();
  xSemaphoreGive(lockRx);
  Serial.printf("#%s,code=%d\n",active?"START":"ERROR",(int)e);
}
void stopCapture() {
  xSemaphoreTake(lockRx,portMAX_DELAY);
  if(!active) { xSemaphoreGive(lockRx); Serial.println("#ERROR,not_running"); return; }
  disableTransceiver();
  twai_stop();
  Record r;
  while(twai_receive(&r.m,0)==ESP_OK) {
    r.us=esp_timer_get_time()-epoch; ++received;
    if(xQueueSend(queue,&r,0)!=pdTRUE) ++lost;
  }
  active=false; xSemaphoreGive(lockRx);
  while(uxQueueMessagesWaiting(queue)) outputOne();
  status(); Serial.println("#STOP");
}
void execute(String c) {
  c.trim();
  if(c=="START") startCapture();
  else if(c=="STOP") stopCapture();
  else if(c=="STATUS") status();
  else if(c.startsWith("BITRATE ")) {
    String v=c.substring(8); uint32_t b=v.toInt(); twai_timing_config_t t;
    if(active || String(b)!=v || !timing(b,t)) Serial.println("#ERROR,stop_first_or_invalid_bitrate");
    else { bitrate=b; Serial.printf("#BITRATE,%lu\n",(unsigned long)b); }
  } else if(c.startsWith("MARK ")) {
    if(!active) { Serial.println("#ERROR,mark_requires_running"); return; }
    String label=c.substring(5); label.replace("\"","\"\"");
    Serial.printf("#EVENT,%lld,\"%s\"\n",(long long)(esp_timer_get_time()-epoch),label.c_str());
  } else Serial.println("#ERROR,use_START_STOP_STATUS_BITRATE_or_MARK");
}
void setup() {
  parkCanPins();
  Serial.begin(921600);
  queue=xQueueCreate(2048,sizeof(Record)); lockRx=xSemaphoreCreateMutex();
  if(!queue || !lockRx || xTaskCreatePinnedToCore(rxTask,"can_rx",4096,nullptr,4,nullptr,0)!=pdPASS) {
    Serial.println("#ERROR,allocation_failed"); for(;;) delay(1000);
  }
  Serial.println("#READY,firmware=0.1.1,baud=921600,transceiver=sleep,send=START");
}
void loop() {
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='\n') {
      if(overflow) Serial.println("#ERROR,command_too_long");
      else if(input.length()) execute(input);
      input=""; overflow=false;
    } else if(c!='\r') {
      if(input.length()<180 && !overflow) input+=c; else overflow=true;
    }
  }
  for(int i=0;i<32 && uxQueueMessagesWaiting(queue);++i) outputOne();
  static uint32_t last=0;
  if(active && millis()-last>=5000) { last=millis(); status(); }
  delay(1);
}

