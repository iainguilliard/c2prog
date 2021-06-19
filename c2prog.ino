// c2prog.ino
//
// Flash Ardupilot bootloader to SiK radios via C2 interface
// using an Arduino.
//
// Created by Iain Guilliard on 28/09/2019.
//
// Based on the AVR code in https://github.com/x893/C2.Flash
// Which in turn is based on the code examples provided in
// the orginal Si Labs application note, AN127 Rev. 1.1

// HM-TRP C2 interface
// -------------------
//
// The HopeRF HM-TRP board conveniently provides pads for programming
// the Si1000.  These are the four 2mm-spaced holes next to the LEDs,
// near the edge of the board ('o' in this picture).
//
// +-----------~~
// | O #:#:
// | O = +------+
// | O = |      |
// | O   |      |
// | O o +------+  <- C2CK
// | O o     +-+   <- C2D
// | O o # # | |   <- VDD_MCU
// | O o     +-+   <- GND
// +---^-------~~
//     |
//  This row of 4 holes.
//
// NOTE! Si1000 VDD_MCU is not 5V tolerant! Only connect VDD_MCU to +3.3V
// I/O pins including C2CK and C2D are 5V tolerant


const char *startmessage = R"(


                c2prog
                
Flash Ardupilot bootloader to SiK radios via C2 interface:

  Arduino        HM-TRP
 ~~-------+    +-----------~~
          |    | O #:#:
          |    | O = +------+
          |    | O = |      |
          |    | O   |      |
      D12 | -----> o +------+  <- C2CK
      D11 | -----> o     +-+   <- C2D
     +3V3 | -----> o # # | |   <- VDD_MCU
      GND | -----> o     +-+   <- GND
 ~~-------+    +-----------~~

  Warning! VDD_MCU is NOT 5V tolerant.
 
)";

const char *keys =  R"(
Keys:
    D : Get Device ID
    E : Erase Flash
    B : Burn bootloader
    V : Verify bootloader
    W : Write Flash: WAAAALLbb..bb
    R : Read Flash: RAAAALL
    
        AAAA = 16 bit start address in hex
        LL = length in hex, 0-256 bytes
        bb..bb = LL hex bytes to write

)";

const char *instructions =  R"(
Instructions:
  1) Connect Ardiuno to Sik board C2 interface as shown in the diagram.
  2) Check Device ID. e.g. 1600 = Si100x, rev. A, 0000 = not connected.
  3) Erase Flash.
  4) Burn bootloader.
  5) Verify bootloader (lock byte is not flashed, so ignor failure at address 0xFBFF).

)";

// Ardiuno Pins to C2 interface
// ----------------------------
//
// C2 Connection   Arduino   Uno/Nano/Mini   Mega2560
//                           (ATmega328P)    (ATmega2560)
// ------------------------------------------------------
// C2CK            D12       PB4              PB6     
// C2D             D11       PB3              PB5
// VDD_MCU         +3V3
// GND             GND
// ---------------         
// Ardiuno LED     D13       PB5              PB7
// ------------------------------------------------------

// Set Arduino pins to use 
#define ARDUINO_C2D_PIN 11
#define ARDUINO_C2K_PIN 12
#define ARDUINO_LED_PIN LED_BUILTIN

// Map Arduino pins to ATmega328P or ATmega2560 port B pins
// Use low level access for speed
#define C2CK_BIT  digital_pin_to_bit_mask_PGM[ARDUINO_C2K_PIN] // _BV(6) // mega2560 D12
#define C2CK_PORT PORTB
#define C2CK_PIN  PINB
#define C2CK_DDR  DDRB

#define C2D_BIT   digital_pin_to_bit_mask_PGM[ARDUINO_C2D_PIN] // _BV(5) // mega2560 D11
#define C2D_PORT  PORTB
#define C2D_PIN   PINB
#define C2D_DDR   DDRB

// Comment next line to disable LED
#define LED_BIT   digital_pin_to_bit_mask_PGM[ARDUINO_LED_PIN] // _BV(7) // mega2560 D13
#define LED_PORT  PORTB
#define LED_PIN   PINB
#define LED_DDR   DDRB

#ifdef LED_BIT
  #define LED_ON()  (LED_PORT |= LED_BIT)
  #define LED_OFF() (LED_PORT &= ~LED_BIT)
#else
  #define LED_ON()
  #define LED_OFF()
#endif

// DeviceID definitions
#define C8051F32X_DEVICEID    0x09
#define SI100X_DEVICEID     0x16

// C2 Host command
#define C2_CONNECT_TARGET       0x20
#define C2_DISCONNECT_TARGET    0x21
#define C2_DEVICE_ID            0x22
#define C2_UNIQUE_DEVICE_ID     0x23
#define C2_TARGET_GO            0x24
#define C2_TARGET_HALT          0x25

#define C2_READ_SFR             0x28
#define C2_WRITE_SFR            0x29
#define C2_READ_RAM             0x2A
#define C2_WRITE_RAM            0x2B
#define C2_READ_FLASH           0x2E
#define C2_WRITE_FLASH          0x2F
#define C2_ERASE_PAGE           0x30

#define C2_ADDRESSWRITE         0x38
#define C2_ADDRESSREAD          0x39
#define C2_DATAWRITE            0x3A
#define C2_DATAREAD             0x3B

#define C2_ERASE_FLASH_03       0x3C
#define C2_ERASE_FLASH_04       0x3D
#define C2_READ_XRAM            0x3E
#define C2_WRITE_XRAM           0x3F

#define GET_FIRMWARE_VERSION  0x44
#define SET_FPDAT_ADDRESS   0x45
#define GET_FPDAT_ADDRESS   0x46

