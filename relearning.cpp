#include <Arduino.h>
#include <LSM6DS3.h> // Include the LSM6DS3 library
#include <MadgwickAHRS.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_LittleFS.h>
#include <CRC32.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
// sleeping pins
#include <nrf_gpio.h>
#include <nrf_power.h>
// ------- imu globals--------
LSM6DS3 myIMU(I2C_MODE, 0x6A);

uint8_t status=0;
int16_t aX=0, aY=0, aZ=0, gX=0, gY=0, gZ=0, temp=0; 
float deltat = 0.0f;
uint8_t rawData[12]= {0,0,0,0,0,0,0,0,0,0,0,0};
Madgwick filter;
uint32_t lastUpdate = 0;
int16_t ax_mg;
int16_t ay_mg;
int16_t az_mg;
uint32_t timestamp;
float a_mag;
float deviation;
float beta;
bool load_first=true;

// ------- buffer globals ------
uint16_t dt_us; //making it us so it can be 2 bytes instead of float for sending efficicency
// u because it cant be negative
struct __attribute__((packed)) AccelSample {
    uint32_t timestamp;
    int16_t ax;
    int16_t ay;
    int16_t az;
};
static_assert(sizeof(AccelSample) == 10, "AccelSample must be exactly 10 bytes");
const uint8_t buffersize=20;
AccelSample bufferA[20];
AccelSample bufferB[20];
uint8_t bufferIndex = 0;
AccelSample* activeBuffer = bufferA;
AccelSample* readyBuffer = nullptr;
void clear_buffer() {
    bufferIndex = 0;
    activeBuffer = bufferA;
    readyBuffer = nullptr;
}


// ------ power globals -----
const int POWER_BUTTON_PIN = D3;
#define IMU_POWER_PIN 8 /// check
const uint32_t POWER_HOLD_TIME = 2000;
uint32_t powerButtonStartTime = 0;
bool powerButtonWasPressed = false;

// --------------------------------- sleep mode ------------------------------------
void setupPowerButton(){
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
}
// error codes for info
enum StatusCode : uint8_t {
    STATUS_OK                  = 0x00,

    STATUS_CALIBRATION_START  = 0x01,
    STATUS_CALIBRATION_FAILED = 0x02,
    STATUS_CALIBRATION_SUCCES= 0x03,

    STATUS_RECORDING_STARTED  = 0x04,
    STATUS_RECORDING_STOPPED  = 0x05,

    STATUS_DISCONNECTED       = 0x06,
    STATUS_BUFFER_OVERFLOW    = 0x07,
    STATUS_SENSOR_ERROR       = 0x08,

    STATUS_CALIBRATION_LOADING_FAILED=0x09
};

bool record=false;

bool powerButtonHeld() {

    bool pressed = (digitalRead(POWER_BUTTON_PIN) == LOW);

    if (pressed && !powerButtonWasPressed) {
        powerButtonWasPressed = true;
        powerButtonStartTime = millis();

        Serial.println("Power button pressed");
    }

    if (!pressed && powerButtonWasPressed) {
        powerButtonWasPressed = false;

        Serial.println("Power button released");
    }

    if (pressed && powerButtonWasPressed) {

        if (millis() - powerButtonStartTime >= POWER_HOLD_TIME) {

            powerButtonWasPressed = false;

            Serial.println("2 SECOND HOLD DETECTED");

            return true;
        }
    }

    return false;
}

void enterSystemOff() {

    Serial.println("Entering system off...");

    record = false;
    clear_buffer();

    // Stop BLE
    Bluefruit.Advertising.stop();

    if (Bluefruit.connected()) {
        Bluefruit.disconnect(Bluefruit.connHandle());
    }

    // Wait until the button is released
    while (digitalRead(POWER_BUTTON_PIN) == LOW) {
        delay(10);
    }

    delay(50);

    // Configure D3 / P0.29 as wake-up input
    nrf_gpio_cfg_sense_input(
        NRF_GPIO_PIN_MAP(0, 29),
        NRF_GPIO_PIN_PULLUP,
        NRF_GPIO_PIN_SENSE_LOW
    );

    delay(50);

    Serial.println("System going OFF");

    // Enter nRF52840 SYSTEMOFF
    NRF_POWER->SYSTEMOFF = 1;

    while (true) {
        __WFE();
    }
}


