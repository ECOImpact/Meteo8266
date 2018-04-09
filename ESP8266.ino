#include <Wire.h>
#include <BME280I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define TCAADDR 0x70

#define SEALEVELPRESSURE_HPA (1013.25)

#define ALL -1

#define SENSORS 8

BME280I2C bme[SENSORS];

bool status = false;
bool metric = true;

float temperature=0.0, humidity=0.0, pres=0.0;//, metric=0.0;
float SensorsData[SENSORS][3];

String SensorsHash[SENSORS] = {"hash_code",//1
                              "hash_code",//2
                              "hash_code",//3
                              "hash_code",//4
                              "hash_code",//5
                              "hash_code"//6
                              };

unsigned long delayTime = 1000;

String esid;
String epass;

String essid;
String epassword;

String content = "";


const char* www_username = "admin_user";
const char* www_password = "admin_pass";

const char* ssid = "WiFi_STA_SSID";;
const char* password = "WiFi_STA_PASSWORD";//"1nencum1";

const String sensorUrl = "endpoint";

const char* host = "host";
const int httpsPort = 443;
const char* fingerprint = "12 34 56 78 9a bc ef 01 23 45 67 89 ab cd ef 01 23 45 67 89";

const int wifiAttempts = 30;
const int sensorAttempts = 30;
const unsigned long connectionTimeOut = 10000;//in miliseconds
const unsigned long wifiTimeout = 600000;//in miliseconds
const unsigned long sensorTimeout = 10000;//in miliseconds

bool wifiStatus = false;
bool sensorStatus = false;
bool isSerial=true;

ESP8266WebServer server ( 80 );

void checkAuth();
void setupAP();
void POST(String data,String url);
void DetectSensors(int sensor);
void settings();
void reboot();
void root();
String PrepareData(float temperature,float humidity,float pressure);
void SendData(float source[][3]);

//For Multiplexer
void tcaselect(uint8_t i) {
  if (i > 7) return;
 
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}

void setup()
{
  if (isSerial)
  {
    Serial.begin(9600);
  }

  EEPROM.begin(512);
  
  for (int i = 0; i < 32; ++i)
  {
     esid += char(EEPROM.read(i));
  }

  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }

  Serial.println(esid);
  Serial.println(epass);

  essid = String(esid);
  epassword = String(epass);

  WiFi.mode(WIFI_STA);
  WiFi.begin(essid.c_str(),epassword.c_str());
  
  for (int i = 0;i<wifiAttempts;i++)
  {
    Serial.print ( "." );
    if(WiFi.status() == WL_CONNECTED)
    {
      wifiStatus = true;
      break;
    }
    delay ( 500 );
  }

  if (wifiStatus==false)
  {
    Serial.println("Setup AP");
    setupAP();
  }

  server.on ("/",root);
  server.on ("/settings",settings);
  server.on ("/reboot", reboot);
     
  server.begin();


  ArduinoOTA.setPassword((const char *)"OTA_PASSWORD");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
   
  DetectSensors(ALL);
 
}

void loop() {
  
    Serial.println("Try read"); 
    for (int i =0;i<SENSORS;i++)
    {
       tcaselect(i);
       delay(10);
     uint8_t pressureUnit(0);  // unit: B000 = Pa, B001 = hPa, B010 = Hg, B011 = atm, B100 = bar, B101 = torr, B110 = N/m^2, B111 = psi    
    bme[i].read(pres, temperature, humidity, metric,pressureUnit);
    Serial.print("Sensor:");
    Serial.println(i);
    if (isnan(temperature)==false)
    {
      SensorsData[i][0] = temperature;
      SensorsData[i][1] = humidity;
      SensorsData[i][2] = pres;
      
      Serial.print("Data:");
      Serial.println(temperature);
      Serial.print("H:");
      Serial.print(humidity);
      Serial.println("");
      temperature = NAN;
      humidity = NAN;
      pres = NAN;
      delay(10);
    }
    else
    {
      SensorsData[i][0] = NAN;
      SensorsData[i][1]= NAN;
      SensorsData[i][2] = NAN;
      DetectSensors(ALL);
      delay(10);
      //isSerial=false;
      //setup();
    }
    }
    
    server.handleClient();
    ArduinoOTA.handle();
    delay(500);
   // SendData(SensorsData);
    /*
    for (int i =0;i<SENSORS;i++)
    {
      SensorsData[i][0] = 0;
      SensorsData[i][1] = 0;
      SensorsData[i][2] = 0;
    }
    */
    //delay(2000);
    //delay(delayTime);
}

String PrepareData(float temperature,float humidity,float pressure,String hash)
{
  return "{\"sensorHash\":\""+hash+"\",\"temperature\":"+String(temperature)+",\"humidity\":"+String(humidity)+",\"pressure\":"+String(pressure)+"}";
}

