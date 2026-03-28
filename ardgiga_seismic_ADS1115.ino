#include <mbed.h>
//using namespace mbed;
#include <Adafruit_GPS.h>
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// Update this to whatever pin you wire to 'CS' on the W5500
const int W5500_CS = 10;
#include <ArduinoHttpClient.h>
#include "arduinoFFT.h"
#include <Adafruit_ADS1X15.h>

#define SAMPLES 128            // Increased for better resolution
#define SAMPLE_FREQ 200      
EthernetServer server(23);

struct Config {
  int adsGain;          // 0 = 2/3, 1 = 1, 2 = 2, 4 = 4, 8 = 8, 16 = 16
  int Samples;
  int Upload;
  int16_t sensorOffsetY;   // Manual DC offset adjustment
  int16_t sensorOffsetX;   // Manual DC offset adjustment
  int16_t sensorOffsetZ;   // Manual DC offset adjustment
  float VMMSY;           // Volts/mm/s Y
  float VMMSX;           // Volts/mm/s X
  float VMMSZ;           // Volts/mm/s Z
} currentSettings;
const char* configFile = "config.txt";

void readConfig() {
  bool sdStatus = SD.begin(4); // Assuming Pin 4 for Ethernet Shield SD

  if (!sdStatus || !SD.exists(configFile)) {
    // FALLBACK: Load these if the card is dead or the file is missing
    currentSettings.adsGain = 1;
    currentSettings.Samples = 5000;
    currentSettings.Upload = 5000; 
    currentSettings.sensorOffsetY = 0;
    currentSettings.sensorOffsetX = 0;
    currentSettings.sensorOffsetZ = 0;
    currentSettings.VMMSY = 1.1300;
    currentSettings.VMMSX = 1.1300;
    currentSettings.VMMSZ = 1.1300;
    
    Serial.println("WARNING: Vault running in SAFE MODE (Defaults loaded)");
    return;
  }

  File f = SD.open(configFile, FILE_READ);
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("GAIN=")) currentSettings.adsGain = line.substring(5).toInt();
    if (line.startsWith("SAMPLES=")) currentSettings.Samples = line.substring(8).toInt();
    if (line.startsWith("UPLOAD=")) currentSettings.Upload = line.substring(12).toInt();
    if (line.startsWith("OFFSETY=")) currentSettings.sensorOffsetY = line.substring(8).toInt();
    if (line.startsWith("OFFSETX=")) currentSettings.sensorOffsetX = line.substring(8).toInt();
    if (line.startsWith("OFFSETZ=")) currentSettings.sensorOffsetZ = line.substring(8).toInt();
    if (line.startsWith("VMMSY=")) currentSettings.VMMSY = line.substring(6).toFloat();
    if (line.startsWith("VMMSX=")) currentSettings.VMMSX = line.substring(6).toFloat();
    if (line.startsWith("VMMSZ=")) currentSettings.VMMSZ = line.substring(6).toFloat();
    
  }
  f.close();
}

