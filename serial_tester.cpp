#include <Arduino.h>
#include <Wire.h>
#include <LSM6DS3.h>
#include <MadgwickAHRS.h>
#include <bluefruit.h>

LSM6DS3 imu(I2C_MODE, 0x6A);
Madgwick filter;
int printnmr=0;
// ble making
BLEService imuService = BLEService(0x181A);
BLECharacteristic quatChar = BLECharacteristic(0x2A37); 
BLECharacteristic cmdChar = BLECharacteristic(0x2A38); 

bool record=false;
bool doubleTapHandled = false;
// buffer sizes
#define BATCH_SIZE 2
uint8_t outBuffer[8 * BATCH_SIZE];
int sampleCount = 0;

unsigned long lastUpdate = 0;
float deltat = 0.0;

// Calibration
float accBias[3] = {0}, gyroBias[3] = {0};
bool biasesSet = false;

int16_t aX, aY, aZ, gX, gY, gZ;
uint8_t rawData[12];
uint8_t status = 0;

// function
float calibrationsamples=2000;
float gyrobiasx=0, gyrobiasy=0, gyrobiasz=0;
void getgyrobiases (){
    Serial.println("Calibrating ......");
    delay(1000);
    float sumx=0, sumy=0, sumz=0;
    for (int i=0; i<calibrationsamples;i++)
        imu.readRegisterRegion(rawData, 0x22, 12);

        sumx += (int16_t)rawData[0] | int16_t(rawData[1] << 8);
        sumy += (int16_t)rawData[2] | int16_t(rawData[3] << 8);
        sumz += (int16_t)rawData[4] | int16_t(rawData[5] << 8);
        delay(5);
    gyrobiasx=sumx/calibrationsamples;
    gyrobiasy=sumy/calibrationsamples;
    gyrobiasz=sumz/calibrationsamples;

}


void readAccelGyroBurst(int16_t* ax, int16_t* ay, int16_t* az,
                         int16_t* gx, int16_t* gy, int16_t* gz, float* deltat) {
    imu.readRegisterRegion(rawData, 0x22, 12);

    *gx = (int16_t)rawData[0] | int16_t(rawData[1] << 8);
    *gy = (int16_t)rawData[2] | int16_t(rawData[3] << 8);
    *gz = (int16_t)rawData[4] | int16_t(rawData[5] << 8);
    *ax = (int16_t)rawData[6] | int16_t(rawData[7] << 8);
    *ay = (int16_t)rawData[8] | int16_t(rawData[9] << 8);
    *az = (int16_t)rawData[10] | int16_t(rawData[11] << 8);

    unsigned long now = micros();
    *deltat = (now - lastUpdate) * 1e-6f;
    lastUpdate = now;
    if (*deltat>=0.1 || *deltat<=1.0f/600){
        *deltat=1.0f/416;
    }
}
void sendBLECommand(bool record) {
    if (record) {
        cmdChar.write("START", 5);  // write “START” to the RX characteristic
    } else {
        cmdChar.write("STOP", 4);   // write “STOP”
    }
}

// double tap starts recoridng aswell
void record_from_device (){
    uint8_t activate;
    


    // imu.readRegister(&tap, 0x1C); // this is for double tap reading

    if((activate & 0x10 )&& !doubleTapHandled){
        record=!record;
        sendBLECommand(record);
        doubleTapHandled = true;
    }
    if(doubleTapHandled && !(activate & 0x10)){
        doubleTapHandled = false;
    }

}

void cmdReceivedCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
    String cmd = "";
    for (uint16_t i = 0; i < len; i++) {
        cmd += (char)data[i];
    }
    cmd.trim();

    if (cmd == "START") {
        record = true;
        Serial.println("Recording started!");
    } else if (cmd == "STOP") {
        record = false;
        Serial.println("Recording stopped!");
    }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);

  // Initialize IMU
  imu.begin();
  if (imu.beginCore() != 0) {
      Serial.println("IMU Error!");
      while (1);
  }
  imu.settings.gyroEnabled=1;
  imu.settings.accelEnabled=1;
  imu.settings.accelRange = 4;   // ±4 g
  imu.settings.gyroRange = 500; 
  imu.settings.gyroBandWidth=200;
  imu.settings.accelBandWidth=200;
  imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL, LSM6DS3_ACC_GYRO_ODR_XL_416Hz | LSM6DS3_ACC_GYRO_FS_XL_4g);
  imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL2_G, LSM6DS3_ACC_GYRO_ODR_G_416Hz | LSM6DS3_ACC_GYRO_FS_G_500dps);
  getgyrobiases();
  // enable tap detection
  // imu.writeRegister(0x58,0x8E); // enable
  // imu.writeRegister(0x5A,0x7F); // duration
  // imu.writeRegister(0x59, 0x0A); // threshold
  // imu.writeRegister(0x5B, 0x80); // wake up or not

  // filter beta:
  filter.beta=1.0f; // standard is 0.1
  // BLE
  Bluefruit.begin();
  Bluefruit.setName("IMU_Sense");
  imuService.begin();
  quatChar.setProperties(CHR_PROPS_NOTIFY);
  quatChar.begin();

  cmdChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  cmdChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  cmdChar.begin();
  cmdChar.setWriteCallback(cmdReceivedCallback);
  
  Bluefruit.Advertising.addService(imuService);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
  quatChar.notifyEnabled(true);
}

void loop() { record_from_device();
    imu.readRegister(&status, 0x1E);
    bool accelReady = status & 0x01;  // Bit 0 -> XLDA (accelerometer data available)
    bool gyroReady  = status & 0x02;
    if (accelReady && gyroReady) {
            // reading accelerometer data
            
            
            readAccelGyroBurst(&aX, &aY, &aZ, &gX, &gY, &gZ, &deltat);
            float ax = imu.calcAccel(aX)-acc_bias_x; // converting to g
            float ay = imu.calcAccel(aY)-acc_bias_y;// converting to g
            float az = imu.calcAccel(aZ)-acc_bias_z;// converting to g
            float gx= imu.calcGyro(gX-gyro_bias_x);
            float gy= imu.calcGyro(gY-gyro_bias_y);
            float gz= imu.calcGyro(gZ-gyro_bias_z);
            filter.updateIMU(gy, gx, -gz, ay, ax, -az, deltat);
            float q0 = filter.q0;
            float q1 = filter.q1;
            float q2 = filter.q2;
            float q3 = filter.q3;
            // converting
            float ax_g = ((1 - 2*q2*q2 - 2*q3*q3)*ay + (2*q1*q2 - 2*q0*q3)*ax + (2*q1*q3 + 2*q0*q2)*-az);
            float ay_g = ((2*q1*q2 + 2*q0*q3)*ay + (1 - 2*q1*q1 - 2*q3*q3)*ax + (2*q2*q3 - 2*q0*q1)*-az);
            float az_g = (((2*q1*q3 - 2*q0*q2)*ay + (2*q2*q3 + 2*q0*q1)*ax + (1 - 2*q1*q1 - 2*q2*q2)*-az)-1.0f);
            ax_mg = (int16_t)(ax_g * 1000.0f);
            ay_mg = (int16_t)(ay_g * 1000.0f);
            az_mg = (int16_t)(az_g * 1000.0f);
            dt_us = (uint16_t)(deltat * 1000000.0f);
            if (record){
                buffer_save();
            }
            
    }
}