// C2 status return codes
#define COMMAND_INVALID     0x00
#define COMMAND_NO_CONNECT    0x01
#define COMMAND_FAILED      0x02
#define COMMAND_TIMEOUT     0x04
#define COMMAND_BAD_DATA    0x05
#define COMMAND_OK        0x0D
#define COMMAND_READ_01     0x81
#define COMMAND_READ_02     0x82
#define COMMAND_READ_03     0x83
#define COMMAND_READ_04     0x84

// C2 FP Command
#define C2_FP_ERASE_DEVICE    0x03
#define C2_FP_READ_FLASH    0x06
#define C2_FP_WRITE_FLASH   0x07
#define C2_FP_ERASE_SECTOR    0x08

#define C2_FP_READ_XRAM     0x0E
#define C2_FP_WRITE_XRAM    0x0F

#define C2ADD_OUTREADY  0x01
#define C2ADD_INBUSY  0x02

#define C2ADD_DEVICEID  0
#define C2ADD_REVISION  1
#define C2ADD_FPCTL   2
byte  C2ADD_FPDAT   = 0xAD;

byte DeviceID   = 0;
byte C2_CONNECTED = 0;
byte LEDS_STATE   = 0;
byte C2_RESPONSE  = 0;

byte cmdPackage[140];
byte cmdPackageLength;
byte cmdPackageGet;

#define STRINGIFY(...) #__VA_ARGS__

char *bootloader_hex = STRINGIFY(
    :0600000002009B02040354
    :03000B0002040BE1
    :03001300020413D1
    :03001B0002041BC1
    :03002300020423B1
    :03002B0002042BA1
    :0300330002043391
    :03003B0002043B81
    :0300430002044371
    :03004B0002044B61
    :0300530002045351
    :03005B0002045B41
    :0300630002046331
    :03006B0002046B21
    :0300730002047311
    :03007B0002047B01
    :03008300020483F1
    :03008B0002048BE1
    :03009300020493D1
    :08009B007581641200A380FED0
    :0B00A300120292E5EFF56130E10375F9
    :0200AE006102ED
    :0A00B00012F800E58224FF920075AB
    :0400BA0062007FFF62
    :0500BE00208202056232
    :0C00C3008F061E8E07EF70F3C296E561F9
    :0A00CF0020E6163000137438256295
    :0D00D900400D90FBFEE493F5C475C30112C9
    :0200E600040014
    :0500E8001200ED80FB99
    :0E00ED00C2961202FCAF82D296BF210280178B
    :0500FB00BF220280128B
    :05010000BF2302800D89
    :05010500BF2602800886
    :05010A00BF2902800383
    :03010F00BF310FEE
    :0E011200C0071202FCAE82D007BE20028001A0
    :0101200022BC
    :03012100BF2100FB
    :0301240050012265
    :06012700EF24CE5001227E
    :0E012D00EF24DFFE240983C0E0EE241483C01B
    :02013B00E022C0
    :02013D005F62FF
    :02013F007581C8
    :02014100A4CE4A
    :02014300E54590
    :020145007B81BC
    :020147008181B4
    :020149008181B2
    :02014B00817CB5
    :01014D007F32
    :02014E000101AD
    :020150000101AB
    :020152000101A9
    :020154000102A6
    :020156000102A4
    :020158000202A1
    :02015A0002029F
    :02015C0002029D
    :01015E00029E
    :03015F0002028217
    :0E01620075824E1202F290FBFEE493F58212BB
    :0501700002F202028210
    :0601750012F836020282BE
    :06017B0012F85E02028290
    :080181001202FCAE828E6375D0
    :0B01890064001202FCAE828E05E4420E
    :0501940063ED4264125E
    :0A01990002FCAE82BE2003020282C7
    :0101A3002239
    :0E01A4001202FCAF82C0071202FCAE82D0072E
    :0501B200BE20028001E7
    :0101B7002225
    :0701B8008563828564830565
    :0601BF0063E4B5630205D4
    :0101C50064D5
    :0601C6008F0812F86E0222
    :0201CC000282AD
    :0701CE00856382856483054F
    :0601D50063E4B5630205BE
    :0101DB0064BF
    :0701DC0012F8981202F20272
    :0201E300028296
    :0B01E5001202FCE582FE24BF50012244
    :0201F0007D0090
    :0C01F200C3ED9E5019ED2421F9C006C099
    :0E01FE0005C0011202FCE582D001D005D0063A
    :04020C00F70D80E288
    :0E021000C0061202FCAD82D006BD20697D0042
    :0A021E00C3ED9E505F856382856486
    :08022800830563E4B5630205E0
    :010230006469
    :0A023100ED2421F98708C006C0057E
    :0A023B0012F86ED005D0060D80D930
    :0E0245001202FCAE82C0061202FCAD82D00690
    :05025300BD202F7D001D
    :0A025800C3ED9E5025856382856486
    :08026200830563E4B5630205A6
    :01026A00642F
    :0E026B00C006C00512F8981202F2D005D006A7
    :030279000D80DC19
    :03027C0043EF103D
    :02027F008001FC
    :01028100225A
    :03028200020286EF
    :010285002256
    :0C0286007582121202F27582100202F260
    :0E029200C2AFAFD95307BF8FD975B64075B252
    :0E02A0008F75A900758840758920758E0875C8
    :0C02AE008D9675981275FF807E5E7F01B2
    :0502BA001EBEFF011F44
    :0E02BF00EE4F70F775EF0675A41075A70F755A
    :0E02CD00A41075A70075E10143D40C43D56061
    :0E02DB0075A70F43A56075A700740F55E44486
    :0902E90007F5E4C28875E3402228
    :0EF8000090F7FEE493FFBF3D0B90F7FFE493FB
    :06F80E00FFBFC2028004EE
    :04F814007F008002EF
    :02F818007F016E
    :03F81A008F8222B8
    :0EF81D00AE82E583FF54FC600574082F500294
    :02F82B00C322F6
    :02F82D00D322E4
    :07F82F0075B7A575B7F122C2
    :04F836007E007FF4DD
    :0EF83A00EF54FC601EC007C00612F82FD00667
    :0EF84800D007758F038E828F8374FFF0758F4B
    :07F8560000EF24FCFF80DD40
    :01F85D002288
    :0EF85E0012F82F758F0790000074FFF0758F61
    :02F86C00002278
    :0EF86E00AE82AF83C007C00612F81DD006D0D0
    :0EF87C00075018C007C00612F82FD006D0079C
    :0CF88A00758F018E828F83E508F0758F6A
    :01F896000071
    :01F89700224E
    :0EF89800AE82AF83C007C00612F81DD006D0A6
    :0CF8A6000750098E828F83E493F58222C4
    :04F8B2007582FF223A
    :01FBFF00FE07
    :01FBFE009175
    :0202F200AF82D9
    :0502F40010990280FBDF
    :0302F9008F9922B8
    :0502FC0010980280FBD8
    :040301008599822236
    :00000001FF
);