void handleTelnet() {
  EthernetClient client = server.available();
  if (client) {
    String cmd = client.readStringUntil('\n');
    cmd.trim();
    
    bool changed = false;

    if (cmd.startsWith("SET_GAIN=")) {
      currentSettings.adsGain = cmd.substring(9).toInt();
      changed = true;
    } 
    else if (cmd.startsWith("SET_SAMPLES=")) {
      currentSettings.Samples = cmd.substring(12).toInt();
      changed = true;
    }
    else if (cmd.startsWith("SET_UPLOAD=")) {
      currentSettings.Upload = cmd.substring(16).toInt();
      changed = true;
    }
    else if (cmd.startsWith("SET_OFFSETY=")) {
      currentSettings.sensorOffsetY = cmd.substring(12).toInt();
      changed = true;
    }
    else if (cmd.startsWith("SET_OFFSETX=")) {
      currentSettings.sensorOffsetX = cmd.substring(12).toInt();
      changed = true;
    }
    else if (cmd.startsWith("SET_OFFSETZ=")) {
      currentSettings.sensorOffsetZ = cmd.substring(12).toInt();
      changed = true;
    }
    else if (cmd.startsWith("SET_VMMSY=")) {
      currentSettings.VMMSY = cmd.substring(12).toFloat();
      changed = true;
    }
    else if (cmd.startsWith("SET_VMMSX=")) {
      currentSettings.VMMSX = cmd.substring(12).toFloat();
      changed = true;
    }
    else if (cmd.startsWith("SET_VMMSZ=")) {
      currentSettings.VMMSZ = cmd.substring(12).toFloat();
      changed = true;
    }

    if (changed) {
      writeConfig(); // Save to SD immediately
      client.println("Settings staged to SD. Send 'COMMIT' to apply and reboot.");
    }

    if (cmd == "COMMIT") {
      client.println("Writing to SD...");
      writeConfig();
      client.println("Rebooting Giga in the vault...");
      while(1);
    }
    
    if (cmd == "STATUS") {
      client.println("--- Current Vault Settings ---");
      client.println("Gain: " + String(currentSettings.adsGain));
      client.println("Samples: " + String(currentSettings.Samples));
      client.println("Upload: " + String(currentSettings.Upload));
      client.println("OffsetY: " + String(currentSettings.sensorOffsetY));
      client.println("OffsetX: " + String(currentSettings.sensorOffsetX));
      client.println("OffsetZ: " + String(currentSettings.sensorOffsetZ));
      client.println("VMMS Y: " + String(currentSettings.VMMSY,4));
      client.println("VMMS X: " + String(currentSettings.VMMSX,4));
      client.println("VMMS Z: " + String(currentSettings.VMMSZ,4));
      
    }
  }
}

void writeConfig() {
  // Remove the old file first to ensure a clean write
  if (SD.exists(configFile)) {
    SD.remove(configFile);
  }

  File f = SD.open(configFile, FILE_WRITE);
  if (f) {
    f.println("GAIN=" + String(currentSettings.adsGain));
    f.println("SAMPLES=" + String(currentSettings.Samples));
    f.println("UPLOAD=" + String(currentSettings.Upload));
    f.println("OFFSETY=" + String(currentSettings.sensorOffsetY));
    f.println("OFFSETX=" + String(currentSettings.sensorOffsetX));
    f.println("OFFSETZ=" + String(currentSettings.sensorOffsetZ));
    f.println("VMMSY=" + String(currentSettings.VMMSY,4)); // 4 decimal places
    f.println("VMMSX=" + String(currentSettings.VMMSX,4)); // 4 decimal places
    f.println("VMMSZ=" + String(currentSettings.VMMSZ,4)); // 4 decimal places
    
    f.close();
  }
}



double vRealY[SAMPLES];
double vImagY[SAMPLES];
ArduinoFFT<double> FFTy = ArduinoFFT<double>(vRealY, vImagY, SAMPLES, SAMPLE_FREQ);

double vRealX[SAMPLES];
double vImagX[SAMPLES];
ArduinoFFT<double> FFTx = ArduinoFFT<double>(vRealX, vImagX, SAMPLES, SAMPLE_FREQ);

double vRealZ[SAMPLES];
double vImagZ[SAMPLES];
ArduinoFFT<double> FFTz = ArduinoFFT<double>(vRealZ, vImagZ, SAMPLES, SAMPLE_FREQ);




Adafruit_ADS1115 ads; // Create the 16-bit ADC object
EthernetClient ethernet;
float peakFrequencyY = 0;
float peakFrequencyX = 0;
float peakFrequencyZ = 0;
unsigned int localPort = 8888;       // local port to listen for UDP packets

const char timeServer[] = "time.nist.gov"; // time.nist.gov NTP server

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;
char serverAddress[] = "192.168.1.103"; 
int port = 8086;

HttpClient Hclient = HttpClient(ethernet, serverAddress, port);

struct FilterY {
  float fx1;
  float fx2;
  float fy1;
  float fy2;
} filtY;

struct FilterX {
  float fx1;
  float fx2;
  float fy1;
  float fy2;
} filtX;

