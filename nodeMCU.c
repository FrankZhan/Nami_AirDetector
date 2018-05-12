#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include<SoftwareSerial.h>
#include<ESP8266HTTPClient.h>
#include<ArduinoJson.h>

/*
 * ESP8266HTTPClient.h
* 程序开发工具Arduino 1.8.4
* 开发板 nodeMCU
* 不清楚发送的数据格式
* 不清楚接受的时间格式
*/
WiFiUDP Udp;
IPAddress appUdpIP(255, 255, 255, 255);
SoftwareSerial stm32(D7, D8);   //stm32的串口
unsigned int localUdpPort = 7001;  // local port to listen on
unsigned int appUdpPort = 18266;  //target port to send reply packet
boolean IFWIFI;   // 是否连接到了WIFI
char tim[20] = "201805081600";     // 初试时间, eg:201804281600
char data[200] = "{ \"CarbonDioxide\":56, \"Formaldehyde\":12, \"ParticlePollutionTwoPointFive\":5, \"Temperature\":25, \"Humidity\":43 }" ; // 保存传感器数据;
char severIP[100];  //保存服务器IP
char token[500];  // 保存用户令牌， Air-Token
char mac_char[18];  //MCU Mac地址，DeviceID
int count = 0;       // 传感器数据计数

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);
    stm32.begin(9600);
    Serial.print( "\n" );   
    IFWIFI = false;
    smartConfig();
    sendTime();
    if(IFWIFI){     
      stm32.write("W1");
    }else{
      stm32.write("W0");
    }
}

void loop() {

   if(IFWIFI){
      //读取 格式为 { ... ... }的数据
      int index;
      while (stm32.available() > 0) {
        index = 0;
        char tmp = stm32.read();
        if(tmp == '{'){
          data[index++]=tmp;
          do{
            if(stm32.available()){
              tmp = stm32.read();
              data[index++]=tmp;
            }
          }while(tmp!='}');
          break;
        }
      }
      data[index] = '\0';
      if(index > 0){
        Serial.print("\nreceive sensor data:");
        Serial.print(data);
        Serial.print("\t");
        index--;
        data[index] = '\0';
        postData();
      }   
   }
   count++;
   // 循环二十次发一次时间
   if(count > 20){
       getTime();
       count = 0;
   }
   digitalWrite(LED_BUILTIN, HIGH); 
   delay(500);
   digitalWrite(LED_BUILTIN, LOW);
   delay(500);
}
// 从服务器获取时间
void getTime(){
  if(IFWIFI){
    char url[100];
    char result[100];
    int point = 0;
    sprintf(url, "/api/time", severIP);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if(httpCode > 0){
      Serial.printf("Http Get code: %d \n", httpCode);
      if(httpCode == HTTP_CODE_OK){
        String result = http.getString();
        Serial.println(result);
        
        // obtain the json msg
        StaticJsonBuffer<200> jsonBuf;
        JsonObject& root = jsonBuf.parse(result);
        if(root.success()){
          strcpy(tim, root["time"]);
          formatTime();
          sendTime();
         }
       }
    }else{
      Serial.printf("Http Get failed");
    }
    http.end();    
  }
}
//            0123456789012345
// 格式化时间 2018-05-11 19:30-》
//            201805111930
void formatTime(){
  char tmp[20];
  strcpy(tmp, tim);
  tim[4]=tmp[5];
  tim[5]=tmp[6];
  tim[6]=tmp[8];
  tim[7]=tmp[9];
  tim[8]=tmp[11];
  tim[9]=tmp[12];
  tim[10]=tmp[14];
  tim[11]=tmp[15];
  tim[12]=0;
}
// 把时间发送给32
void sendTime(){
  //把wifi连接结果告知32
   char tmp[15];
   sprintf(tmp, "T%s", tim);
   Serial.printf("Time is %s", tmp);
   stm32.write(tmp);
}
// 上传数据到服务器。注意设置上传间隔
void postData(){
  
  if(data == NULL){
    return;
  }
  char tmp[200];
  sprintf(tmp, "{\"PushStatistics\": %s, \"DeviceID\": \"%s\"} }", data, mac_char);
  if(IFWIFI){
    char url[100];
    sprintf(url, "%s/api/pipe", severIP);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Air-Token", token);
    int httpCode = http.POST(tmp);
    if(httpCode > 0){
      Serial.printf("Http Post code: %d \n", httpCode);
      String result = http.getString();
      Serial.println(result);
    }else{
        Serial.printf("Http Post failed");
    }
    http.end();
    Serial.print("upload url: ");
    Serial.println(url);
    Serial.println("Send data: ");
    Serial.println(tmp);
  }
}