byte verify_buffer[140];

void setup()
{
  LED_DDR |= LED_BIT;
  LED_OFF();

  C2CK_DriverOn();
  C2D_DriverOff();
  delay(10);
  
  Serial.begin(115200);
  Serial.print(startmessage);
  Serial.print(keys);
  Serial.print(instructions);
  Serial.print("Device ID: ");
  C2_Reset();
  C2_Device_ID();

  Serial.println();

  C2_Connect_Target();
}

void loop()
{
  delay(1);
  byte c = toupper(Serial.read());
  //byte command = char2hex(c);

  if (c == 'D') {
    
    Serial.print("Device ID: ");
    C2_Reset();
    C2_Device_ID();
    //PutByte(DeviceID);
    Serial.println();
    
  } else if (c == 'R') {
    
    byte ah = Serial.read();
    byte al = Serial.read();
    byte addr_hi = (char2hex(ah) << 4) + char2hex(al);
    ah = Serial.read();
    al = Serial.read();
    byte addr_lo = (char2hex(ah) << 4) + char2hex(al);
    ah = Serial.read();
    al = Serial.read();
    byte len = (char2hex(ah) << 4) + char2hex(al);
    cmdPackage[0] = addr_lo; //addr lo
    cmdPackage[1] = addr_hi; // addr hi
    cmdPackage[2] = len; // length, 0-256 bytes
  
    cmdPackageLength=3;
    cmdPackageGet = 0;

    C2_Connect_Target();
    C2_Read_Memory(C2_FP_READ_FLASH);
    Serial.println();
    
  } else if (c == 'W') {
    
    byte ah = Serial.read();
    byte al = Serial.read();
    byte addr_hi = (char2hex(ah) << 4) + char2hex(al);
    ah = Serial.read();
    al = Serial.read();
    byte addr_lo = (char2hex(ah) << 4) + char2hex(al);
    ah = Serial.read();
    al = Serial.read();
    byte len = (char2hex(ah) << 4) + char2hex(al);
    cmdPackage[0] = addr_lo; //addr lo; //addr lo
    cmdPackage[1] = addr_hi; //addr_hi; // addr hi
    cmdPackage[2] = len; // length, 0-256 bytes
    byte b;

    for (byte i=0; i<len; i++) {

      ah = Serial.read();
      al = Serial.read();
      b = (char2hex(ah) << 4) + char2hex(al);
      cmdPackage[3 + i] = b; // data
      
    }

    cmdPackageLength = 3 + len;
    cmdPackageGet = 0;
  
    C2_Connect_Target();
    C2_Write_Memory(C2_FP_WRITE_FLASH);
    PutByte(C2_RESPONSE);

  } else if (c == 'B') {
    
    byte lock_byte = 0;
    Serial.println("\nBurning hex data to flash:");
    char *data_ptr = bootloader_hex;
    byte pass = 1;
    
    for(;;) {
      
        byte type = char2byte(data_ptr + 7);
        if (data_ptr[0] != ':') {
          
            pass = 0;
            Serial.println("\nERROR: Hex file format\n");
            break;
            
        }

        if (type != 0) break;
        byte len = char2byte(data_ptr + 1);
        byte addr_hi = char2byte(data_ptr + 3);
        byte addr_lo = char2byte(data_ptr + 5);
        data_ptr += 9;
        byte chksum = type + len + addr_hi + addr_lo;
        
        cmdPackage[0] = addr_lo;
        cmdPackage[1] = addr_hi;
        cmdPackage[2] = len;
        
        for(int i=0;i < len;i++) {
          
            cmdPackage[3+i] = char2byte(data_ptr);
            chksum += cmdPackage[3+i];

            if (addr_hi == 0xfb && addr_lo + i == 0xff) {
               //Serial.println("lock byte detected");
               lock_byte = cmdPackage[3+i];
               cmdPackage[3+i] = 0xff;
            }
            data_ptr += 2;
        }
        chksum = 0x100 - chksum;
        if (char2byte(data_ptr) != chksum) {
            pass = 0;
            Serial.print("\nERROR: checksum fail: ");
            PutByte(char2byte(data_ptr));
            Serial.print(" != ");
            PutByte(chksum);
            Serial.println();
            break;
        }
        
        //Serial.println();
        cmdPackageLength = 3 + len;
        cmdPackageGet = 0;
        C2_Connect_Target();
        C2_Write_Memory(C2_FP_WRITE_FLASH);
        if (C2_RESPONSE != 0x0d) {
            pass = 0;
            Serial.print("W C2_RESPONSE: ");
            PutByte(C2_RESPONSE);
            Serial.println();
        }
        cmdPackageLength = 3;
        cmdPackageGet = 0;
        //C2_Connect_Target();
        C2_Read_Memory_to_buffer(C2_FP_READ_FLASH, verify_buffer);
        if (C2_RESPONSE != 0x0d) {
            pass = 0;
            Serial.print("R C2_RESPONSE: ");
            PutByte(C2_RESPONSE);
            Serial.println();
        }
        Serial.print(".");
        for (byte i=0; i<len; i++) {
          //PutByte(verify_buffer[i]);
          if (verify_buffer[i] != cmdPackage[3+i]) {
            pass = 0;
            Serial.println();
            PutByte(addr_hi);
            PutByte(addr_lo);
            Serial.print(" Failed at byte: ");
            PutByte(char2byte(i));
            Serial.print(", Flash byte ");
            PutByte(verify_buffer[i]);
            Serial.print(" != ihex byte: ");
            PutByte(cmdPackage[3+i]);
            Serial.println();
            break;
          } else {
            
          }
        }
        ///Serial.println();
        
        data_ptr += 3;
    }
    Serial.print("\nBurning ihex data to flash: ");
    if (pass == 1) {
      Serial.println("passed.");
    } else {
      Serial.println("failed.");
    }
  } else if (c == 'V') {
    Serial.println("\nVerifing hex data in flash:");
    char *data_ptr = bootloader_hex;
    for(;;) {
        byte type = char2byte(data_ptr + 7);
        if (data_ptr[0] != ':') {
            Serial.println("\nERROR: Hex file format\n");
            break;
        }
        //printf("%c%c ",data_ptr[i*2 + 1], data_ptr[i*2 + 2]);
        //PutByte(type);
        if (type != 0) break;
        byte len = char2byte(data_ptr + 1);
        //PutByte(len);
        byte addr_hi = char2byte(data_ptr + 3);
        
        byte addr_lo = char2byte(data_ptr + 5);
        //printf("%02x ",addr_lo);
        data_ptr += 9;
        byte chksum = type + len + addr_hi + addr_lo;
        cmdPackage[0] = addr_lo;
        cmdPackage[1] = addr_hi;
        cmdPackage[2] = len;
        for(int i=0;i < len;i++) {
            cmdPackage[3+i] = char2byte(data_ptr);
            chksum += cmdPackage[3+i];
            //PutByte(cmdPackage[3+i]);
            data_ptr += 2;
        }
        chksum = 0x100 - chksum;
        if (char2byte(data_ptr) != chksum) {
            Serial.print("\nERROR: checksum fail: ");
            PutByte(char2byte(data_ptr));
            Serial.print(" != ");
            PutByte(chksum);
            Serial.println();
            break;
        }
        //Serial.println();
        cmdPackageLength = 3;
        cmdPackageGet = 0;
        C2_Connect_Target();
        C2_Read_Memory_to_buffer(C2_FP_READ_FLASH, verify_buffer);
        //Serial.print("W C2_RESPONSE: ");
        Serial.print("C2_RESPONSE: ");
        PutByte(C2_RESPONSE);
        Serial.println();
        PutByte(addr_hi);
        PutByte(addr_lo);
        Serial.print(" ");
        for (byte i=0; i<len; i++) {
          PutByte(verify_buffer[i]);
          if (verify_buffer[i] != cmdPackage[3+i]) {
            Serial.print("\nFail verify at byte ");
            PutByte(char2byte(i));
            Serial.print(", Flash byte ");
            PutByte(verify_buffer[i]);
            Serial.print(" != ihex byte ");
            PutByte(cmdPackage[3+i]);
            Serial.println();
            break;
          }
        }
        Serial.println();
        data_ptr += 3;
    }
    Serial.println("\nVerifing ihex data in flash: complete\n");
  } else if (c == 'E') {
    Serial.println("\nErasing flash:\n");
    C2_Erase_Flash_03();
    Serial.print("C2_RESPONSE: ");
    PutByte(C2_RESPONSE);
    Serial.println();
    Serial.println("\nErasing flash: complete\n");
  }
  
  if (C2D_PIN & C2D_BIT)
    LED_ON();
  else
    LED_OFF();
  delay(20);
}