// -------------------------- reading data-------------------------------
void readAccelGyroBurst(int16_t* ax, int16_t* ay, int16_t* az,
                         int16_t* gx, int16_t* gy, int16_t* gz, float* deltat,uint32_t *timestamp) {
    myIMU.readRegisterRegion(rawData, 0x22, 12);

    *gx = (int16_t)rawData[0] | int16_t(rawData[1] << 8);
    *gy = (int16_t)rawData[2] | int16_t(rawData[3] << 8);
    *gz = (int16_t)rawData[4] | int16_t(rawData[5] << 8);
    *ax = (int16_t)rawData[6] | int16_t(rawData[7] << 8);
    *ay = (int16_t)rawData[8] | int16_t(rawData[9] << 8);
    *az = (int16_t)rawData[10] | int16_t(rawData[11] << 8);
    *timestamp=micros();
    if (lastUpdate == 0) {
        *deltat = 1.0f / 416.0f;
    }
    else{
        *deltat = (*timestamp - lastUpdate)/ 1000000.0f;
    }
    lastUpdate = *timestamp;
}

// ------------------------------- calibration -----------------------------------
bool loaded=true;
float sumX = 0, sumY = 0,sumZ = 0;
float sumMagnitude = 0;
const int SAMPLE_RATE = 416;
const int WARMUP_SAMPLES = 2 * SAMPLE_RATE;   // 832 = 2 seconds
const int CALIBRATION_SAMPLES = 5 * SAMPLE_RATE; // 2080 = 5 seconds
const int TOTAL_SAMPLES = WARMUP_SAMPLES + CALIBRATION_SAMPLES;
float gyro_bias_x=0, gyro_bias_y=0, gyro_bias_z=0;
float acc_bias_x=0, acc_bias_y=0, acc_bias_z=0;
float accScale;

void gyro_biases(){
    sumX = 0;
    sumY = 0;
    sumZ = 0;
    lastUpdate=0;
    for (int i=0; i<CALIBRATION_SAMPLES;i++){
        readAccelGyroBurst(&aX, &aY, &aZ, &gX, &gY, &gZ, &deltat,&timestamp);
        sumX+=gX;
        sumY+=gY;
        sumZ+=gZ;
    }
    gyro_bias_x=sumX/CALIBRATION_SAMPLES;
    gyro_bias_y=sumY/CALIBRATION_SAMPLES;
    gyro_bias_z=sumZ/CALIBRATION_SAMPLES;
}

void accel_biases(){
    sumX = 0;
    sumY = 0;
    sumZ = 0;
    filter.q0 = 1.0f;
    filter.q1 = 0.0f;
    filter.q2 = 0.0f;
    filter.q3 = 0.0f;
    filter.beta = 1.0f;
    lastUpdate = 0;
    for (int i=0; i<TOTAL_SAMPLES;i++){
        readAccelGyroBurst(&aX, &aY, &aZ, &gX, &gY, &gZ, &deltat,&timestamp);
        gX=gX-gyro_bias_x;
        gY=gY-gyro_bias_y;
        gZ=gZ-gyro_bias_z;
        float ax = myIMU.calcAccel(aX); // converting to g
        float ay = myIMU.calcAccel(aY);// converting to g
        float az = myIMU.calcAccel(aZ);// converting to g
        float gx= myIMU.calcGyro(gX);
        float gy= myIMU.calcGyro(gY);
        float gz= myIMU.calcGyro(gZ);
        filter.updateIMU(gy, gx, -gz, ay, ax, -az, deltat);
        if (i>=WARMUP_SAMPLES){
            float magnitude = sqrt(
                    ax * ax +
                    ay * ay +
                    az * az
                );
            sumMagnitude += magnitude;
            float q0 = filter.q0;
            float q1 = filter.q1;
            float q2 = filter.q2;
            float q3 = filter.q3;
            // converting
            float ax_g = (1 - 2*q2*q2 - 2*q3*q3)*ay + (2*q1*q2 - 2*q0*q3)*ax + (2*q1*q3 + 2*q0*q2)*-az;
            float ay_g = (2*q1*q2 + 2*q0*q3)*ay + (1 - 2*q1*q1 - 2*q3*q3)*ax + (2*q2*q3 - 2*q0*q1)*-az;
            float az_g = ((2*q1*q3 - 2*q0*q2)*ay + (2*q2*q3 + 2*q0*q1)*ax + (1 - 2*q1*q1 - 2*q2*q2)*-az)-1.0f;


            sumX+=ax_g;
            sumY+=ay_g;
            sumZ+=az_g;
        }
    }
    acc_bias_x=sumX/CALIBRATION_SAMPLES;
    acc_bias_y=sumY/CALIBRATION_SAMPLES;
    acc_bias_z=sumZ/CALIBRATION_SAMPLES;

    float averageMagnitude = sumMagnitude / CALIBRATION_SAMPLES;
    
    if (averageMagnitude > 0.5f && averageMagnitude < 1.5f) {
        accScale = 1.0f / averageMagnitude;
    }
}

