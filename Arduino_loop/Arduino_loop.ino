//Writing to files
#include <SPI.h>
#include <SD.h>

//GPS Headers
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"
//#include "MPU6050.h" // not necessary if using MotionApps include file

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
#include "Wire.h"
#endif

// class default I2C address is 0x68
// specific I2C addresses may be passed as a parameter here
// AD0 low = 0x68 (default for SparkFun breakout and InvenSense evaluation board)
// AD0 high = 0x69
MPU6050 mpu;
//MPU6050 mpu(0x69); // <-- use for AD0 high

// uncomment "OUTPUT_READABLE_WORLDACCEL" if you want to see acceleration
// components with gravity removed and adjusted for the world frame of
// reference (yaw is relative to initial orientation, since no magnetometer
// is present in this case). Could be quite handy in some cases.
#define OUTPUT_READABLE_WORLDACCEL//i think we want this one



#define LED_PIN 13 // (Arduino is 13, Teensy is 11, Teensy++ is 6)

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaReal_p;   // [x, y, z]          previous gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorInt16 aaWorld_p;  // [x, y, z]            previous world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
//float euler[3];         // [psi, theta, phi]    Euler angle container
//float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// packet structure for InvenSense teapot demo
//uint8_t teapotPacket[14] = { '$', 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, '\r', '\n' };


//Thresholds
const float jerk_thershold = 8;

//text message buffer
String text_msg = "";

//functions
void text_load(String tag, String data);
void text_send();
void displayInfo();
void uploadScript();

//iterator. we use this so we dont record data for gps as often as acceleration data
int iterator = 0;

//GPS constants
static const int RXPin = 8, TXPin = 9;
static const uint32_t GPSBaud = 9600;

// The TinyGPS++ object
TinyGPSPlus gps;

// The serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin);


//PrintWriter output;
File dataFile;


// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady()
{
    mpuInterrupt = true;
}



// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================



