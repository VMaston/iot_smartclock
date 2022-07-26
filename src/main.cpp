#include <WiFi.h>
#include <WiFiClientSecure.h> //WifiClient with SSL Support
#include <WiFiManager.h>       //Wifi Modules Caller
#define WEBSERVER_H            //Including this avoids a conflict between the WiFiManager and Async Libraries https://github.com/me-no-dev/ESPAsyncWebServer/issues/418#issuecomment-667976368
#include <ESPAsyncWebServer.h> //Web Server

#define ARDUINOJSON_USE_DOUBLE 0 //https://arduinojson.org/v6/api/config/use_double/ 0 uses Float. Not necessary to have that precision.
#include <ArduinoJson.h> //ArduinoJson

#include <time.h>
#include <SPIFFS.h>          //SPIFFS FILE SYSTEM
#include <RGBmatrixPanel4.h> //Adafruit Libraru for RGB Matrix Panel

AsyncWebServer server(80); //Setup server. Port 80 for normal HTTP.

//RGB Panel Connector Setup
#define CLK 14 // USE THIS ON ESP32
#define OE 13
#define LAT 15
#define A 26
#define B 4
#define C 27

RGBmatrixPanel4 matrix(A, B, C, CLK, LAT, OE, true, 2); //Initializer for Matrix - 'true' enables double buffering and '2' doubles the width of the panel from 32 to 64.

int16_t textX = matrix.width(), //Create textX, a variable which will hold the horizontal cursor positon so text can scroll accross the matrix.
        textMin = 0; //TextMin is used to determine the length of which the text will scroll across the screen before returning to the original textX position of matrix.width().

//Button Connector Setup
#define BLEFT 34
#define BRIGHT 21
#define BUP 35
#define BDOWN 33
#define BSELECT 32

int displayMode = 0; //Variable to shift between our 5 available screens.
boolean firstTimeSetup = true; //To ping internet services on the first launch of the device.

//CLOCK SETUP
int daylightSave = 3600; //Hour worth of seconds, currently hardcoded.
unsigned long lastTime; //Holds the last time the clock was updated so it can add the time between millis() - lastTime to the incremental seconds of the clock.
int hours, minutes; //Time definition holders.
float seconds; //Seconds are a float rather than int to avoid truncating the missing time inbetween pings and cause a delay in the clock.
String date; //Date is currently held in a string and is fetched via the server. TODO: create dayofweek array for local time update.

WiFiClientSecure client; //Initialize WifiClient

boolean state = true; //State operates the on/off function of the clock.

//Social Media Variables
String twitterUser = "BestGuyEver"; //Placeholder variables which get replaced by the loading of local .txt files via SPIFFS.
String youtubeID = "UCBa659QWEk1AI4Tg--mrJ2A"; //Youtube channel is accessed via channel ID - hard to find. TODO: create a request for ID from a name using API.
String location = "Aberystwyth"; //While API can use lat/long - names have been pretty reliable so far and remains the more user friendly option.

/*
|--------------------------------------------------------------------------
| Convenience Methods
|--------------------------------------------------------------------------
*/

boolean checkUpdateTime(float updateMins, unsigned long lastUpdate) //Takes in a variable on the amount of minutes between web updates / variable that keeps track of each update.
{
  if (millis() - lastUpdate >= updateMins * 60 * 1000UL) //Sum converts to milliseconds for millis() comparison.
  {
    return true;
  }
  else //Uses a true/false return to determine if the data should be updated via an API call or not.
  {
    return false; 
  }
}

String readFile(fs::FS &fs, const char * filePath){ //Simple File Read via SPIFFS. A seperate file is used for each saved variable as a convenience.
  File file = fs.open(filePath, "r");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  return fileContent; //Returns the content of file, which is the values of the //Social Media Variables replaced at runtime.
}

void writeFile(fs::FS &fs, const char * filePath, const char * message){ //Save files to SPIFFS for when variables are replaced.
  Serial.printf("Writing file: %s\r\n", filePath);
  File file = fs.open(filePath, "w");
  file.print(message);
}

String processor(const String& var){ //Function to replace %PLACEHOLDER% items in HTML with correct variable from file. 
  if(var == "twitterUser"){          //https://github.com/me-no-dev/ESPAsyncWebServer#respond-with-content-coming-from-a-file-containing-templates
    return readFile(SPIFFS, "/twitterUser.txt");
  }
  else if(var == "youtubeID"){
    return readFile(SPIFFS, "/youtubeID.txt");
  }
  else if(var == "location"){
    return readFile(SPIFFS, "/location.txt");
  }
  return String();
}

void clearWifi() //WifiManager method for debug, clear saved Wifi Networks
{
  WiFiManager wifiManager;
  wifiManager.resetSettings();
}

/*
|--------------------------------------------------------------------------
| Clock Methods
|--------------------------------------------------------------------------
*/

void printTime() //Function to print the time.
{
  matrix.fillScreen(0); //0 'clears' the screen.
  matrix.setTextColor(matrix.Color444(7, 0, 0));
  matrix.setTextSize(1); //1 is the lowest text size, which takes up 5 spaces accross.

  matrix.setCursor(10, 0); //Where the text will be placed on the matrix. 64, 16 max.
  if (hours < 10)
  {
    matrix.print("0");
  }
  matrix.print(hours);
  matrix.print(":");
  if (minutes < 10)
  {
    matrix.print("0");
  }
  matrix.print(minutes);
  matrix.print(":");
  if ((int)seconds < 10)
  {
    matrix.print("0");
  }
  matrix.print((int)seconds); //the float seconds is casted to int to conveniently truncate the decimal for the display.

  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color444(0, 7, 0));
  matrix.setCursor((64 / 2) - (date.length() * 6 / 2), 8); //Calculate the correct placement of the date to centre it.
  matrix.print(date); //Print the date.
}


void updateClock() //Local Clock update. So NTP isn't pinged every second.
{
  double secondIncrement; //Stores the time since this method was last ran.

  if (millis() > lastTime)
  {
    secondIncrement = millis() - lastTime; //Stores the amount of seconds between the current time and the time this method was last ran.
    seconds = seconds + secondIncrement / 1000; //Dividing by 1000 brings secondIncrement miillisecond time to normal seconds.
    lastTime = millis();
  }
  if (seconds > 59) //Time increment handling
  {
    minutes++;
    seconds = seconds - 60;
  }
  if (minutes > 59)
  {
    hours++;
    minutes = minutes - 60;
  } //TODO: Create a local method for date that goes through an array of days of week that changes when hours > 23. Currently updates only on NTP check.

  if (displayMode == 0 && state == true) //Method can run in background while LEDs are off to keep time with frequent updates.
  {
    printTime();
  }
}