// ---------------ble status errors ----------------

BLEService imuService = BLEService("19B10000-E8F2-537E-4F6C-D104768A1214");

BLECharacteristic dataChar =
    BLECharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214");

BLECharacteristic cmdChar =
    BLECharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214");

BLECharacteristic statusChar =
    BLECharacteristic(
        "19B10003-E8F2-537E-4F6C-D104768A1214"
    ); // sending info other than numbers

// sending STATUS_SENSOR_ERROR
void sendStatus(StatusCode code)
{
    uint8_t status = static_cast<uint8_t>(code);

    if (Bluefruit.connected()) {
        statusChar.notify(&status, 1);
    }
}



//----------------- eprom code-----------------

const uint16_t CALIBRATION_VERSION=1;
struct Calibration {
    uint32_t magic;
    uint16_t version;

    float gyro_bias_x;
    float gyro_bias_y;
    float gyro_bias_z;

    float acc_bias_x;
    float acc_bias_y;
    float acc_bias_z;
    float accScale;
    uint32_t crc;
};

    Calibration calibration;
    const uint32_t CALIBRATION_MAGIC = 0xCA1B2026;

uint32_t calculateCalibrationCRC()
{
    return CRC32::calculate(
        (const uint8_t*)&calibration,
        offsetof(Calibration, crc)
    );
}
bool saveCalibration() {
    
    calibration.magic = CALIBRATION_MAGIC;
    calibration.version = CALIBRATION_VERSION;

    calibration.gyro_bias_x = gyro_bias_x;
    calibration.gyro_bias_y = gyro_bias_y;
    calibration.gyro_bias_z = gyro_bias_z;

    calibration.acc_bias_x = acc_bias_x;
    calibration.acc_bias_y = acc_bias_y;
    calibration.acc_bias_z = acc_bias_z;
    calibration.accScale = accScale;
    calibration.crc = calculateCalibrationCRC();

    // write calibration to EEPROM
    auto file =InternalFS.open("calibration.dat",Adafruit_LittleFS_Namespace::FILE_O_WRITE);
     if (!file) {
        sendStatus(STATUS_CALIBRATION_FAILED);
        loaded=false;
        return false;
    }
    size_t written = file.write((const uint8_t*)&calibration,sizeof(calibration));
    file.close();
    if (written != sizeof(calibration)) {
        sendStatus(STATUS_CALIBRATION_FAILED);
        loaded=false;
        Serial.println("failed");
        return false;
    }

    sendStatus(STATUS_CALIBRATION_SUCCES);
    Serial.println("calibration succes");
    loaded=true;
    return true;

}

bool loadCalibration() {
    // read EEPROM into calibration
    auto file =InternalFS.open("calibration.dat");
    if (!file) {
    loaded=false;
    Serial.println("first error");
    return false;
    }
    size_t bytesRead = file.read(&calibration, sizeof(calibration));
    file.close();

    if (bytesRead != sizeof(calibration)) {
        loaded=false;
        Serial.println("second error");
        return false;
    }

    if (calibration.magic != CALIBRATION_MAGIC ||
        calibration.version != CALIBRATION_VERSION) {
        loaded=false;
        Serial.println("third error");
        return false;
    }
    uint32_t stored_crc = calibration.crc;
    if (stored_crc != calculateCalibrationCRC()) {
    loaded=false;
    Serial.println("fourth error");
    return false;
    }

    gyro_bias_x = calibration.gyro_bias_x;
    gyro_bias_y = calibration.gyro_bias_y;
    gyro_bias_z = calibration.gyro_bias_z;

    acc_bias_x = calibration.acc_bias_x;
    acc_bias_y = calibration.acc_bias_y;
    acc_bias_z = calibration.acc_bias_z;
    accScale = calibration.accScale;
    loaded=true;
    return true;
}
bool load_pulse=false;
// ----------------------- ble callbacks --------------------------------
void cmdReceivedCallback(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
    String cmd = "";
    for (uint16_t i = 0; i < len; i++) {
        cmd += (char)data[i];
    }
    cmd.trim();

    if (cmd == "START") {
        
        if (!loaded) {
            Serial.println("START BLOCKED: calibration not loaded");
            return;
        }
        record = true;
        clear_buffer();
        lastUpdate = 0;
        sendStatus(STATUS_RECORDING_STARTED);
    } else if (cmd == "STOP") {
        record = false;
        clear_buffer();
        sendStatus(STATUS_RECORDING_STOPPED);
    }
    else if (cmd=="CALIBRATE"){
        
        record=false;
        clear_buffer();
        sendStatus(STATUS_CALIBRATION_START);
        loaded=false;
        load_pulse=true;
        
    }
}
void connected_callback(uint16_t conn_handle)
{
    BLEConnection* connection = Bluefruit.Connection(conn_handle);
    connection->requestMtuExchange(247);
    delay(500);
    if (!loaded) {
        sendStatus(STATUS_CALIBRATION_LOADING_FAILED);
    }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    record = false;
    clear_buffer();
}