uint8_t char2byte(char *ptr)
{
    byte b =char2hex(*ptr) << 4;
    b |= char2hex(*(ptr + 1));
    return b;
}

byte char2hex(char c)
{
  if (c >= '0' && c <= '9')
    return (c - '0');
  if (c >= 'A' && c <= 'F')
    return (c - 'A' + 0xA);
  if (c >= 'a' && c <= 'f')
    return (c - 'a' + 0xA);
  C2_RESPONSE = COMMAND_BAD_DATA;
  return 0;
}

byte ContainData_Q040(byte count)
{
  if ((cmdPackageLength - cmdPackageGet) == count)
    return 1;
  C2_RESPONSE = COMMAND_BAD_DATA;
  return 0;
}

byte GetNextByte()
{
  byte data = 0;
  if (cmdPackageGet < cmdPackageLength)
    data = cmdPackage[cmdPackageGet++];
  else
    C2_RESPONSE = COMMAND_BAD_DATA;
  return data;
}


/*******************************************************
 * 
 * Connect Target
 * Return:
 * 0x0D
 * 
 *******************************************************/
void C2_Connect_Target()
{
  C2_Reset();
  C2_WriteAR(C2ADD_FPCTL);
  if (C2_WriteDR(0x02) == COMMAND_OK)
  {
    if (1) //(C2_WriteDR(0x04) == COMMAND_OK)
    {
      delayMicroseconds(80);
      if (C2_WriteDR(0x01) == COMMAND_OK)
      {
        delay(25);

        C2_WriteAR(0x00);
        DeviceID = C2_ReadDR();

        Prepare_C2_Device_Params();
        Set_Connected_And_LEDs();
      }
    }
  }
}