void checkTime() //NTP Server Check for Time.
{
  static unsigned long updateCounter; //Update time tracking variable.

  if (checkUpdateTime(5, updateCounter) || firstTimeSetup == true) //The program runs on first setup().
  {
    updateCounter = millis(); //Reset the update time counter.
    struct tm time; //Create structure time https://pubs.opengroup.org/onlinepubs/7908799/xsh/time.h.html
    while(!getLocalTime(&time)){ //getLocalTime() transmits a request to the NTP server and parses it as a readable format.
      configTime(0, daylightSave, "0.uk.pool.ntp.org"); //setup() has occassional issues with this, this handles that exception by trying again.
      Serial.println("Trying to get time, hang on.");
      delay(500);
  } 
    Serial.println(&time, "%A, %B %d %Y %H:%M:%S"); //Format specifiers for the tm struct.

    lastTime = millis(); //lastTime gets updated so updateClock() is synced with the NTP time.

    hours = time.tm_hour; //Sync time stored in tm struct with local variables.
    minutes = time.tm_min;
    seconds = time.tm_sec;

    char timeStringBuffer[50]; //Buffer value to hold the day of week.
    strftime(timeStringBuffer, sizeof(timeStringBuffer), "%A", &time); //strftime() is a local method for formatting time into a human readable format.

    date = (timeStringBuffer);
  }
}

/*
|--------------------------------------------------------------------------
| API Call Methods
|--------------------------------------------------------------------------
*/

void twitter()
{

  static unsigned long updateCounter;
  static int twitterFollowers; //Variable declared outside of if statement so it can be accessed later.

  if (checkUpdateTime(15, updateCounter) || firstTimeSetup == true)
  {
    updateCounter = millis();
    //Twitter API Key - TODO: Make the API keys customizable via WebSever
    String bearerToken = "AAAAAAAAAAAAAAAAAAAAAG%2FROgEAAAAAyS15ewbTwLF%2BYMaAyEb0oS%2FNheU%3DkTHCQjAYzafKVaDcxuEZWdkdB6ze71sOWr8VRn7ZLZp92ZSs6A";

    //Mandatory Certificate for HTTPS Twitter Connection for API Usage

    const char *twitterCert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIGOTCCBSGgAwIBAgIQBxr2E9Wg0irhzn+EfHYkEjANBgkqhkiG9w0BAQsFADBP\n"
        "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMSkwJwYDVQQDEyBE\n"
        "aWdpQ2VydCBUTFMgUlNBIFNIQTI1NiAyMDIwIENBMTAeFw0yMTAyMjQwMDAwMDBa\n"
        "Fw0yMjAyMjIyMzU5NTlaMGwxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9y\n"
        "bmlhMRYwFAYDVQQHEw1TYW4gRnJhbmNpc2NvMRYwFAYDVQQKEw1Ud2l0dGVyLCBJ\n"
        "bmMuMRgwFgYDVQQDEw9hcGkudHdpdHRlci5jb20wggEiMA0GCSqGSIb3DQEBAQUA\n"
        "A4IBDwAwggEKAoIBAQCBuHO3v/+tgRdikSYnvvTAP2Ue00nNjiofGH0oBYrTvY9P\n"
        "RYXPmgUuJn3of/lQg6c2uYJ8R/W7+gB8IXbR9bIqLi3fMBSgBEdVUKpa9Kv0mWIu\n"
        "VoRK6sAQ5mcXI5MISnQCySqPohMf6jddtiTJG2VIz0pmI9/qyxqgYE8vqCWC+7IF\n"
        "7TDU8uzhRv2W4KSqNI1inI277GkWwIMXTcqWcA2M43Qg6gVpuoEBRR4jZvm6zqla\n"
        "OpN1Dr8CDJ+HlYNKiFltGWxsnRhNjVrcAYdvxznZNCrV2fZjTLY3klYoUm9VPcWv\n"
        "3ToqKRhT4JkbK0dPYWqMr2rPnHtWnWt9s8h2wczZAgMBAAGjggLyMIIC7jAfBgNV\n"
        "HSMEGDAWgBS3a6LqqKqEjHnqtNoPmLLFlXa59DAdBgNVHQ4EFgQUpfPZXIoV69+p\n"
        "sT5CqoK1Edfrl6kwGgYDVR0RBBMwEYIPYXBpLnR3aXR0ZXIuY29tMA4GA1UdDwEB\n"
        "/wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwgYsGA1UdHwSB\n"
        "gzCBgDA+oDygOoY4aHR0cDovL2NybDMuZGlnaWNlcnQuY29tL0RpZ2lDZXJ0VExT\n"
        "UlNBU0hBMjU2MjAyMENBMS5jcmwwPqA8oDqGOGh0dHA6Ly9jcmw0LmRpZ2ljZXJ0\n"
        "LmNvbS9EaWdpQ2VydFRMU1JTQVNIQTI1NjIwMjBDQTEuY3JsMD4GA1UdIAQ3MDUw\n"
        "MwYGZ4EMAQICMCkwJwYIKwYBBQUHAgEWG2h0dHA6Ly93d3cuZGlnaWNlcnQuY29t\n"
        "L0NQUzB9BggrBgEFBQcBAQRxMG8wJAYIKwYBBQUHMAGGGGh0dHA6Ly9vY3NwLmRp\n"
        "Z2ljZXJ0LmNvbTBHBggrBgEFBQcwAoY7aHR0cDovL2NhY2VydHMuZGlnaWNlcnQu\n"
        "Y29tL0RpZ2lDZXJ0VExTUlNBU0hBMjU2MjAyMENBMS5jcnQwDAYDVR0TAQH/BAIw\n"
        "ADCCAQQGCisGAQQB1nkCBAIEgfUEgfIA8AB2ACl5vvCeOTkh8FZzn2Old+W+V32c\n"
        "YAr4+U1dJlwlXceEAAABd9FnprMAAAQDAEcwRQIhAMkQnOIKT5Fxa3PsT1VpG3W+\n"
        "whHP1I7Xvqnvk1pUoCVuAiA1DRtMDi0oiQpWoRogtoKMg4u5X+pg4YdRIjeRXlQw\n"
        "EwB2ACJFRQdZVSRWlj+hL/H3bYbgIyZjrcBLf13Gg1xu4g8CAAABd9FnpkgAAAQD\n"
        "AEcwRQIgOb5s0lchD8Hig2S4+WXU4gVLE7RtB4TUlTifoMCIuQYCIQC5dMntV9o0\n"
        "YtFN/oZXLaEGiyGjF/zNXO5CaTz3zmhe/zANBgkqhkiG9w0BAQsFAAOCAQEASaDI\n"
        "gyG9NxBj+otkbRJV7iJMYYbN05n1QLTZcuuYtE8KeqXqvX9XDBPmxv9fjU+DuUNM\n"
        "U3/USI3pv8m5vcl+A/fqNgccFynAros9pv8qYzfAwC+2YILXAsmVhNRv0Kd4YY9N\n"
        "5rHiRGq+kh5yp97PHWl4JOrUKNo1Hw+Sb/s0/CVmelUju67ZXcBtgTZDgrs+SeLZ\n"
        "q8uHU3fYu9OQMe7Q5Jw/ZOVY+VMZ04JRPx2MD4/1O8Faw6x3PQWA4BVGzS2WfYnd\n"
        "1fG4ZHhFpJkIwMwRhV7TbQM+fS376yJ1578VQWm7Uls+gVMXw2hpWX1diRd7slfe\n"
        "lmuY9PjJVTE+EGYznA==\n"
        "-----END CERTIFICATE-----\n";

    client.setCertificate(twitterCert);
    client.setInsecure(); //ESP32 bug, only functions with this enabled. https://github.com/espressif/arduino-esp32/issues/4992#issuecomment-811088088

    client.connect("api.twitter.com", 443); //Connect to Twitter API. 443 is HTTPS Port.

    // Send HTTP request
    client.print("GET /1.1/statuses/user_timeline.json?count=1&screen_name="); //Webserver Request that delivers JSON file
    client.print(twitterUser);
    client.println(" HTTP/1.1");
    client.println("Host: api.twitter.com");
    client.print("Authorization: Bearer "); //Twitter uses Bearer Tokens.
    client.println(bearerToken); //Need authorization to use API, currently implements personal bearerToken

    if (client.println() == 0)
    {
      Serial.println(("Failed to send request"));
    }

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) //If 200 OK isn't returned show error.
    {
      Serial.print("Unexpected response: ");
      Serial.println(status);
    }

    // Skip response headers
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    // Stream& input; - Created from https://arduinojson.org/v6/assistant/

    StaticJsonDocument<48> filter;
    filter[0]["user"]["followers_count"] = true; //Filter for JSON to only store followers_count from full JSON data.

    StaticJsonDocument<128> doc;

    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter)); //https://arduinojson.org/news/2020/03/22/version-6-15-0/

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    twitterFollowers = doc[0]["user"]["followers_count"]; //Stores JSON object in local variable.

    Serial.println(twitterFollowers);

    client.stop(); //Stop the client and clear the data recieved.
  }

  textMin = sizeof("Twitter Followers") * -8;
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color444(0, 0, 7));
  matrix.fillScreen(0);
  matrix.setCursor(textX, 1);
  matrix.print("Twitter Followers");
  matrix.setCursor((matrix.width() / 2) - (sizeof(twitterFollowers) * 5 / 2) + (sizeof(twitterFollowers)), 9); //*5 is used as each character is 5 led's accross. The additional sizeOf() is for the 1 led spaces between words.
  matrix.print(twitterFollowers);

  if ((--textX) < textMin) //This moves the "Twitter Followers" banner along as textX continually updates.
  {
    textX = matrix.width();
  }
}