void SendData(float source[][3])
{
  
  for (int i = 0;i<SENSORS;i++)
  {
    if (isnan(SensorsData[i][0])==false)
    {
      String Data = PrepareData(SensorsData[i][0],SensorsData[i][1],SensorsData[i][2],SensorsHash[i]);
      POST(Data,sensorUrl);
    }
  }
}

void DetectSensors(int sensor)
{
  Wire.begin();
  Wire.beginTransmission(TCAADDR);
  Wire.write(0);  // no channel selected
  Wire.endTransmission();
  for (int i = 0;i<SENSORS;i++)
  {
    tcaselect(i);
    delay(1);
    if (sensor==-1)
    {
    if (!bme[i].begin())
    {
      /*
      if (isSerial)
      {
        Serial.println("Ooops, no BME detected ... Check your wiring!");
      }
      */
    }
    }
    else if (sensor==i)
    {
      if (!bme[i].begin())
    {
      /*
      if (isSerial)
      {
        Serial.println("Ooops, no BME detected ... Check your wiring!");
      }
      */
    }
    }
   }
}

void setupAP()
{
  WiFi.softAP(ssid, password, 1);
}

void root()
{
  checkAuth();

  String temp = "";

  for (int i =0;i<SENSORS;i++)
  {
    temp += "<p>Sensor "+String(i)+": "+String(SensorsData[i][0])+" "+String(SensorsData[i][1])+" "+String(SensorsData[i][2])+" </p>"; 
  }
  
  content = "<!DOCTYPE html>\
  <html>\
  <head>\
  <title>ROOM 43</title>\
  <meta http-equiv=\"refresh\" content=\"2\"> \
  </head>\
  <body>\
  <h1>Sensors list</p>\
  "+temp+"</body></html>";

  server.send(200,"text/html",content);
}

void reboot()
{
  checkAuth();
 
   content = "<!DOCTYPE HTML>\r\n<html><head><title>Reboot</title></head><body><p>Reboot... Wait 10 seconds.</p></body></html>";
   server.sendHeader("Refresh","7; url=/");
   server.send(200, "text/html", content);  
   
   delay(3000);
   //ESP.wdtDisable();
   ESP.reset();
}

void checkAuth()
{
  if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
}

void settings()
{
  checkAuth();

  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");

  Serial.println(qsid);
  Serial.println(qpass);
 // delay(10000);
  
   content = "<!DOCTYPE HTML>\r\n<html><head><title>Settings</title></head><body> ";
  if (qsid.length() > 0 && qpass.length() > 0) {
    if (qsid.length()<33 &&  qpass.length()<65 && qpass.length()>7)
    {
      for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }//Clean eeprom
      for (int i = 0; i < qsid.length(); ++i){ EEPROM.write(i, qsid[i]); }
      for (int i = 0; i < qpass.length(); ++i){ EEPROM.write(32+i, qpass[i]); }
      EEPROM.commit(); 
      server.sendHeader("Refresh","2; url=/settings");
      content +="<p>Save success!</p>";   
    }
    else
    {
      content +="<p>Error. Incorrect data</p>";
    }
    
  }
  else
  {
        content += "<p>Current network:";
        content += essid;
        content += "</p><form method='POST' action='settings'><label>SSID: </label><input name='ssid' length=32 type=\"text\"><br><label>Password: </label><input name='pass' length=64 type=\"password\"><br><br><input type='submit' value=\"Save\"></form>";
        
  }
  content += "</body></html>";
  server.send(200, "text/html", content);  
}

void POST(String data,String url)
{
  WiFiClientSecure client;
 
  if (!client.connect(host, httpsPort)) {
      Serial.println("connection failed");
      ESP.reset();
      return;
  }

  if (client.verify(fingerprint, host)) {
    Serial.println("certificate matches");
  //  secure = true;
  } else {
    Serial.println("certificate doesn't match");
//    secure = false;
  }
    if (url=="")
    {
      url = "/iot/beta/source/power/";
    }   
    String PostData = data;//"{\"device\":\"ESP8266\",\"data\":{\"temp\":["+String(temp_f)+","+String(bmptemp)+"],\"humidity\":["+String(humidity)+"]}}";
    client.println("POST "+url+" HTTP/1.1");
    client.println("Host: ideas.knu.ua");
    client.println("Cache-Control: no-cache");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(PostData.length());
    client.println("Usertoken: authtoken");//If you use auth that based on token
    client.println();
    client.println(PostData);

unsigned long connectionTime = millis();
  while (client.connected()) {
    if ((millis()-connectionTime)>connectionTimeOut)
    {
       ESP.reset();
    }
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  String line = client.readStringUntil('\n');
  if (line.startsWith("{\"state\":\"success\"")) {
    Serial.println("esp8266/Arduino CI successfull!");
  } else {
    Serial.println("esp8266/Arduino CI has failed");
  }

  Serial.println(data);
}