// -------------------------info sending packages------------------------
volatile uint32_t droppedPackets = 0;
uint32_t packetNumber = 0;

void send_pending_packet()
{   
    if (!Bluefruit.connected()) {
    return;
    }   

    if (readyBuffer == nullptr) {
        return;
    }

    // Send the completed buffer
    dataChar.notify(
        (uint8_t*)readyBuffer,
        sizeof(AccelSample) * buffersize
    );

    packetNumber++;

    // Buffer is now available again
    readyBuffer = nullptr;
}

// theoretically we only need to send z accelerations

void buffer_save(){
    activeBuffer[bufferIndex].timestamp=timestamp;
    activeBuffer[bufferIndex].ax=ax_mg;
    activeBuffer[bufferIndex].ay=ay_mg;
    activeBuffer[bufferIndex].az=az_mg;
    bufferIndex++;
    if (bufferIndex < buffersize) {
        return;
    }
    AccelSample* completedBuffer = activeBuffer;

    if (readyBuffer != nullptr) {
        droppedPackets++;
        record = false;
        clear_buffer();
        sendStatus(STATUS_BUFFER_OVERFLOW);
        return;
    }

    
    if (activeBuffer == bufferA) {
        activeBuffer = bufferB;

    } else {
        activeBuffer = bufferA;
    }
    bufferIndex = 0;

   readyBuffer = completedBuffer;
}



void setup(){
    Serial.begin(115200); 
    setupPowerButton();

    // ------------------------ imu settings --------------------------
    //Over-ride default settings if desired
    myIMU.settings.gyroEnabled = 1;  //Can be 0 or 1
    myIMU.settings.gyroRange = 500;   //Max deg/s.  Can be: 125, 245, 500, 1000, 2000
    myIMU.settings.gyroSampleRate = 416;   //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666
    myIMU.settings.gyroBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;
    myIMU.settings.gyroFifoEnabled = 1;  //Set to include gyro in FIFO
    myIMU.settings.gyroFifoDecimation = 1;  //set 1 for on /1

    myIMU.settings.accelEnabled = 1;
    myIMU.settings.accelRange = 4;      //Max G force readable.  Can be: 2, 4, 8, 16
    myIMU.settings.accelSampleRate = 416;  //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666, 3332, 6664, 13330
    myIMU.settings.accelBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;

    myIMU.settings.timestampEnabled=1;    // 1: enable timestamp ; 0: disable timestamp
    myIMU.settings.timestampResolution=1; // 1: Set timestamp resolution ; 0: 6.4ms  1: 25us  

    delay(1000); //relax...
    Serial.println("Processor came out of reset.\n");
    
    //Call .begin() to configure the IMUs
    if( myIMU.begin() != 0 )
    {
        Serial.println("Problem starting the sensor with CS @ Pin 10.");
    }
    else
    {
        Serial.println("Sensor with CS @ Pin 10 started.");
    }
    myIMU.writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL, LSM6DS3_ACC_GYRO_ODR_XL_416Hz | LSM6DS3_ACC_GYRO_FS_XL_4g);
    myIMU.writeRegister(LSM6DS3_ACC_GYRO_CTRL2_G, LSM6DS3_ACC_GYRO_ODR_G_416Hz | LSM6DS3_ACC_GYRO_FS_G_500dps);
    // ---------------------- setting up ble first so we can give feedback ------------------
    Bluefruit.configPrphConn(247,16,8,8);
    Bluefruit.begin();
    Bluefruit.setName("IMU_Sense");
    Bluefruit.Periph.setConnectCallback(connected_callback);
    imuService.begin();
    dataChar.setProperties(CHR_PROPS_NOTIFY);
    dataChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS); // only we write to phone on this
    dataChar.setMaxLen(sizeof(bufferB));
    dataChar.begin();

    cmdChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    cmdChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    cmdChar.setMaxLen(20);
    cmdChar.setWriteCallback(cmdReceivedCallback);
    cmdChar.begin();
    
    // sending info
    statusChar.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    statusChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    statusChar.setFixedLen(1);
    statusChar.begin();

    Bluefruit.Advertising.addService(imuService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.start(0);
    Serial.println("BLE advertising started");
    dataChar.notifyEnabled(true);

    filter.beta=1.0f;
    // filtering and calibration
    if(!InternalFS.begin()){
        Serial.println("LittleFS failed!");
        while (1);
    }

    loadCalibration();
}