void youtube()
{
  static unsigned long updateCounter;
  static String subCount; //Variable declared outside of if statement so it can be accessed later.

  if (checkUpdateTime(15, updateCounter) || firstTimeSetup == true)
  {
    updateCounter = millis();
    //YouTube API Key - TODO: Make the API keys customizable via WebSever
    String youtubeKey = "AIzaSyAZfrkUznUx7O6bciLjWTHP000iDzjQSTM";
    
    //Mandatory Certificate for HTTPS YouTube Connection for API Usage

    const char *youtubeCert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIGUjCCBTqgAwIBAgIQUcOenyrDS40DAAAAAMvXPzANBgkqhkiG9w0BAQsFADBC\n"
        "MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVR29vZ2xlIFRydXN0IFNlcnZpY2VzMRMw\n"
        "EQYDVQQDEwpHVFMgQ0EgMU8xMB4XDTIxMDMyMzA4MjQ0N1oXDTIxMDYxNTA4MjQ0\n"
        "NlowcTELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNhbGlmb3JuaWExFjAUBgNVBAcT\n"
        "DU1vdW50YWluIFZpZXcxEzARBgNVBAoTCkdvb2dsZSBMTEMxIDAeBgNVBAMTF3Vw\n"
        "bG9hZC52aWRlby5nb29nbGUuY29tMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE\n"
        "UIJItydg/9/tW1iMrkRwLS4WBzwyyoMRnYwf5vbRjU7zRcOZjdyKBmY0/4Pp7HCV\n"
        "3vxyzQXG7jE4slQ4qTDZd6OCA94wggPaMA4GA1UdDwEB/wQEAwIHgDATBgNVHSUE\n"
        "DDAKBggrBgEFBQcDATAMBgNVHRMBAf8EAjAAMB0GA1UdDgQWBBQGeX8SCF71X6dr\n"
        "Dho0PCLnNEElBTAfBgNVHSMEGDAWgBSY0fhuEOvPm+xgnxiQG6DrfQn9KzBoBggr\n"
        "BgEFBQcBAQRcMFowKwYIKwYBBQUHMAGGH2h0dHA6Ly9vY3NwLnBraS5nb29nL2d0\n"
        "czFvMWNvcmUwKwYIKwYBBQUHMAKGH2h0dHA6Ly9wa2kuZ29vZy9nc3IyL0dUUzFP\n"
        "MS5jcnQwggGYBgNVHREEggGPMIIBi4IXdXBsb2FkLnZpZGVvLmdvb2dsZS5jb22C\n"
        "FCouY2xpZW50cy5nb29nbGUuY29tghEqLmRvY3MuZ29vZ2xlLmNvbYISKi5kcml2\n"
        "ZS5nb29nbGUuY29tghMqLmdkYXRhLnlvdXR1YmUuY29tghAqLmdvb2dsZWFwaXMu\n"
        "Y29tghMqLnBob3Rvcy5nb29nbGUuY29tghMqLnVwbG9hZC5nb29nbGUuY29tghQq\n"
        "LnVwbG9hZC55b3V0dWJlLmNvbYIXKi55b3V0dWJlLTNyZC1wYXJ0eS5jb22CG2Jn\n"
        "LWNhbGwtZG9uYXRpb24tYWxwaGEuZ29vZ4IcYmctY2FsbC1kb25hdGlvbi1jYW5h\n"
        "cnkuZ29vZ4IZYmctY2FsbC1kb25hdGlvbi1kZXYuZ29vZ4IVYmctY2FsbC1kb25h\n"
        "dGlvbi5nb29nghF1cGxvYWQuZ29vZ2xlLmNvbYISdXBsb2FkLnlvdXR1YmUuY29t\n"
        "gh91cGxvYWRzLnN0YWdlLmdkYXRhLnlvdXR1YmUuY29tMCEGA1UdIAQaMBgwCAYG\n"
        "Z4EMAQICMAwGCisGAQQB1nkCBQMwMwYDVR0fBCwwKjAooCagJIYiaHR0cDovL2Ny\n"
        "bC5wa2kuZ29vZy9HVFMxTzFjb3JlLmNybDCCAQUGCisGAQQB1nkCBAIEgfYEgfMA\n"
        "8QB2AJQgvB6O1Y1siHMfgosiLA3R2k1ebE+UPWHbTi9YTaLCAAABeF5monYAAAQD\n"
        "AEcwRQIhALzeLVr2KiOKnfnPFUnFNp8EWrjwRMV3T0Gz9ob5FQUtAiBcuv5wnFl/\n"
        "uzkGtWwe0bLuSbv3uZrgxXjAviRw31G5mQB3AH0+8viP/4hVaCTCwMqeUol5K8UO\n"
        "eAl/LmqXaJl+IvDXAAABeF5moj4AAAQDAEgwRgIhAOz/PkhwgBswj/ycQ3fbeAhN\n"
        "hzf8P7pFTOnmrxbE7FGkAiEA5br1kb5KeqVJEXUu46b460il/lhxSo51lSIYDUon\n"
        "IxIwDQYJKoZIhvcNAQELBQADggEBAHSCpZPaWtR2xAeiuL12zohGPRTL+k/DcStu\n"
        "IWRNjBUXwOuu71cqpFZdICzIBIWQj6xRXrk7SlfF2csOxF5qNWC0INyJU1tMfCcT\n"
        "xfIAjuwKbphwGAKhrHAukwH2tqe/o1PKZMlI7SezX2SwyG6kFeMMPXBXkjN+yUrG\n"
        "34wynj+RUIeVbmjmswMuyPt1hz9e4Enae32SjnBuDiN2GkWJgdK1YBbGMvpfEa3e\n"
        "mMngV+BpYQXEJlRUJPJFQixS4iaQJk9tJga/DEBW6y5KaPYYsU7wCbnu8zddl4P8\n"
        "7xBti2DQbCt0tf+xpLb3KFlVEfm3fU3XBlMaZRzdoDomhRqZoG8=\n"
        "-----END CERTIFICATE-----\n";

    client.setCertificate(youtubeCert);
    client.setInsecure(); //ESP32 bug, only functions with this enabled. https://github.com/espressif/arduino-esp32/issues/4992#issuecomment-811088088

    client.connect("youtube.googleapis.com", 443); //Connect to YouTube API. 443 is HTTPS Port.

    // Send HTTP request
    client.print("GET /youtube/v3/channels?part=statistics&id="); //Webserver Request that delivers JSON file
    client.print(youtubeID);
    client.print("&key=");
    client.print(youtubeKey);
    client.println(" HTTP/1.0"); //The delivery of HTTP1.1 uses chunked transfer encoding, which isn't supported by Stream&. 1.0 is used here.
    client.println("Host: youtube.googleapis.com");

    if (client.println() == 0)
    {
      Serial.println(("Failed to send request"));
    }

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.0 200 OK") != 0) //If 200 OK isn't returned show error.
    {
      Serial.print("Unexpected response: ");
      Serial.println(status);
    }

    // Skip response headers
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    // Stream& input; - Created from https://arduinojson.org/v6/assistant/ for memory allocation.

    StaticJsonDocument<64> filter;
    filter["items"][0]["statistics"]["subscriberCount"] = true; //Filter for JSON to only store subscriberCount from full JSON data.

    StaticJsonDocument<192> doc;

    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter)); //https://arduinojson.org/news/2020/03/22/version-6-15-0/

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    subCount = doc["items"][0]["statistics"]["subscriberCount"].as<String>(); //Stores JSON object in local variable
    
    Serial.println(subCount);

    client.stop(); //Stop the client and clear the data recieved.
  }

  textMin = sizeof("YouTube Subscribers") * -8;
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color444(0, 0, 7));
  matrix.fillScreen(0);
  matrix.setCursor(textX, 1);
  matrix.print("YouTube Subscribers");
  matrix.setCursor((matrix.width() / 2) - (sizeof(subCount) * 5 / 2) + (sizeof(subCount)) , 9); //*5 is used as each character is 5 led's accross. The additional sizeOf() is for the 1 led spaces between words.
  matrix.print(subCount);

  if ((--textX) < textMin) //This moves the "YouTube Subscribers" banner along as textX continually updates.
  {
    textX = matrix.width();
  }
}

