#include <LSM6DS3.h>
#include <Wire.h>

//Create a instance of class LSM6DS3
LSM6DS3 myIMU(I2C_MODE, 0x6A);    //I2C device address 0x6A
void printFifoPattern()
{
    uint8_t status3;
    uint8_t status4;

    myIMU.readRegister(&status3, 0x3C);
    myIMU.readRegister(&status4, 0x3D);

    uint16_t pattern =
        ((status4 & 0x03) << 8) |
        status3;

    Serial.print("FIFO pattern = ");
    Serial.println(pattern);
}
void setup( void ) {
  //Over-ride default settings if desired
  myIMU.settings.gyroEnabled = 1;  //Can be 0 or 1
  myIMU.settings.gyroRange = 2000;   //Max deg/s.  Can be: 125, 245, 500, 1000, 2000
  myIMU.settings.gyroSampleRate = 416;   //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666
  myIMU.settings.gyroBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;
  myIMU.settings.gyroFifoEnabled = 1;  //Set to include gyro in FIFO
  myIMU.settings.gyroFifoDecimation = 1;  //set 1 for on /1

  myIMU.settings.accelEnabled = 1;
  myIMU.settings.accelRange = 16;      //Max G force readable.  Can be: 2, 4, 8, 16
  myIMU.settings.accelSampleRate = 416;  //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666, 3332, 6664, 13330
  myIMU.settings.accelBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;
  myIMU.settings.accelFifoEnabled = 1;  //Set to include accelerometer in the FIFO
  myIMU.settings.accelFifoDecimation = 1;  //set 1 for on /1
  myIMU.settings.tempEnabled = 1;
  //Non-basic mode settings
  myIMU.settings.commMode = 1;

  myIMU.settings.timestampEnabled=0;    // 1: enable timestamp ; 0: disable timestamp
  myIMU.settings.timestampFifoEnabled=0;// 1: enable write timestamp into fifo ; 0: disable
  myIMU.settings.timestampResolution=1; // 1: Set timestamp resolution ; 0: 6.4ms  1: 25us  

  //FIFO control settings
  myIMU.settings.fifoThreshold = 100;  //Can be 0 to 4096 (16 bit bytes)
  myIMU.settings.fifoSampleRate = 400;  //Hz.  Can be: 10, 25, 50, 100, 200, 400, 800, 1600, 3300, 6600
  myIMU.settings.fifoModeWord = 6;  //FIFO mode.
  //FIFO mode.  Can be:
  //  0 (Bypass mode, FIFO off)
  //  1 (Stop when full)
  //  3 (Continuous during trigger)
  //  4 (Bypass until trigger)
  //  6 (Continous mode)
  

  Serial.begin(115200);  // start serial for output
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
  
  Serial.print("Configuring FIFO with no error checking...");
  myIMU.fifoBegin();
  Serial.print("Done!\n");
  
  Serial.print("Clearing out the FIFO...");
  myIMU.fifoClear();
  Serial.print("Done!\n");
  
}



void loop()
{
  printFifoPattern();

  int16_t w0 = myIMU.fifoRead();
  int16_t w1 = myIMU.fifoRead();
  int16_t w2 = myIMU.fifoRead();
  int16_t w3 = myIMU.fifoRead();
  int16_t w4 = myIMU.fifoRead();
  int16_t w5 = myIMU.fifoRead();

  Serial.print("RAW: ");
  Serial.print(w0);
  Serial.print(", ");
  Serial.print(w1);
  Serial.print(", ");
  Serial.print(w2);
  Serial.print(", ");
  Serial.print(w3);
  Serial.print(", ");
  Serial.print(w4);
  Serial.print(", ");
  Serial.println(w5);
}