void dynamicbeta(float ax,float ay,float az){
    a_mag = sqrtf(ax*ax + ay*ay + az*az);
    deviation = fabsf(a_mag - 1.0f) / 1.0f;
    beta = 0.01f + (0.08f - 0.01f) * expf(-6.0f * deviation);
    filter.beta=beta;
}
void loop() {
    if (powerButtonHeld()) {
        enterSystemOff();
    }
    if (!Bluefruit.connected()){ // do nothing if not connected
        return;
    }
    if (load_first){
        if (!loaded){
        sendStatus(STATUS_CALIBRATION_LOADING_FAILED);
        }
        load_first=false;
    }

    if (!loaded && load_pulse){
        gyro_biases();
        accel_biases();
        saveCalibration();
        loadCalibration();
        load_pulse=false;
    }
    if (!loaded) { 
        return;
    }
    
    
    // checking statuses of data
    myIMU.readRegister(&status, 0x1E);
    bool accelReady = status & 0x01;  // Bit 0 -> XLDA (accelerometer data available)
    bool gyroReady  = status & 0x02;
    if (accelReady && gyroReady) {
        // reading accelerometer data          
        readAccelGyroBurst(&aX, &aY, &aZ, &gX, &gY, &gZ, &deltat,&timestamp);

        float ax = myIMU.calcAccel(aX); // converting to g
        float ay = myIMU.calcAccel(aY);// converting to g
        float az = myIMU.calcAccel(aZ);// converting to g
        ax *= accScale;
        ay *= accScale;
        az *= accScale;
        float gx= myIMU.calcGyro(gX-gyro_bias_x);
        float gy= myIMU.calcGyro(gY-gyro_bias_y);
        float gz= myIMU.calcGyro(gZ-gyro_bias_z);

        // updating orientation as to prevent issues when starting recording.
        filter.updateIMU(gy, gx, -gz, ay, ax, -az, deltat);
        dynamicbeta(ax,ay,az);
        if (record){
            float q0 = filter.q0;
            float q1 = filter.q1;
            float q2 = filter.q2;
            float q3 = filter.q3;
            float norm = sqrtf(
                q0*q0 + q1*q1 + q2*q2 + q3*q3
            );
            if (norm > 1e-9f) {
                q0 /= norm;
                q1 /= norm;
                q2 /= norm;
                q3 /= norm;
            }
            
            // converting
            float ax_g = ((1 - 2*q2*q2 - 2*q3*q3)*ay + (2*q1*q2 - 2*q0*q3)*ax + (2*q1*q3 + 2*q0*q2)*-az);
            float ay_g = ((2*q1*q2 + 2*q0*q3)*ay + (1 - 2*q1*q1 - 2*q3*q3)*ax + (2*q2*q3 - 2*q0*q1)*-az);
            float az_g = (((2*q1*q3 - 2*q0*q2)*ay + (2*q2*q3 + 2*q0*q1)*ax + (1 - 2*q1*q1 - 2*q2*q2)*-az)-1.0f);
            ax_mg = (int16_t)(ax_g * 1000.0f);
            ay_mg = (int16_t)(ay_g * 1000.0f);
            az_mg = (int16_t)(az_g * 1000.0f);
            
            buffer_save();
        }
            
    }
    send_pending_packet();
}