void Prepare_C2_Device_Params()
{
  if (DeviceID == C8051F32X_DEVICEID || DeviceID == SI100X_DEVICEID)
    C2ADD_FPDAT = 0xB4;
  else
    C2ADD_FPDAT = 0xAD;
}

void Set_Connected_And_LEDs()
{
  C2_CONNECTED = 1;
  LEDS_STATE = 2;
}

/*******************************************************
 * 
 * Disconnect Target
 * Return:
 * 0x0D
 * 
 *******************************************************/
byte C2_Disconnect_Target()
{
  C2_WriteAR(C2ADD_FPCTL);
  C2_WriteDR(0x00);
  delayMicroseconds(40);
  C2_Reset();
  C2_CONNECTED = 0;
  LEDS_STATE = 0;
  return COMMAND_OK;
}

/*******************************************************
 * 
 * Get Device ID and Revision
 * Return:
 *  DeviceID
 *  Revision
 *  0x0D
 * 
 *******************************************************/
void C2_Device_ID()
{
  C2_WriteAR(C2ADD_DEVICEID);
  PutByte(C2_ReadDR());

  C2_WriteAR(C2ADD_REVISION);
  PutByte(C2_ReadDR());
}

/*******************************************************
 * 
 * Get Unique Device ID
 * Return:
 *  Unique Device ID 1
 *  Unique Device ID 1
 *  0x0D
 * 
 *******************************************************/
void C2_Unique_Device_ID()
{
  byte id1 = 0;
  byte id2 = 0;
  C2_RESPONSE = COMMAND_NO_CONNECT;
  if (C2_CONNECTED)
  {
    if (C2_Write_FPDAT_Read(0x01) == COMMAND_OK)
    {
      id1 = C2_Read_FPDAT();
      if (C2_RESPONSE == COMMAND_OK)
      {
        if (C2_Write_FPDAT_Read(0x02) == COMMAND_OK)
          id2 = C2_Read_FPDAT();
      }
    }
  }
  PutByte(id1);
  PutByte(id2);
}

/*******************************************************
 * 
 * Target Go
 * Return:
 *  1   not connected
 *  0x0D go
 * 
 *******************************************************/
void C2_Target_Go()
{
  C2_RESPONSE = COMMAND_OK;
  if (C2_CONNECTED)
  {
    C2_WriteAR(C2ADD_FPCTL);
    if (C2_WriteDR(0x08) == COMMAND_OK)
    {
      C2CK_DriverOff();
      C2_CONNECTED = 0;
      LEDS_STATE = 1;
    }
  }
}

/*******************************************************
 * 
 * Target Halt
 * Return:
 *  0x0D
 * 
 *******************************************************/
void C2_Target_Halt()
{
  C2_RESPONSE = COMMAND_OK;
  if (!C2_CONNECTED)
  {
    if (C2CK_PIN & C2CK_BIT)
    {
      cli();
      C2CK_DriverOn();
      C2CK_PORT &= ~C2CK_BIT;
      delayMicroseconds(1);
      C2CK_DriverOff();
      sei();
    }
    unsigned long timeout = millis();
    while (!(C2CK_PIN & C2CK_BIT))
    {
      if (millis() - timeout > 1000)
      {
        C2_RESPONSE = COMMAND_FAILED;
        return;
      }
    }
    C2CK_DriverOn();
    Set_Connected_And_LEDs();
  }
}

/*******************************************************
 * 
 * Erase Entire Flash (command 03)
 * Return:
 *   0x0D
 * 
 *******************************************************/
void C2_Erase_Flash_03()
{
  byte data;

  C2_RESPONSE = COMMAND_NO_CONNECT;
  if (!C2_CONNECTED)
    return;
/* Set VDD monitor. Seems not needed for si1000.
  C2_WriteAR(0xFF);
  C2_WriteDR(0x80);

  C2_WriteAR(0xEF);
  C2_WriteDR(0x02);*/

  C2_WriteAR(C2ADD_FPCTL);
  C2_WriteDR(0x02);
  C2_WriteDR(0x01);

  delay(20);

  while (1)
  {
    C2_WriteAR(C2ADD_FPDAT);

    if (C2_WriteDR(0x03) != COMMAND_OK)
    {
      PutByte(0x51);
      break;
    }
    if (Poll_InBusy() != COMMAND_OK)
    {
      PutByte(0x52);
      break;
    }
    if (Poll_OutReady() != COMMAND_OK)
    {
      PutByte(0x53);
      break;
    }
    data = C2_ReadDR();
    if (C2_RESPONSE != COMMAND_OK)
    {
      PutByte(0x54);
      break;
    }
//    if (data != COMMAND_OK)
//    {
//      PutByte(0x55);
//      PutByte(data);
//      break;
//    }

    if (C2_WriteDR(0xDE) != COMMAND_OK)
    {
      PutByte(0x56);
      break;
    }
    if (Poll_InBusy() != COMMAND_OK)
    {
      PutByte(0x57);
      break;
    }

    if (C2_WriteDR(0xAD) != COMMAND_OK)
    {
      PutByte(0x58);
      break;
    }
    if (Poll_InBusy() != COMMAND_OK)
    {
      PutByte(0x59);
      break;
    }

    if (C2_WriteDR(0xA5) != COMMAND_OK)
    {
      PutByte(0x5A);
      break;
    }
    if (Poll_InBusy() != COMMAND_OK)
    {
      PutByte(0x5B);
      break;
    }

    if (Poll_OutReady() != COMMAND_OK)
    {
      PutByte(0x5C);
      break;
    }

    data = C2_ReadDR();
    if (C2_RESPONSE != COMMAND_OK)
    {
      PutByte(0x5D);
      break;
    }
    if (data != COMMAND_OK)
    {
      PutByte(0x5E);
      PutByte(data);
      break;
    }

    break;
  }
}