//**接受App发送的Udp包，获取用户信息并返回ACK */
boolean receiveUdp(){
  char incomingPacket[1000];  // buffer for incoming packets
  char  replyPacket[100];    // a reply string to send back, json字符串，表示成功配对
  sprintf(replyPacket, "{'smartConfig':101}");
  int packetSize = Udp.parsePacket();
  if (packetSize){
    // receive incoming UDP packets
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    int len = Udp.read(incomingPacket, 1000);
    if (len > 0){
      incomingPacket[len] = 0;
    }
    Serial.printf("UDP packet contents: %s, Send Reply Packet: %s\n", incomingPacket, replyPacket);
    // send back a reply, to the IP address and port we got the packet from
    Udp.beginPacket(Udp.remoteIP(), appUdpPort);
    Udp.write(replyPacket);
    Udp.endPacket();

    // obtain the json msg
    StaticJsonBuffer<500> jsonBuf;
    JsonObject& root = jsonBuf.parse(incomingPacket);
    if(root.success()){
      strcpy(token, root["token"]);
      strcpy(tim, root["time"]);
      strcpy(severIP, root["severIP"]);
    }
    return true;
  }
  return false;
}

//**通过广播包发送本机mac地址和IP到APP端*/
void sendACK(){
  //**通过UDP广播的方式把本机MAC地址发送给APP**//
  byte mac[6];
  char data[200];
  
  Udp.begin(localUdpPort);
  WiFi.macAddress(mac);

  int index=0;
  for(int i=5;i>=0;i--){
    byte tmp = mac[i];
    mac_char[index++] = int2hex(tmp/16);
    mac_char[index++] = int2hex(tmp%16);
    mac_char[index++] = ':';
  }
  mac_char[17]=0;
  
  sprintf(data, "{'BSSID':'%s', 'IP':'%s'}", mac_char, WiFi.localIP().toString().c_str());
  Serial.println(data);
  
  Udp.beginPacket(appUdpIP, appUdpPort);
  Udp.write(data);
  Udp.endPacket();
  Udp.stop();
}
//**把十进制int转换为十六进制字符*/
char int2hex(int a){
  if( a<10){
    return a+'0';
  }else{
    return a-10+'A';
  }
}

//** 连接到WiFi 并接受APP发送过来的时间、服务器IP等信息*/
void smartConfig(){  
    // 設定模式為 STA ( MODE 只能是 STA ) 才能使用 smartconfig
    WiFi.mode( WIFI_STA );
    if( WiFi.SSID() = "" ) {
        Serial.print( "Begining Smart Config\n" );
        WiFi.beginSmartConfig();
        int count = 0;
        while( WiFi.status() != WL_CONNECTED ) {
            Serial.print( "." );
            delay( 500 );
            count++;
            if( WiFi.smartConfigDone() ) {
                Serial.println( "Done!" );
                WiFi.stopSmartConfig();
                IFWIFI = true;
                break;
            }
            // 超时设置60个循环，即30S
            if(count >= 60){
                IFWIFI = false;
                Serial.print("SamrtConfig is timeout!\n");
                break;
            }
        }
    } else {
            Serial.print( "Using SSID： ");
            Serial.println( WiFi.SSID() );
    }
    //如果SmartConfig成功
    if(IFWIFI == true){
      // 连接至ESO-touch所输入的SSID 和 PASSWORD
       printf( "Connecting.." );
       WiFi.begin();
       while( WiFi.status() != WL_CONNECTED ) {
           Serial.print( "." );
           delay( 500 );
        }
        Serial.println( "OK" );
        // 顯示連線知道所分配的 local IP 位址
        Serial.print( "\nLocal IP: ");
        Serial.println( WiFi.localIP() );
        //发送局域网广播包，广播MAC地址和IP
        Udp.begin(localUdpPort);  
        delay(1000);
        sendACK(); 
        Serial.printf("Now listening at IP %s, UDP port %d\n", WiFi.localIP().toString().c_str(), localUdpPort);
        Udp.begin(localUdpPort);
        int count = 0;
        while(!receiveUdp()){
            delay(500);
            count++;
            if(count >= 60){
              IFWIFI = false;
              Serial.print("SmartConfig:receiveUdp is Timeout!");
              break;
            }
        };
        Udp.stop();
    }
}