void setup()
{
    //writing to txt
    //Bridge.begin();
    //FileSystem.begin();
    SD.begin(4);

    //GPS
    Serial.begin(115200);
    ss.begin(GPSBaud);
   
    //Acceleration
    // join I2C bus (I2Cdev library doesn't do this automatically)
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    Wire.begin();
    TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz)
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
#endif

   
    while (!Serial) ; // wait for Leonardo enumeration, others continue immediately


    // initialize device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // wait for ready
    Serial.println(F("\nSend any character to begin DMP programming and demo: "));
    while (Serial.available() && Serial.read()) ; // empty buffer
    while (!Serial.available()) ;                 // wait for data
    while (Serial.available() && Serial.read()) ; // empty buffer again

    // load and configure the DMP
    Serial.println(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();

    

    // make sure it worked (returns 0 if so)
    if (devStatus == 0)
    {
        // turn on the DMP, now that it's ready
        Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    }
    else
    {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

    // configure LED for output
    pinMode(LED_PIN, OUTPUT);

    //This just tells us not to compare the first value to nothing
    aaReal_p.x = -100;
    aaWorld_p.x = -100;
   
   //Bridge.begin();
}



// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================

void loop()
{
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    // wait for MPU interrupt or extra packet(s) available
    while (!mpuInterrupt && fifoCount < packetSize)
    {
        // other program behavior stuff here

    }

    // reset interrupt flag and get INT_STATUS byte
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024)
    {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        //Serial.println(F("FIFO overflow!"));// we get this a lot

        // otherwise, check for DMP data ready interrupt (this should happen frequently)
    }
    else if (mpuIntStatus & 0x02)
    {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);

        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;

//# ifdef OUTPUT_READABLE_QUATERNION
        // display quaternion values in easy matrix form: w x y z
        //mpu.dmpGetQuaternion(&q, fifoBuffer);
        //Serial.print("quat\t");
        //Serial.print(q.w);
        //Serial.print("\t");
        //Serial.print(q.x);
      //  Serial.print("\t");
        //Serial.print(q.y);
    //    Serial.print("\t");
  //      Serial.println(q.z);
//#endif

//# ifdef OUTPUT_READABLE_EULER
        // display Euler angles in degrees
        //mpu.dmpGetQuaternion(&q, fifoBuffer);
        //mpu.dmpGetEuler(euler, &q);
        //Serial.print("euler\t");
        //Serial.print(euler[0] * 180 / M_PI);
        //Serial.print("\t");
      //  Serial.print(euler[1] * 180 / M_PI);
    //    Serial.print("\t");
  //      Serial.println(euler[2] * 180 / M_PI);
//#endif

//# ifdef OUTPUT_READABLE_YAWPITCHROLL
        // display Euler angles in degrees
        //mpu.dmpGetQuaternion(&q, fifoBuffer);
        //mpu.dmpGetGravity(&gravity, &q);
        //mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
      //  Serial.print("ypr\t");
        //Serial.print(ypr[0] * 180 / M_PI);
      //  Serial.print("\t");
      //  Serial.print(ypr[1] * 180 / M_PI);
    //    Serial.print("\t");
  //      Serial.println(ypr[2] * 180 / M_PI);
//#endif

        //THis is all we care about!!!
# ifdef OUTPUT_READABLE_REALACCEL
        // display real acceleration, adjusted to remove gravity
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetAccel(&aa, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
        Serial.print("areal\t");
        Serial.print(aaReal.x);
        Serial.print("\t");
        Serial.print(aaReal.y);
        Serial.print("\t");
        Serial.println(aaReal.z);

        //Logic for dropage
        if (aaReal_p.x != -100)
        {
            //jerk calculations. if Jerk > 8.
            //magnitude of the jerk. in this cae direction doesnt matter
            //also for fast computation we only care about the jerk^2

            float jerk_mag_squared = sq(3 * (aaReal.x - aaReal_p.x)) + sq(3 * (aaReal.y - aaReal_p.y)) + sq(3 * (aaReal.z - aaReal_p.z));


            if (jerk_mag_squared > sq(jerk_thershold))
            {
                char c[200] = "Possible drop: Jerk^2[>8]: ";
                sprintf(c, "%f", jerk_mag_squared);
                text_load("msg", c);
            }
        }
        //set prev values at end of loop, init at -100;
        aaReal_p.x = aaReal.x;
        aaReal_p.x = aaReal.x;
        aaReal_p.x = aaReal.x;
#endif

# ifdef OUTPUT_READABLE_WORLDACCEL
        // display initial world-frame acceleration, adjusted to remove gravity
        // and rotated based on known orientation from quaternion
        mpu.dmpGetQuaternion(&q, fifoBuffer);
        mpu.dmpGetAccel(&aa, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
        mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &q);
        Serial.print("aworld\t");
        Serial.print(aaWorld.x);
        Serial.print("\t");
        Serial.print(aaWorld.y);
        Serial.print("\t");
        Serial.println(aaWorld.z);

        //Logic for dropage
        if (aaWorld_p.x != -100)
        {
            //jerk calculations. if Jerk > 8.
            //magnitude of the jerk. in this cae direction doesnt matter
            //also for fast computation we only care about the jerk^2

            float jerk_mag_squared = sq(3 * (aaWorld.x - aaWorld_p.x)) + sq(3 * (aaWorld.y - aaWorld_p.y)) + sq(3 * (aaWorld.z - aaWorld_p.z));


            if (jerk_mag_squared > sq(jerk_thershold))
            {
                char c[200] = "Possible drop: Jerk^2[>8]: ";
                sprintf(c, "%f", jerk_mag_squared);
                text_load("msg", c);
            }
        }
        //set prev values at end of loop, init at -100;
        aaWorld_p.x = aaWorld.x;
        aaWorld_p.x = aaWorld.x;
        aaWorld_p.x = aaWorld.x;
#endif

//# ifdef OUTPUT_TEAPOT
  //      // display quaternion values in InvenSense Teapot demo format:
    //    teapotPacket[2] = fifoBuffer[0];
      //  teapotPacket[3] = fifoBuffer[1];
        //teapotPacket[4] = fifoBuffer[4];
        //teapotPacket[5] = fifoBuffer[5];
        //teapotPacket[6] = fifoBuffer[8];
        //teapotPacket[7] = fifoBuffer[9];
        //teapotPacket[8] = fifoBuffer[12];
        //teapotPacket[9] = fifoBuffer[13];
        //Serial.write(teapotPacket, 14);
        //teapotPacket[11]++; // packetCount, loops at 0xFF on purpose
//#endif


    }

    if (iterator % 50 == 0)
    {
        iterator = 0;
        //Other codes: Water level
       // int water_reading = analogRead(A1); // Incoming analog signal read and appointed sensor
        //Serial.println(water_reading);
        

        //GPS
        // This sketch displays information every time a new sentence is correctly encoded.
        while (ss.available() > 0)
            if (gps.encode(ss.read()))
                displayInfo();
    }

    text_send();
    iterator++;
    delay(300);
}

void text_load(String tag, String data)
{
    text_msg += (tag + ":" + data + "\n");
}

void text_send()
{
  dataFile = SD.open("TextMessage.txt", FILE_WRITE);

  dataFile.print(text_msg);
    text_msg = "";
}

void displayInfo()
{
   Serial.print(F("Location: "));
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.println(gps.location.lng(), 6);
     
    char c[200] = "";
    sprintf(c, "(%f,%f)", gps.location.lat(), gps.location.lng());
    text_load("gps", c);
}