/*******************************************************
 * 
 * Erase Entire Flash (command 04)
 * Return:
 *   0x0D
 * 
 *******************************************************/
void C2_Erase_Flash_04()
{
  C2_RESPONSE = COMMAND_NO_CONNECT;
  byte data;
  if (C2_CONNECTED)
    if ((data = C2_Write_FPDAT_Read(0x04)) != COMMAND_OK)
      C2_RESPONSE = data;
    else if ((data = C2_Write_FPDAT(0xDE)) != COMMAND_OK)
      C2_RESPONSE = data;
    else if ((data = C2_Write_FPDAT(0xAD)) != COMMAND_OK)
      C2_RESPONSE = data;
    else if ((data = C2_Write_FPDAT(0xA5)) != COMMAND_OK)
      C2_RESPONSE = data;
    else
      C2_RESPONSE = C2_Read_FPDAT();
}

/*******************************************************
 * 
 * Get Firmware Version
 * Return:
 *  version number
 * 
 *******************************************************/
void Get_Firmware_Version()
{
  PutByte(0x44);
  PutByte(0x06);
  C2_RESPONSE = COMMAND_OK;
}

/*******************************************************
 * 
 * Set FPDAT register address
 * 
 *******************************************************/
void Set_FPDAT_Address()
{
  C2ADD_FPDAT = GetNextByte();
  if (C2_RESPONSE == COMMAND_BAD_DATA)
    return;
  C2_RESPONSE = COMMAND_OK;
}

/*******************************************************
 * 
 * Get FPDAT register address
 * 
 *******************************************************/
void Get_FPDAT_Address()
{
  PutByte(C2ADD_FPDAT);
  C2_RESPONSE = COMMAND_OK;
}

/*******************************************************
 * 
 * Read Memory
 *  06, read flash
 *  09, read SFR
 *  0B, read RAM
 *  0E, read XRAM
 * Return:
 *  0xD
 * 
 *******************************************************/
void C2_Read_Memory(byte memType)
{
  byte address16bit = (memType == C2_FP_READ_XRAM || memType == C2_FP_READ_FLASH ? 1 : 0);
  byte lowAddress = GetNextByte();
  byte highAddress = 0;
  if (address16bit)
    highAddress = GetNextByte();
  byte byteCount = GetNextByte();
  byte data;

  if (C2_RESPONSE == COMMAND_BAD_DATA)
    return;

  C2_RESPONSE = COMMAND_NO_CONNECT;
  for (;;)
  {
    if (!C2_CONNECTED)
      break;

    // Write FP Command
    data = C2_Write_FPDAT_Read(memType);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_01;
      break;
    }

    // Write high address for Flash and XRAM
    if (address16bit)
    {
      data = C2_Write_FPDAT(highAddress);
      if (data != COMMAND_OK)
      {
        PutByte(data);
        C2_RESPONSE = COMMAND_READ_02;
        break;
      }
    }

    // Write low address
    data = C2_Write_FPDAT(lowAddress);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_03;
      break;
    }

    // Write byte count
    data = C2_Write_FPDAT(byteCount);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_04;
      break;
    }

    // Read response only for Flash
    if (memType == C2_FP_READ_FLASH)
    {
      C2_RESPONSE = C2_Read_FPDAT();
      if (C2_RESPONSE != COMMAND_OK)
        break;
    }

    // Read data to host
    do //for (; byteCount; byteCount--)
    {
      data = C2_Read_FPDAT();
      if (C2_RESPONSE != COMMAND_OK)
        break;
      PutByte(data);
      byteCount -= 1;
    }  while(byteCount);
    break;
  }
}


/*******************************************************
 * 
 * Read Memory
 *  06, read flash
 *  09, read SFR
 *  0B, read RAM
 *  0E, read XRAM
 * Return:
 *  0xD
 * 
 *******************************************************/
void C2_Read_Memory_to_buffer(byte memType, byte *ptr)
{
  byte address16bit = (memType == C2_FP_READ_XRAM || memType == C2_FP_READ_FLASH ? 1 : 0);
  byte lowAddress = GetNextByte();
  byte highAddress = 0;
  if (address16bit)
    highAddress = GetNextByte();
  byte byteCount = GetNextByte();
  byte data;

  if (C2_RESPONSE == COMMAND_BAD_DATA)
    return;

  C2_RESPONSE = COMMAND_NO_CONNECT;
  for (;;)
  {
    if (!C2_CONNECTED)
      break;

    // Write FP Command
    data = C2_Write_FPDAT_Read(memType);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_01;
      break;
    }

    // Write high address for Flash and XRAM
    if (address16bit)
    {
      data = C2_Write_FPDAT(highAddress);
      if (data != COMMAND_OK)
      {
        PutByte(data);
        C2_RESPONSE = COMMAND_READ_02;
        break;
      }
    }

    // Write low address
    data = C2_Write_FPDAT(lowAddress);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_03;
      break;
    }

    // Write byte count
    data = C2_Write_FPDAT(byteCount);
    if (data != COMMAND_OK)
    {
      PutByte(data);
      C2_RESPONSE = COMMAND_READ_04;
      break;
    }

    // Read response only for Flash
    if (memType == C2_FP_READ_FLASH)
    {
      C2_RESPONSE = C2_Read_FPDAT();
      if (C2_RESPONSE != COMMAND_OK)
        break;
    }

    // Read data to host
    do //for (; byteCount; byteCount--)
    {
      data = C2_Read_FPDAT();
      if (C2_RESPONSE != COMMAND_OK)
        break;
      *ptr = data;
      ptr++;
      byteCount -= 1;
    }  while(byteCount);
    break;
  }
}

