This is an upload of a project I made while studying at University.

It's primary purpose is to act as a hub for a visual display (a connected LED Matrix) which will gather information from the connected social media APIs.

I recommend while testing this program you have the Serial Monitor open. I used Visual Studio Code with the PlatformIO extension.
This gives you useful information (that should be implemented within the matrix) that is necessary to operate the program.

## To operate the program:

1. If your Wifi network isn't saved in WifiManager then it will create a new access point for you to connect to called "ESP32 Smart Clock".

2. The Serial Monitor will output the IP address the WifiManager instance is hosted on. Navigate to that using a web browser.

3. Here you can select an SSID and input your credentials so the server will locally host on your network.

4. The server's IP address will appear under "\*WM: [1] STA IP Address: 192.168.1.x" in the Serial Monitor

5. Navigate to the Server

6. Here you can configure the 3 variable inputs of Twitter Username, YouTube Channel ID and Location

   6.1. Find YouTube Channel ID tutorial: https://support.google.com/youtube/answer/3250431?hl=en-GB

   6.2. Twitter Username is your @handle, not your Display Name.

   6.3. Location uses city names but may not get specific towns for unpopular places which share a common town name.

7. You can also switch the LED's ON and OFF using the top button.

8. There are 5 physical buttons on the board, they are assigned like so.

   8.1 SELECT = Clock

   8.2 LEFT = Twitter Followers

   8.3 RIGHT = YouTube Subscribers

   8.4 UP = Temperature in Location

   8.5 DOWN = 5 Most Popular Crypto, Their Values and Their % Change in the Last 24Hrs.

## DIRECTORY STRUCTURE

Main file is located in /src
Libraries are located in /.pio/libdeps/esp32dev
Web Server files are located in /data

The .txt files in /data are local files with placeholder variables for when a new Filesystem Image is uploaded to the ESP32.
They don't represent the actual values - which are stored in the ESP32 itself.