struct FilterZ {
  float fx1;
  float fx2;
  float fy1;
  float fy2;
} filtZ;

void setFiltInit() {
  filtY.fx1 = 0;
  filtY.fx2 = 0;
  filtY.fy1 = 0;
  filtY.fy2 = 0;

  filtX.fx1 = 0;
  filtX.fx2 = 0;
  filtX.fy1 = 0;
  filtX.fy2 = 0;

  filtZ.fx1 = 0;
  filtZ.fx2 = 0;
  filtZ.fy1 = 0;
  filtZ.fy2 = 0;
}

// Filter Coefficients (Keep these as-is or prefix them too)
const float fb0 = 0.0675;
const float fb1 = 0.1349;
const float fb2 = 0.0675;
const float fa1 = -1.1429;
const float fa2 = 0.4128;

float applyFilterY(int16_t input) {
  // Use the new prefixed names in the calculation
  float output = (fb0 * input) + (fb1 * filtY.fx1) + (fb2 * filtY.fx2) - (fa1 * filtY.fy1) - (fa2 * filtY.fy2);

  // Update history
  filtY.fx2 = filtY.fx1;
  filtY.fx1 = input;
  filtY.fy2 = filtY.fy1;
  filtY.fy1 = output;

  return output;
}

float applyFilterX(int16_t input) {
  // Use the new prefixed names in the calculation
  float output = (fb0 * input) + (fb1 * filtX.fx1) + (fb2 * filtX.fx2) - (fa1 * filtX.fy1) - (fa2 * filtX.fy2);

  // Update history
  filtX.fx2 = filtX.fx1;
  filtX.fx1 = input;
  filtX.fy2 = filtX.fy1;
  filtX.fy1 = output;

  return output;
}

float applyFilterZ(int16_t input) {
  // Use the new prefixed names in the calculation
  float output = (fb0 * input) + (fb1 * filtZ.fx1) + (fb2 * filtZ.fx2) - (fa1 * filtZ.fy1) - (fa2 * filtZ.fy2);

  // Update history
  filtZ.fx2 = filtZ.fx1;
  filtZ.fx1 = input;
  filtZ.fy2 = filtZ.fy1;
  filtZ.fy1 = output;

  return output;
}


// Add a global flag at the top of your code
bool rebootTriggered = false;
float filteredVibration = 0;


int16_t finalValueY = 0;
int16_t finalValueX = 0;
int16_t finalValueZ = 0;

int systemState = 0; // 0 = Booting
char lineBuffer[128]; // Buffer for the string
unsigned long long nanoSeconds;

volatile unsigned long ppsMicros = 0;
volatile bool newSecond = false;


void petTheDog() {
  mbed::Watchdog::get_instance().kick();
}

float getPGV(int Gain, float VMMS, uint16_t ADC) {
  float adcVolts = 0.0000000000;
  switch(Gain) {
    case 0:
      adcVolts = 0.0001875;
      break;
    case 1:
      adcVolts = 0.000125;
      break;
    case 2:
      adcVolts = 0.0000625;
      break;
    case 4:
      adcVolts = 0.00003125;
      break;
    case 8:
      adcVolts = 0.000015625;
      break;
    case 16:
      adcVolts = 0.0000078125;
      break;
  }
  return (((ADC * adcVolts) / VMMS));
}