/*******************************************************
 * 
 * Write Memory
 *  07, write to flash
 *  0A, write to SFR
 *  0C, write to RAM
 *  0F, write to XRAM
 * Return:
 *  0xD
 * 
 *******************************************************/
void C2_Write_Memory(byte memType)
{
  byte address16bit = (memType == C2_FP_WRITE_XRAM || memType == C2_FP_WRITE_FLASH ? 1 : 0);
  byte lowAddress = GetNextByte();
  byte highAddress = 0;
  if (address16bit)
    highAddress = GetNextByte();
  byte byteCount = GetNextByte();
  //Serial.print("byteCount:");
  //PutByte(byteCount);
  //Serial.println();
  if ((C2_RESPONSE == COMMAND_BAD_DATA) || !ContainData_Q040(byteCount))
    return;

  C2_RESPONSE = COMMAND_NO_CONNECT;
  for (;;)
  {
    if (!C2_CONNECTED)
      break;

    if (C2_Write_FPDAT_Read(memType) != COMMAND_OK)
      break;

    if (address16bit)
      if (C2_Write_FPDAT(highAddress) != COMMAND_OK)
        break;

    if (C2_Write_FPDAT(lowAddress) != COMMAND_OK)
      break;

    if (C2_Write_FPDAT(byteCount) != COMMAND_OK)
      break;

    if (memType == C2_FP_WRITE_FLASH)
      if (C2_Read_FPDAT() != COMMAND_OK)
        break;

    for (; byteCount; byteCount--)
      if (C2_Write_FPDAT(GetNextByte()) != COMMAND_OK)
        break;

    if (C2_RESPONSE == COMMAND_OK)
      Poll_OutReady();

    break;
  }
}

/*******************************************************
 * 
 * Erase Flash Sector
 * Return:
 *  0xD
 * 
 *******************************************************/
void C2_Erase_Flash_Sector(void)
{
  byte sector = GetNextByte();
  if (C2_RESPONSE == COMMAND_BAD_DATA)
    return;

  C2_RESPONSE = COMMAND_NO_CONNECT;
  for (;;)
  {
    if (!C2_CONNECTED)
      break;
    if (C2_Write_FPDAT_Read(C2_FP_ERASE_SECTOR) != COMMAND_OK)
      break;
    if (C2_Write_FPDAT_Read(sector) != COMMAND_OK)
      break;
    C2_Write_FPDAT(0);
    break;
  }
}

/*******************************************************
 * 
 * C2 protocol helpers
 * 
 *******************************************************/

//
// Make RESET
//
byte C2_Reset(void)
{
  cli();
  C2CK_DriverOn();
  C2CK_PORT &= ~C2CK_BIT;
  delayMicroseconds(20);
  C2CK_PORT |= C2CK_BIT;
  delayMicroseconds(2);
  sei();
  C2_RESPONSE = COMMAND_OK;
}

//
// Make pulse on C2CLK line
//
void Pulse_C2CLK(void)
{
  C2CK_PORT &= ~C2CK_BIT;
  asm volatile("nop\n\t");
  asm volatile("nop\n\t");
  C2CK_PORT |= C2CK_BIT;
}

//
// Drive On/Off C2D line
//
void C2D_DriverOn()
{
  C2D_DDR |= C2D_BIT;
}

void C2D_DriverOff()
{
  C2D_PORT &= ~C2D_BIT;
  C2D_DDR  &= ~C2D_BIT;
}

//
// Drive On/Off C2CLK line
//
void C2CK_DriverOn()
{
  C2CK_PORT |= C2CK_BIT;
  C2CK_DDR  |= C2CK_BIT;
}

void C2CK_DriverOff()
{
  C2CK_PORT |=  C2CK_BIT;
  C2CK_DDR  &= ~C2CK_BIT;
}

//
// Write to FPDAT register
//
byte C2_Write_FPDAT_Read(byte data)
{
  if (C2_Write_FPDAT(data) == COMMAND_OK)
    return C2_Read_FPDAT();
  return 0;
}

//
// Wait for set OutReady
//
byte Poll_InBusy()
{
  C2_RESPONSE = COMMAND_OK;
  unsigned int timeout = 50000;
  byte h_timeout = 0;
  while ((C2_ReadAR() & C2ADD_INBUSY))
  {
    if (!h_timeout)
    {
      h_timeout = 50;
      if (--timeout == 0)
      {
        C2_RESPONSE = COMMAND_TIMEOUT;
        break;
      }
    }
    --h_timeout;
    delayMicroseconds(1);
  }
  return C2_RESPONSE;
}

//
// Wait for set OutReady
//
byte Poll_OutReady()
{
  C2_RESPONSE = COMMAND_OK;
  unsigned int timeout = 50000;
  byte h_timeout = 0;
  while (!(C2_ReadAR() & C2ADD_OUTREADY))
  {
    if (!h_timeout)
    {
      h_timeout = 50;
      if (--timeout == 0)
      {
        C2_RESPONSE = COMMAND_TIMEOUT;
        break;
      }
    }
    --h_timeout;
    delayMicroseconds(1);
  }
  return C2_RESPONSE;
}

//
// Read from FPDAT
//
byte C2_Read_FPDAT()
{
  if (Poll_OutReady() == COMMAND_OK)
  {
    C2_WriteAR(C2ADD_FPDAT);
    return C2_ReadDR();
  }
  return 0;
}