void weather()
{
  static unsigned long updateCounter;
  static int main_temp; //Variable declared outside of if statement so it can be accessed later.

  if (checkUpdateTime(15, updateCounter) || firstTimeSetup == true)
  {
    updateCounter = millis();
    //Weather API Key - TODO: Make the API keys customizable via WebSever
    String weatherKey = "4b656d804eb01aa84b5ab6a99796b365";
    
    //Mandatory Certificate for HTTPS OpenWeatherMap Connection for API Usage

    const char *weatherCert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIGvjCCBaagAwIBAgIRAKL7IEo7D+v9u0G4ItYCJYwwDQYJKoZIhvcNAQELBQAw\n"
        "gY8xCzAJBgNVBAYTAkdCMRswGQYDVQQIExJHcmVhdGVyIE1hbmNoZXN0ZXIxEDAO\n"
        "BgNVBAcTB1NhbGZvcmQxGDAWBgNVBAoTD1NlY3RpZ28gTGltaXRlZDE3MDUGA1UE\n"
        "AxMuU2VjdGlnbyBSU0EgRG9tYWluIFZhbGlkYXRpb24gU2VjdXJlIFNlcnZlciBD\n"
        "QTAeFw0yMDAzMTcwMDAwMDBaFw0yMjA2MTkwMDAwMDBaMB8xHTAbBgNVBAMMFCou\n"
        "b3BlbndlYXRoZXJtYXAub3JnMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
        "AQEA2DMTq6QbiQ6N/PK6u6dv8J1w5/w/GLm1d7J3daL80/15qRlsxUEpM78/OWmE\n"
        "s60kKSfyOVyxOHrVoXMfEhIxATdYQtRtN2JQEFYDkRauvVgr5eXQO2EJZXBZUb2C\n"
        "0dLFMD2WtrQGl7059kCOBlA/vX2+uTIQwFx/qZyVKkhzgdthtoDQ5jDzx7scDM0U\n"
        "9c/be/aWNPzoJV1HK37luC0nHUyT0zDpXMt82DgoCRix9z9RzDNkyjsPW2qP/pOE\n"
        "RpXk0z49jOFqtUxTtR9HfbKoeQ/RobxD2fG5P1cfunZ2lU3lyl5PeKbmMlSdSlci\n"
        "4OuileGdauTqgU254X7bB/9iTQIDAQABo4IDgjCCA34wHwYDVR0jBBgwFoAUjYxe\n"
        "xFStiuF36Zv5mwXhuAGNYeEwHQYDVR0OBBYEFP2HTXuP9/WVxbHQk4RHPpXCLktU\n"
        "MA4GA1UdDwEB/wQEAwIFoDAMBgNVHRMBAf8EAjAAMB0GA1UdJQQWMBQGCCsGAQUF\n"
        "BwMBBggrBgEFBQcDAjBJBgNVHSAEQjBAMDQGCysGAQQBsjEBAgIHMCUwIwYIKwYB\n"
        "BQUHAgEWF2h0dHBzOi8vc2VjdGlnby5jb20vQ1BTMAgGBmeBDAECATCBhAYIKwYB\n"
        "BQUHAQEEeDB2ME8GCCsGAQUFBzAChkNodHRwOi8vY3J0LnNlY3RpZ28uY29tL1Nl\n"
        "Y3RpZ29SU0FEb21haW5WYWxpZGF0aW9uU2VjdXJlU2VydmVyQ0EuY3J0MCMGCCsG\n"
        "AQUFBzABhhdodHRwOi8vb2NzcC5zZWN0aWdvLmNvbTAzBgNVHREELDAqghQqLm9w\n"
        "ZW53ZWF0aGVybWFwLm9yZ4ISb3BlbndlYXRoZXJtYXAub3JnMIIB9gYKKwYBBAHW\n"
        "eQIEAgSCAeYEggHiAeAAdwBGpVXrdfqRIDC1oolp9PN9ESxBdL79SbiFq/L8cP5t\n"
        "RwAAAXDobGj8AAAEAwBIMEYCIQDuoxRU3qxvOhsXh/vQPwAzBQfmu0b76RYKY27r\n"
        "3IjeuwIhAKhiaG0C9WMqsBNviTNJHl8iUZppSoDbreFWKU3ju715AHYA36Veq2iC\n"
        "Tx9sre64X04+WurNohKkal6OOxLAIERcKnMAAAFw6GxoyAAABAMARzBFAiEAiPLZ\n"
        "oR9BVGbeBKcZWWCWe5khT1jrbwqFFs1qqciHhmUCICNPG3dRIueExiu3HF6tUiNb\n"
        "rlGF/mf9Efr3JkAkqGsZAHUAQcjKsd8iRkoQxqE6CUKHXk4xixsD6+tLx2jwkGKW\n"
        "BvYAAAFw6Gxo7AAABAMARjBEAiAzzodBqseRU0wn7ukh37SvTOjmv8vpayKuZ4AE\n"
        "ut06BAIgArnrQObBVZU87a6ubmSWGHPiEi8cyPYdqZkMVycT3TgAdgBvU3asMfAx\n"
        "GdiZAKRRFf93FRwR2QLBACkGjbIImjfZEwAAAXDobGnaAAAEAwBHMEUCIGo9M7aa\n"
        "TjzbYPbR16+gwPnAGNiZI0ujRTDXRUJsW+D8AiEAgexT/9i23R7/XZfh5sL1Q9E/\n"
        "pE40zy1wXC1O3BHvz2MwDQYJKoZIhvcNAQELBQADggEBANJ4pa0tYp5QOtGy1RxM\n"
        "hcX2WydaU89WwySUB41pxbXBvaRLQyFBzC/COjPyN6zR52irYeBr0uFLLmwkaZfg\n"
        "eavkaExosslVP9g1js4j7wAKR5CdlEJfgw4eTxu8LAx5WUhm66HaMQol2neSyky2\n"
        "XPZt4KvZC9Fk/0x28JpXbMpckpH1/VpWPz3ulQw1/9TgV0+saRpFaKVXoZT5IObo\n"
        "j6cAp85OGBmRNJFypFFZRvy85aPJCP8IIyNoC9MoZIQ2VEuXQMTrIDU14Y46BTDq\n"
        "HaolM6WQZl42iGBzqJcOF2PGzcZ5YUahZW1GMxwB3NCyugR93FMCwtM4Wip6Ja5Q\n"
        "5fs=\n"
        "-----END CERTIFICATE-----\n";

    client.setCertificate(weatherCert);
    client.setInsecure(); //ESP32 bug, only functions with this enabled. https://github.com/espressif/arduino-esp32/issues/4992#issuecomment-811088088

    client.connect("api.openweathermap.org", 443); //Connect to OpenWeatherMap API. 443 is HTTPS Port.

    //Send HTTP request
    client.print("GET /data/2.5/weather?q="); //Webserver Request that delivers JSON file
    client.print(location);
    client.print("&units=metric&appid=");
    client.print(weatherKey); //Need authorization to use API, currently implements via GET request structure.
    client.println(" HTTP/1.1");
    client.println("Host: api.openweathermap.org");

    if (client.println() == 0)
    {
      Serial.println(("Failed to send request"));
    }

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) //If 200 OK isn't returned show error.
    {
      Serial.print("Unexpected response: ");
      Serial.println(status);
    }

    // Skip response headers
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    // Stream& input; - Created from https://arduinojson.org/v6/assistant/

    StaticJsonDocument<144> filter;

    JsonObject filter_weather_0 = filter["weather"].createNestedObject(); //Various data filters from full JSON data recieved.
    filter_weather_0["main"] = true;
    filter_weather_0["description"] = true;
    filter_weather_0["icon"] = true;
    filter["main"]["temp"] = true;
    filter["timezone"] = true;
    filter["name"] = true;

    StaticJsonDocument<256> doc;

    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter)); //https://arduinojson.org/news/2020/03/22/version-6-15-0/

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    JsonObject weather_0 = doc["weather"][0];
    const char *weather_0_main = weather_0["main"]; //TODO: Add Weather Descripton to matrix.
    const char *weather_0_description = weather_0["description"]; 
    const char *weather_0_icon = weather_0["icon"]; //TODO: Create custom icons using matrix.drawBitmap()     

    main_temp = doc["main"]["temp"];

    int timezone = doc["timezone"]; //TODO: Use timezone recieved from weather data on clock. Also accounts for daylightSavings.
    const char *name = doc["name"];
    location = name;

    Serial.printf("%s Temperature: %dc\n", name, main_temp);

    client.stop(); //Stop the client and clear the data recieved.
  }

  textMin = -(location.length())*5 - location.length();
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color444(0, 7, 0));
  matrix.fillScreen(0);
  matrix.setCursor(textX, 1);
  matrix.print(location);
  matrix.setCursor((matrix.width() / 2) - (sizeof(main_temp) * 5 / 2) + (sizeof(main_temp)), 9);
  matrix.setTextColor(matrix.Color444(7, 7, 0));
  matrix.printf("%dc", main_temp); //Special characters like degrees aren't included in the matrix's libary of characters

  if ((--textX) < textMin) //This moves the location banner along as textX continually updates.
  {
    textX = matrix.width();
  }
}

