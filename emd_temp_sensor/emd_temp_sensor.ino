#include <Wire.h>

/* =======================
   DS2482-100 DEFINITIONS
   ======================= */
#define BRIDGE_ADDR        0x18
#define CMD_DEVICE_RESET   0xF0
#define CMD_SET_READ_PTR   0xE1
#define CMD_WRITE_CONFIG   0xD2
#define CMD_1WIRE_RESET    0xB4
#define CMD_1WIRE_WRITE    0xA5
#define CMD_1WIRE_READ     0x96
#define REG_STATUS         0xF0
#define REG_DATA           0xE1

/* =======================
   SENSOR ROM IDS
   ======================= */

// Replace these with your actual 8-byte ROM IDs found during discovery
uint8_t sensor1[8] = {0x28, 0x93, 0x05, 0x9B, 0x11, 0x00, 0x00, 0xB0}; 
uint8_t sensor2[8] = {0x28, 0x6A, 0x7D, 0x9C, 0x11, 0x00, 0x00, 0x81}; 
uint8_t sensor3[8] = {0x28, 0x72, 0xAD, 0x9B, 0x11, 0x00, 0x00, 0xA3};
uint8_t sensor4[8] = {0x28, 0x2B, 0x3D, 0x9C, 0x11, 0x00, 0x00, 0x2A}; 
uint8_t sensor5[8] = {0x28, 0x0B, 0x7C, 0x9C, 0x11, 0x00, 0x00, 0xB8};
uint8_t sensor6[8] = {0x28, 0x3F, 0xAE, 0x9B, 0x11, 0x00, 0x00, 0xD2};
uint8_t sensor7[8] = {0x28, 0xA6, 0x05, 0x9B, 0x11, 0x00, 0x00, 0xB6};
uint8_t sensor8[8] = {0x28, 0xA2, 0x05, 0x9B, 0x11, 0x00, 0x00, 0x6A}; 
uint8_t* sensors[] = {sensor1, sensor2, sensor3, sensor4, sensor5, sensor6, sensor7, sensor8};

/* =======================
   LOW-LEVEL HELPERS
   ======================= */
uint8_t readStatus() {
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_SET_READ_PTR);
  Wire.write(REG_STATUS);
  Wire.endTransmission(false);
  Wire.requestFrom(BRIDGE_ADDR, 1);
  return Wire.read();
}

bool wait1WB(uint16_t timeout_ms = 100) {
  uint32_t start = millis();
  while (readStatus() & 0x01) {
    if (millis() - start > timeout_ms) return false;
  }
  return true;
}

void writeConfig(bool spu) {  
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_WRITE_CONFIG);
  Wire.write(CMD_SET_READ_PTR);
  Wire.write(0xC3);
  Wire.endTransmission(false);
  Wire.requestFrom(BRIDGE_ADDR, 1);
  Serial.print(Wire.read());
}

bool owReset() {
  wait1WB();
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_1WIRE_RESET);
  Wire.endTransmission();
  wait1WB();

  uint8_t status = readStatus();
  
  if (status & 0x04) {
    Serial.println("HARDWARE ERROR: Line is shorted to Ground!");
    return false;
  }
  if (!(status & 0x02)) {
    Serial.println("COMMUNICATION ERROR: No Presence Pulse (No sensor found).");
    return false;
  }
  
  Serial.println("SUCCESS: Sensor detected!");
  return true;
}

void owWrite(uint8_t b) {
  wait1WB();
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_1WIRE_WRITE);
  Wire.write(b);
  Wire.endTransmission();
}

uint8_t owReadByte() {
  wait1WB();
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_1WIRE_READ);
  Wire.endTransmission();
  wait1WB();
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_SET_READ_PTR);
  Wire.write(REG_DATA);
  Wire.endTransmission(false);
  Wire.requestFrom(BRIDGE_ADDR, 1);
  return Wire.read();
}

/* =======================
   TEMPERATURE LOGIC
   ======================= */

// trigger all sensors to convert at the same time (saves time)
void triggerConversion() {
  owReset();
  owWrite(0xCC); // skip ROM (Address all devices)
  writeConfig(true); // strong pu for conversion
  owWrite(0x44); // convert T
  delay(1000);    // wait for conversion
  //writeConfig(false); // remove strong pu
}

float readTemperature(uint8_t* rom) {

  owReset();
  //owWrite(0xCC);
  owWrite(0x55); // match ROM command
  for (int i = 0; i < 8; i++) {
    owWrite(rom[i]); // send the 8-byte address
  }

  owWrite(0xBE); //read scratchpad

  uint8_t data[9];
  for (int i = 0; i < 9; i++) {
    data[i] = owReadByte();
  }

  Serial.print("Scratchpad: ");
  for (int i = 0; i < 9; i++) {
    Serial.print("0x");
  if (data[i] < 16) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  //convert raw temp
  int16_t raw = (data[1] << 8) | data[0];
  return raw / 16.0;
}

// add crc check

/* =======================
   SETUP / LOOP
   ======================= */
void setup() {
  Serial.begin(200000); //115200
  while (!Serial);
  Wire.begin(); //initialize i2c
  
  // hardware Reset
  Wire.beginTransmission(BRIDGE_ADDR); //starts i2c w/ DS2482
  Wire.write(CMD_DEVICE_RESET); //reset DS2482
  Wire.endTransmission();
  delay(10);
  
  //writeConfig(false);
  Serial.println("Dual Sensor Reader Ready.");
}

void loop() {
  // reset the bus to see if a sensor is present
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_1WIRE_RESET);
  Wire.endTransmission();
  
  // wait for reset to finish
  delay(2); 
  
  // send 'Read ROM' command (0x33)
  // this ONLY works with 1 sensor connected!
  Wire.beginTransmission(BRIDGE_ADDR);
  Wire.write(CMD_1WIRE_WRITE); //writes byte to ow
  Wire.write(0x33); //requests rom
  Wire.endTransmission();
  delay(2);

  uint8_t rom[8]; //initialize rom id with 8 bytes
  bool allZero = true;

  for (int i = 0; i < 8; i++) { //repeat for 8 bytes
    // request a byte read from the 1-Wire bus
    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(CMD_1WIRE_READ);
    Wire.endTransmission();
    delay(2);
    
    // move pointer to data register and read it
    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(CMD_SET_READ_PTR); //sets read pointer at specified register
    Wire.write(REG_DATA); //gets data from 0xE1 register in sensor
    Wire.endTransmission(false); //write, then read
    Wire.requestFrom(BRIDGE_ADDR, 1); //arduino requests byte from DS2482
    
    rom[i] = Wire.read(); //checks that there is data in rom address
    if (rom[i] != 0x00) allZero = false;
  }

  if (!allZero) { //if romId not all 0
    Serial.print("Detected ID: {");
    for (int i = 0; i < 8; i++) {
      Serial.print("0x");
      if (rom[i] < 16) Serial.print("0");
      Serial.print(rom[i], HEX);
      if (i < 7) Serial.print(", ");
    }
    Serial.println("}");
    delay(3000); // Wait 3 seconds so you can copy the ID
  } else {
    Serial.println("Waiting for sensor...");
    delay(1000);
  }

  // tell ALL sensors on the bus to start measuring
  triggerConversion();

  // loops through all sensor ids and prints temp
  for (int i = 0; i < 8; i++) {
    float t = readTemperature(sensors[i]);
    Serial.print("Sensor ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(t);
    Serial.println(" °C");
  }

  Serial.println("---");
  delay(2000);
}