//
// Write to FPDAT with wait InBusy clear
//
byte C2_Write_FPDAT(byte data)
{
  C2_WriteAR(C2ADD_FPDAT);

  if (C2_WriteDR(data) == COMMAND_OK)
  {
    unsigned int timeout = 50000;
    byte h_timeout = 0;
    while (C2_ReadAR() & C2ADD_INBUSY)
    {
      if (!h_timeout)
      {
        h_timeout = 50;
        if (--timeout == 0)
        {
          C2_RESPONSE = COMMAND_TIMEOUT;
          break;
        }
      }
      --h_timeout;
      delayMicroseconds(1);
    }
  }
  return C2_RESPONSE;
}

//-----------------------------------------------------------------------------------
// C2_ReadAR()
//-----------------------------------------------------------------------------------
// - Performs a C2 Address register read
// - Returns the 8-bit register content
//
byte C2_ReadAR()
{
  cli();
  // START field
  Pulse_C2CLK();

  // INS field (10b, LSB first)
  C2D_PORT &= ~C2D_BIT;
  C2D_DriverOn();
  Pulse_C2CLK();
  C2D_PORT |= C2D_BIT;
  Pulse_C2CLK();
  C2D_DriverOff();

  // ADDRESS field
  byte data = 0;
  for (byte i = 0; i < 8; i++)
  {
    Pulse_C2CLK();
    data = data >> 1;
    data &= 0x7F;
    if (C2D_PIN & C2D_BIT)
      data |= 0x80;
  }

  // STOP field
  Pulse_C2CLK();
  sei();

  C2_RESPONSE = COMMAND_OK;
  return data;
}

//-----------------------------------------------------------------------------------
// C2_WriteAR()
//-----------------------------------------------------------------------------------
// - Performs a C2 Address register write (writes the <addr> input 
//   to Address register)
//
void C2_WriteAR(byte addr)
{
  cli();
  // START field
  Pulse_C2CLK();

  // INS field (11b, LSB first)
  C2D_PORT |= C2D_BIT;
  C2D_DriverOn();
  Pulse_C2CLK();
  Pulse_C2CLK();

  // ADDRESS field
  for (byte i = 0; i < 8; i++)
  {
    if (addr & 0x01)
      C2D_PORT |= C2D_BIT;
    else
      C2D_PORT &= ~C2D_BIT;
    addr = addr >> 1;
    Pulse_C2CLK();
  }

  // STOP field
  C2D_DriverOff();
  Pulse_C2CLK();
  sei();

  C2_RESPONSE = COMMAND_OK;
  return;
}

//-----------------------------------------------------------------------------------
// C2_ReadDR()
//-----------------------------------------------------------------------------------
// - Performs a C2 Data register read
// - Returns the 8-bit register content
//
byte C2_ReadDR()
{
  cli();
  // START field
  Pulse_C2CLK();

  C2D_PORT &= ~C2D_BIT;
  C2D_DriverOn();

  Pulse_C2CLK();    // INS field (00b, LSB first)
  Pulse_C2CLK();

  Pulse_C2CLK();    // LENGTH field (00b -> 1 byte)
  Pulse_C2CLK();

  // WAIT field
  C2D_DriverOff();  // Disable C2D driver for input
  byte retry = 0;
  byte data = 0;
  do
    Pulse_C2CLK();
  while (--retry && !(C2D_PIN & C2D_BIT));

  if (retry)
  {
    // DATA field
    for (byte i = 0; i < 8; i++)
    {
      Pulse_C2CLK();
      data = data >> 1;
      if (C2D_PIN & C2D_BIT)
        data |= 0x80;
      else
        data &= 0x7F;
    }

    // STOP field
    Pulse_C2CLK();
  }

  sei();

  C2_RESPONSE = (retry ? COMMAND_OK : COMMAND_TIMEOUT);
  return data;
}

//-----------------------------------------------------------------------------------
// C2_WriteDR()
//-----------------------------------------------------------------------------------
// - Performs a C2 Data register write (writes <data> input to data register)
//
byte C2_WriteDR(byte data)
{
  cli();
  // START field
  Pulse_C2CLK();

  // INS field (01b, LSB first)
  C2D_PORT |= C2D_BIT;
  C2D_DriverOn();
  Pulse_C2CLK();
  C2D_PORT &= ~C2D_BIT;
  Pulse_C2CLK();

  // LENGTH field (00b -> 1 byte)
  Pulse_C2CLK();
  Pulse_C2CLK();

  // DATA field
  for (byte i = 0; i < 8; i++)
  {
    if (data & 0x01)
      C2D_PORT |= C2D_BIT;
    else
      C2D_PORT &= ~C2D_BIT;
    data = data >> 1;
    Pulse_C2CLK();
  }

  C2D_DriverOff();
  byte retry = 200;
  do
    Pulse_C2CLK();
  while (--retry && !(C2D_PIN & C2D_BIT));

  // STOP field
  Pulse_C2CLK();

  sei();

  C2_RESPONSE = (retry ? COMMAND_OK : COMMAND_TIMEOUT);
  return C2_RESPONSE;
}

//
// Blink LED
//
void BlinkLED(byte flashes)
{
  delay(100);
  while (flashes-- > 0)
  {
    LED_ON();
    delay(100);
    LED_OFF();
    delay(250);
  }
}

char hex2char(byte data)
{
  if (data >= 0x0A)
    Serial.write(data + ('A' - 0x0A));
  else
    Serial.write(data + '0');
}

void PutByte(byte data)
{
  hex2char((data >> 4) & 0x0F);
  hex2char(data & 0x0F);
}