void crypto()
{
  static unsigned long updateCounter;
  static unsigned long screenSwitchCount;
  static String nameArray[5]; //Arrays declared outside of if statement so they can be accessed later.
  static double priceArray[5];
  static double priceDiffArray[5];

  if (checkUpdateTime(15, updateCounter) || firstTimeSetup == true)
  {
    updateCounter = millis();
    //CoinMarketCap API Key - TODO: Make the API keys customizable via WebSever
    String cryptoKey = "8f2fd7b3-eda6-422d-89fd-74e500f47aec";

    //Mandatory Certificate for HTTPS CoinMarketCap Connection for API Usage

    const char *cryptoCert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIEzTCCBHOgAwIBAgIQDSBW8sJj8sYzXNHiBBcRBDAKBggqhkjOPQQDAjBKMQsw\n"
        "CQYDVQQGEwJVUzEZMBcGA1UEChMQQ2xvdWRmbGFyZSwgSW5jLjEgMB4GA1UEAxMX\n"
        "Q2xvdWRmbGFyZSBJbmMgRUNDIENBLTMwHhcNMjAwNzI5MDAwMDAwWhcNMjEwNzI5\n"
        "MTIwMDAwWjBtMQswCQYDVQQGEwJVUzELMAkGA1UECBMCQ0ExFjAUBgNVBAcTDVNh\n"
        "biBGcmFuY2lzY28xGTAXBgNVBAoTEENsb3VkZmxhcmUsIEluYy4xHjAcBgNVBAMT\n"
        "FXNuaS5jbG91ZGZsYXJlc3NsLmNvbTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IA\n"
        "BIStwU6qU9TQST9NAlnKplmZAI7v+4Z+tsD6qUCeSBBh4qzdquqdbvHsXePZ6T27\n"
        "D8HVCexXj8s74GFM6iuCOO+jggMWMIIDEjAfBgNVHSMEGDAWgBSlzjfq67B1DpRn\n"
        "iLRF+tkkEIeWHzAdBgNVHQ4EFgQUYVD4Rp52Zmg3xz8CfCw42acfCMMwSAYDVR0R\n"
        "BEEwP4IRY29pbm1hcmtldGNhcC5jb22CEyouY29pbm1hcmtldGNhcC5jb22CFXNu\n"
        "aS5jbG91ZGZsYXJlc3NsLmNvbTAOBgNVHQ8BAf8EBAMCB4AwHQYDVR0lBBYwFAYI\n"
        "KwYBBQUHAwEGCCsGAQUFBwMCMHsGA1UdHwR0MHIwN6A1oDOGMWh0dHA6Ly9jcmwz\n"
        "LmRpZ2ljZXJ0LmNvbS9DbG91ZGZsYXJlSW5jRUNDQ0EtMy5jcmwwN6A1oDOGMWh0\n"
        "dHA6Ly9jcmw0LmRpZ2ljZXJ0LmNvbS9DbG91ZGZsYXJlSW5jRUNDQ0EtMy5jcmww\n"
        "TAYDVR0gBEUwQzA3BglghkgBhv1sAQEwKjAoBggrBgEFBQcCARYcaHR0cHM6Ly93\n"
        "d3cuZGlnaWNlcnQuY29tL0NQUzAIBgZngQwBAgIwdgYIKwYBBQUHAQEEajBoMCQG\n"
        "CCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQAYIKwYBBQUHMAKG\n"
        "NGh0dHA6Ly9jYWNlcnRzLmRpZ2ljZXJ0LmNvbS9DbG91ZGZsYXJlSW5jRUNDQ0Et\n"
        "My5jcnQwDAYDVR0TAQH/BAIwADCCAQQGCisGAQQB1nkCBAIEgfUEgfIA8AB3APZc\n"
        "lC/RdzAiFFQYCDCUVo7jTRMZM7/fDC8gC8xO8WTjAAABc5vO74cAAAQDAEgwRgIh\n"
        "AMQdbxW7q8ShzK53hMo9MkB2+FQaOkhCOaqHiyAsCMdlAiEAmwOmwVKdyIjj8m4d\n"
        "gFsR0VDx9G0ZO8223liKf7B2WVIAdQBc3EOS/uarRUSxXprUVuYQN/vV+kfcoXOU\n"
        "sl7m9scOygAAAXObzu+6AAAEAwBGMEQCIHPWhJvvn9HnGIoTuFj7k3pe75h7CJyu\n"
        "uGIt9jtkGFs9AiBZVaEsN869ucftorbe87tk9QBYXH6CTdX/Lwqh3iMT/TAKBggq\n"
        "hkjOPQQDAgNIADBFAiB4BxxiScfztDyIUz7CayKeId8kAjtLNTeAVtVAIt8IDwIh\n"
        "AN+LZl6c662IkyxlA1AEY+QGKVhTKz+n8eEEhOp8dPSc\n"
        "-----END CERTIFICATE-----\n";

    client.setCertificate(cryptoCert);
    client.setInsecure(); //ESP32 bug, only functions with this enabled. https://github.com/espressif/arduino-esp32/issues/4992#issuecomment-811088088

    client.connect("pro-api.coinmarketcap.com", 443); //Connect to CoinMarketCap API. 443 is HTTPS Port.

    // Send HTTP request
    client.print("GET /v1/cryptocurrency/listings/latest?start=1&limit=5&convert=GBP"); //Webserver Request that delivers JSON file
    client.println(" HTTP/1.0"); //The delivery of HTTP1.1 uses chunked transfer encoding, which isn't supported by Stream&. 1.0 is used here.
    client.print("X-CMC_PRO_API_KEY: "); //CoinMarketCap uses a unique header for the authorization key input.
    client.println(cryptoKey);
    client.println("Host: pro-api.coinmarketcap.com");

    if (client.println() == 0)
    {
      Serial.println(("Failed to send request"));
    }

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) //If 200 OK isn't returned show error.
    {
      Serial.print("Unexpected response: ");
      Serial.println(status);
    }

    // Skip response headers
    char endOfHeaders[] = "\r\n\r\n";
    client.find(endOfHeaders);

    // Stream& input; - Created from https://arduinojson.org/v6/assistant/ for memory allocation.

    StaticJsonDocument<496> filter;

    JsonArray filter_data = filter.createNestedArray("data"); //Various data filters from full JSON data recieved.

    JsonObject filter_data_0 = filter_data.createNestedObject(); //Due to the nature of the JSON recieved there are a lot of nested objects.
    filter_data_0["name"] = true;

    JsonObject filter_data_0_quote_GBP = filter_data_0["quote"].createNestedObject("GBP");
    filter_data_0_quote_GBP["price"] = true;
    filter_data_0_quote_GBP["percent_change_24h"] = true;

    JsonObject filter_data_1 = filter_data.createNestedObject();
    filter_data_1["name"] = true;

    JsonObject filter_data_1_quote_GBP = filter_data_1["quote"].createNestedObject("GBP");
    filter_data_1_quote_GBP["price"] = true;
    filter_data_1_quote_GBP["percent_change_24h"] = true;

    JsonObject filter_data_2 = filter_data.createNestedObject();
    filter_data_2["name"] = true;

    JsonObject filter_data_2_quote_GBP = filter_data_2["quote"].createNestedObject("GBP");
    filter_data_2_quote_GBP["price"] = true;
    filter_data_2_quote_GBP["percent_change_24h"] = true;

    JsonObject filter_data_3 = filter_data.createNestedObject();
    filter_data_3["name"] = true;

    JsonObject filter_data_3_quote_GBP = filter_data_3["quote"].createNestedObject("GBP");
    filter_data_3_quote_GBP["price"] = true;
    filter_data_3_quote_GBP["percent_change_24h"] = true;

    JsonObject filter_data_4 = filter_data.createNestedObject();
    filter_data_4["name"] = true;

    JsonObject filter_data_4_quote_GBP = filter_data_4["quote"].createNestedObject("GBP");
    filter_data_4_quote_GBP["price"] = true;
    filter_data_4_quote_GBP["percent_change_24h"] = true;

    StaticJsonDocument<768> doc;

    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter)); //https://arduinojson.org/news/2020/03/22/version-6-15-0/

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    int i = 0; //Numerical Iterator

    for (JsonObject elem : doc["data"].as<JsonArray>()) //JsonArray iterates per element within array.
    {

      //copyArray() function of arduinoJson failed. https://arduinojson.org/v6/api/misc/copyarray/
      //TODO: Create two-dimensional array or struct for variables.

      nameArray[i] = elem["name"].as<String>(); //Stores variables in Arrays.

      priceArray[i] = elem["quote"]["GBP"]["price"];

      priceDiffArray[i] = elem["quote"]["GBP"]["percent_change_24h"];

      i++;

    }

    client.stop(); //Stop the client and clear the data recieved.
  }

  static int i; //Variable for numerical iterator
  char priceDiffArrayLength[15];
  snprintf(priceDiffArrayLength, 15, "%.2lf%% ", priceDiffArray[i]);
  //priceDiffArray[i] is converted to a formatted char here so the program can use strlen() to get the length of the text.

  textMin = -((nameArray[i]).length())*5 - strlen(priceDiffArrayLength)*5 - nameArray[i].length() - strlen(priceDiffArrayLength); 
  //textMin is determined by the length of the message, * 5 to account for the length of words and then taken away by the length of the message to account for the 1 led spaces between each letter.

  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color444(0, 7, 0));
  matrix.fillScreen(0);
  matrix.setCursor(textX, 1);
  if (priceDiffArray[i] < 0) //If the price difference is below 0, colour switch to red.
  {
    matrix.setTextColor(matrix.Color444(7, 0, 0));
  }
  matrix.printf("%.2lf%% ", priceDiffArray[i]);
  matrix.print(nameArray[i]);
  matrix.setTextColor(matrix.Color444(7, 7, 0));
  matrix.setCursor((64 / 2) - (sizeof(priceArray[i]) * 6 / 2), 9);
  matrix.printf("%.2lf", priceArray[i]); //priceArray rendered to 2 significant characters.

  if ((--textX) < textMin) //This moves the location banner along as textX continually updates.
  {
    textX = matrix.width();
  }

  if (checkUpdateTime(0.5, screenSwitchCount)) //Small usage of checkUpdateTime that handles switching to the next crypto
  {
    screenSwitchCount = millis();
    i++;
  }

  if (i == 5) //Resets the i which controls the array pointer.
  {
    i = 0;
  } //Essentially, this creates a for (i in n) loop within the method re-runs.
}