void setup() {
  setFiltInit();
  readConfig();

  Ethernet.init(W5500_CS); 

  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

  

  // Wake up the "dog"
  // 10000ms (10 seconds) timeout
  mbed::Watchdog &watchdog = mbed::Watchdog::get_instance();
  watchdog.start(20000);

  if (!ads.begin(0x48)) {
    Serial.println("Failed to initialize ADS1115!");
    while (1);
  }
  switch(currentSettings.adsGain) {
    case 0:
      ads.setGain(GAIN_TWOTHIRDS);
      break;
    case 1:
      ads.setGain(GAIN_ONE);
      break;
    case 2:
      ads.setGain(GAIN_TWO);
      break;
    case 4:
      ads.setGain(GAIN_FOUR);
      break;
    case 8:
      ads.setGain(GAIN_EIGHT);
      break;
    case 16:
      ads.setGain(GAIN_SIXTEEN);
      break;
  }
  
  petTheDog();

  Serial.begin(115200); // Debug
  Hclient.setTimeout(500); // 500ms timeout instead of 10 seconds

  if (Ethernet.begin(mac) == 0) {
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
  }
  petTheDog();
  Serial.println("Ethernet Connected!");

  // after a power-on surge, pause to let hardware components settle
  // so the calibration will be clean...
  delay(5000);
  server.begin();

  Serial.println("M7: Deep Calibration Y starting...");
  long runningSum = 0;
  const int calibrationSamples = 1000; // Increased for better 16-bit accuracy

  for (int i = 0; i < calibrationSamples; i++) {
      petTheDog();
      runningSum += ads.readADC_SingleEnded(0);
      delay(2); // Faster sampling for calibration
  }
  currentSettings.sensorOffsetY = runningSum / calibrationSamples;
  Serial.print("M7: Calibration Y complete. Offset: ");
  Serial.println(currentSettings.sensorOffsetY);

  Serial.println("M7: Deep Calibration X starting...");
  runningSum = 0;

  for (int i = 0; i < calibrationSamples; i++) {
      petTheDog();
      runningSum += ads.readADC_SingleEnded(1);
      delay(2); // Faster sampling for calibration
  }
  currentSettings.sensorOffsetX = runningSum / calibrationSamples;
  Serial.print("M7: Calibration X complete. Offset: ");
  Serial.println(currentSettings.sensorOffsetX);

  Serial.println("M7: Deep Calibration Z starting...");
  runningSum = 0;

  for (int i = 0; i < calibrationSamples; i++) {
      petTheDog();
      runningSum += ads.readADC_SingleEnded(2);
      delay(2); // Faster sampling for calibration
  }
  currentSettings.sensorOffsetZ = runningSum / calibrationSamples;
  Serial.print("M7: Calibration Z complete. Offset: ");
  Serial.println(currentSettings.sensorOffsetZ);

  Udp.begin(localPort);
}
static unsigned long lastSample = 0;
static int sampleCounter = 0;
static unsigned long lastUpload = 0;
uint64_t millisAtRead;
uint64_t currentEpochMs;
uint64_t elapsed;
uint64_t epochMilliseconds;

void loop() {
  //sendNTPpacket(timeServer); // send an NTP packet to a time server
  // 1. High-speed Sampling & Filtering (Every 5ms = 200Hz)
  if (micros() - lastSample >= currentSettings.Samples) {
    lastSample = micros(); // Update lastSample!
  
    int rawY = ads.readADC_SingleEnded(0);
    long inputY = rawY - currentSettings.sensorOffsetY;
    finalValueY = (int16_t)applyFilterY(inputY);

    int rawX = ads.readADC_SingleEnded(1);
    long inputX = rawX - currentSettings.sensorOffsetX;
    finalValueX = (int16_t)applyFilterX(inputX);

    int rawZ = ads.readADC_SingleEnded(2);
    long inputZ = rawZ - currentSettings.sensorOffsetZ;
    finalValueZ = (int16_t)applyFilterZ(inputZ);
    


    if (sampleCounter < SAMPLES) {
      vRealY[sampleCounter] = (double)finalValueY; // Use the filtered signal for FFT
      vImagY[sampleCounter] = 0;

      vRealX[sampleCounter] = (double)finalValueX; // Use the filtered signal for FFT
      vImagX[sampleCounter] = 0;

      vRealZ[sampleCounter] = (double)finalValueZ; // Use the filtered signal for FFT
      vImagZ[sampleCounter] = 0;

      sampleCounter++;
    }
  }
  // 2. When buffer is full, calculate frequency
  if (sampleCounter >= SAMPLES) {
    FFTy.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFTy.compute(FFT_FORWARD);
    FFTy.complexToMagnitude();
    peakFrequencyY = FFTy.majorPeak();

    FFTx.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFTx.compute(FFT_FORWARD);
    FFTx.complexToMagnitude();
    peakFrequencyX = FFTx.majorPeak();

    FFTz.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFTz.compute(FFT_FORWARD);
    FFTz.complexToMagnitude();
    peakFrequencyZ = FFTz.majorPeak();

    sampleCounter = 0; // Reset for next batch
  }

  // 2. InfluxDB Upload (Every 1 seconds)
  
  if (millis() - lastUpload >= currentSettings.Upload) {
    lastUpload = millis();

    if (isnan(peakFrequencyY)) {
      peakFrequencyY = 0.00;
    }
    if (isnan(peakFrequencyX)) {
      peakFrequencyX = 0.00;
    }
    if (isnan(peakFrequencyZ)) {
      peakFrequencyZ = 0.00;
    }
    elapsed = (uint64_t)millis() - millisAtRead;
    currentEpochMs = epochMilliseconds + elapsed;

    String payload = "seismic_data,location=shop,sensor=Y ";
    payload += "amplitude=" + String(finalValueY) + "i,"; // 'i' denotes integer for Influx
    payload += "frequency=" + String(peakFrequencyY, 2);
    payload += ",pgv=" + String(getPGV(currentSettings.adsGain,currentSettings.VMMSY,finalValueY),10);
    //if (currentEpochMs > 0) payload += " " + String(currentEpochMs);
    int errY = Hclient.post("/write?db=seismic&precision=ms", "text/plain", payload);

    if (errY == 0) {
      int statusY = Hclient.responseStatusCode();
      // Only read the body if there is actually an error to report
      if (statusY != 204) {
        String responseY = Hclient.responseBody();
        Serial.println("Error: " + responseY);
      }
    }
    //sendFiberState(systemState);
    // THE FIX: Close the connection so the next loop starts fresh
    Hclient.flush();
    Hclient.stop();

    petTheDog();

    payload = "seismic_data,location=shop,sensor=X ";
    payload += "amplitude=" + String(finalValueX) + "i,"; // 'i' denotes integer for Influx
    payload += "frequency=" + String(peakFrequencyX, 2);
    payload += ",pgv=" + String(getPGV(currentSettings.adsGain,currentSettings.VMMSX,finalValueX),10);
    //if (currentEpochMs > 0) payload += " " + String(currentEpochMs);
    int errX = Hclient.post("/write?db=seismic&precision=ms", "text/plain", payload);

    if (errX == 0) {
      int statusX = Hclient.responseStatusCode();
      // Only read the body if there is actually an error to report
      if (statusX != 204) {
        String responseX = Hclient.responseBody();
        Serial.println("Error: " + responseX);
      }
    }
    //sendFiberState(systemState);
    // THE FIX: Close the connection so the next loop starts fresh
    Hclient.flush();
    Hclient.stop();

    petTheDog();

    payload = "seismic_data,location=shop,sensor=Z ";
    payload += "amplitude=" + String(finalValueZ) + "i,"; // 'i' denotes integer for Influx
    payload += "frequency=" + String(peakFrequencyZ, 2);
    payload += ",pgv=" + String(getPGV(currentSettings.adsGain,currentSettings.VMMSZ,finalValueZ),10);
    //if (currentEpochMs > 0) payload += " " + String(currentEpochMs);
    int errZ = Hclient.post("/write?db=seismic&precision=ms", "text/plain", payload);

    if (errZ == 0) {
      int statusZ = Hclient.responseStatusCode();
      // Only read the body if there is actually an error to report
      if (statusZ != 204) {
        String responseZ = Hclient.responseBody();
        Serial.println("Error: " + responseZ);
      }
    }
    //sendFiberState(systemState);
    // THE FIX: Close the connection so the next loop starts fresh
    Hclient.flush();
    Hclient.stop();

    petTheDog();
  }
  if (Udp.parsePacket()) {
    // We've received a packet, read the data from it
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    millisAtRead = (uint64_t)millis();
    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    epochMilliseconds = (uint64_t)epoch * 1000L;
    
  }
  handleTelnet();
  petTheDog();
}