/*
|--------------------------------------------------------------------------
| Setup - Initialization
|--------------------------------------------------------------------------
*/

void setup()
{
  //INITIALIZE PINS
  pinMode(BUP, INPUT);
  pinMode(BDOWN, INPUT);
  pinMode(BLEFT, INPUT);
  pinMode(BRIGHT, INPUT);
  pinMode(BSELECT, INPUT);

  //HOST SERIAL

  Serial.begin(115200);

  //ACTIVATE LED MATRIX

  matrix.begin();
  matrix.setTextColor(matrix.Color444(7, 0, 0));
  matrix.setTextWrap(false); // Allow text to run off right edge

  //ACTIVATE SPIFFS

  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  //CONNECT TO WIFI VIA WIFIMANAGER

  WiFiManager wifiManager; //Initializer

  wifiManager.autoConnect("ESP32 Smart Clock"); //Creates a Wifi Network with this SSID if not automatically connected.
  Serial.println("Successfully connected via Wifi Manager.");

  configTime(0, daylightSave, "0.uk.pool.ntp.org"); //NTP server setup.

  twitterUser = readFile(SPIFFS, "/twitterUser.txt");
  youtubeID = readFile(SPIFFS, "/youtubeID.txt");
  location = readFile(SPIFFS, "/location.txt"); //Read in SPIFFS stored files for variables.

  twitter();
  youtube();
  weather();
  crypto();
  checkTime(); //Run the initial methods with firstTimeSetup = true to populate the variables.
  updateClock();

  firstTimeSetup = false;

  //SERVER CONFIG

  server.begin();

/*
|--------------------------------------------------------------------------
| Setup - Server Handlers
|--------------------------------------------------------------------------
*/

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", String(), false, processor); //https://github.com/me-no-dev/ESPAsyncWebServer#respond-with-content-coming-from-a-file-containing-templates
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) { //Unused style.css file.
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) { //Unused script.js file.
    request->send(SPIFFS, "/script.js", "text/javascript");
  });

  server.on("/led", HTTP_GET, [](AsyncWebServerRequest *request) { //Function to turn the LEDs ON/OFF.
    request->send(SPIFFS, "/index.html", String(), false, processor);
    if (state == true)
    {
      Serial.println("LEDs OFF");
      state = false;
      matrix.fillScreen(0); //To clear the screen.
      matrix.swapBuffers(false); //Redraws the screen.
    }
    else if (state == false)
    {
      state = true; //Simple if statement to determine on or off.
    }
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) { //Recieves the /get requests.
    String inputMessage;
    int currDisplay;
    if (request->hasParam("twitterUser")) // Form: <ESP_IP>/get?twitterUser=<inputMessage>
    {
      inputMessage = request->getParam("twitterUser")->value();
      twitterUser = inputMessage;
      writeFile(SPIFFS, "/twitterUser.txt", inputMessage.c_str()); //Save file function with recieved input message.
      firstTimeSetup = true; //Flag first time setup to re-call the API instantaneously.
      currDisplay = displayMode;
      displayMode = 0; //client() crashes if we reaccess the method while already in it. displayMode=0 (clock) prevents this.
      twitter();
      displayMode = currDisplay; //Switch back to Display.
    }
    else if (request->hasParam("youtubeID")) // Form: <ESP_IP>/get?youtubeID=<inputMessage>
    {
      inputMessage = request->getParam("youtubeID")->value();
      youtubeID = inputMessage;
      writeFile(SPIFFS, "/youtubeID.txt", inputMessage.c_str()); //Save file function with recieved input message.
      firstTimeSetup = true; //Flag first time setup to re-call the API instantaneously.
      currDisplay = displayMode;
      displayMode = 0; //client() crashes if we reaccess the method while already in it. displayMode=0 (clock) prevents this.
      youtube();
      displayMode = currDisplay; //Switch back to Display.
    }
    else if (request->hasParam("location")) // Form: <ESP_IP>/location=<inputMessage>
    {
      inputMessage = request->getParam("location")->value();
      location = inputMessage;
      writeFile(SPIFFS, "/location.txt", inputMessage.c_str());  //Save file function with recieved input message.
      firstTimeSetup = true; //Flag first time setup to re-call the API instantaneously.
      currDisplay = displayMode;
      displayMode = 0; //client() crashes if we reaccess the method while already in it. displayMode=0 (clock) prevents this.
      weather();
      displayMode = currDisplay; //Switch back to Display.
    }
    firstTimeSetup = false; //Turn variable back off.
    request->send(SPIFFS, "/index.html", String(), false, processor); //Return to index page.
  });

  server.on("/clearWifi", HTTP_GET, [](AsyncWebServerRequest *request) { //Debug function to clear the Wifi without changing the code.
    request->send(SPIFFS, "/index.html", String(), false, processor);
    Serial.println("Wifi Settings Cleared");
    clearWifi();
  });
}

/*
|--------------------------------------------------------------------------
| Button Input Handlers
|--------------------------------------------------------------------------
*/

void readButtons()
{
  int reader = 0;

  //Takes each of the 5 individual buttons and assigns them each a displayMode.

  reader = digitalRead(BSELECT);
  if (reader == LOW)
  {
    displayMode = 0; //checkTime()
  }

  reader = digitalRead(BLEFT);
  if (reader == LOW)
  {
    displayMode = 1; //twitter()
  }

  reader = digitalRead(BRIGHT);
  if (reader == LOW)
  {
    displayMode = 2; //youtube()
  }

  reader = digitalRead(BUP);
  if (reader == LOW)
  {
    displayMode = 3; //weather()
  }

  reader = digitalRead(BDOWN);
  if (reader == LOW)
  {
    displayMode = 4; //crypto()
  }
}

/*
|--------------------------------------------------------------------------
| Main Execution Loop
|--------------------------------------------------------------------------
*/

void loop()
{
  matrix.fillScreen(0); //Clear screen at start of every loop.
  updateClock(); //Keep the local clock updating, even in the background of other displays.

  if (state == true)
  {
    switch (displayMode)
    {
    case 0:
      checkTime();
      break;

    case 1:
      twitter();
      break;

    case 2:
      youtube();
      break;

    case 3:
      weather();
      break;

    case 4:
      crypto();
      break;
    }

#if !defined(__AVR__)
    // On non-AVR boards, delay slightly so screen updates aren't too quick.
    delay(20);
#endif
  }

  matrix.swapBuffers(false); //Update Screen

  readButtons(); //Search for Button Input to change displayMode.
}