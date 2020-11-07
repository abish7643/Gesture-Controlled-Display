#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>
#include <SparkFun_APDS9960.h>

// Replace the next variables with your SSID/Password combination
const char *ssid = "Dhanish2";
const char *password = "reset123@";

// NTPServer
// India Requires an Offset of (+5:30 = 330m)
const char *ntpServer = "asia.pool.ntp.org"; // Asia Time
const long gmtOffset_sec = 270 * 60;         // +4:30 hr (270m)
const int daylightOffset_sec = 60 * 60;      // +1:00 hr (60m)
int minute, hour, second, date, month, year; // Global Variables

#define SCREEN_WIDTH 128 // OLED Display Width, in Pixels
#define SCREEN_HEIGHT 32 // OLED Display Height, in Pixels

// SSD1306 Display Connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset Pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins
#define APDS9960_INT 25 // Needs to be an interrupt pin
#define DATA_PIN 26     // Data Pin

// Navigation Constant & Variables
const String MonthsArray[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const short int MinuteStepsArray[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55};
short int MinuteArrayPosition = 6, MonthArrayPosition = 6; // Array Positions (Will be Updated By ParseLocalTime())

bool InitialLoop = true, SkipIntialHandle = true;      // Bool To Initate Additional Actions
bool isYearSelected = true, isMonthSelected = false;   // Year & Month Selected Bool
bool isDateSelected = false;                           // Date Selected Bool
bool isHourSelected = false, isMinuteSelected = false; // Time Selected Bool
bool isDurationSelected[] = {true, false};             // Session Duration (Hour & Minute) Selected Bool

short int YearSelected, MonthSelected;  // Year Value & Month Index Selected
short int DateSelected = 1;             // Date Value Selected
short int HourSelected, MinuteSelected; // Hour Value & Minute Index Selected
short int DurationSelected[2] = {1, 0}; // Session Duration (Hour Value & Minute Index) Selected
short MonthDaysLeft = 0;                // Days In Month (Assign According to Month)

// Four Steps - Hours & Minutes, Year & Month, Date, Time Selection
bool StepsCompleted[] = {false, false, false, false};

// Millis
unsigned long currentMillis, prevMillis;

// APDS 9960 Sensor
SparkFun_APDS9960 apds = SparkFun_APDS9960();                                          // Sensor Instance
int isr_flag = 0;                                                                      // Interrupt Flag
bool Gesture_Up, Gesture_Down, Gesture_Left, Gesture_Right, Gesture_Far, Gesture_Near; // Gesture Bool

// Functions
void Drawbitmap();
void interruptRoutine();
void handleGesture();
void ControlNavigation();
void UpdateI2CDisplay();
void SetCursorCenterX(String str, int x, int y);
void SetCursorCenterXY(String str, int x, int y);

void setup_wifi();
void ParseLocalTime();

void UpdateDurationHourMinute(short int HourSkip, short int MinuteSkip);
void UpdateYearMonth(short int YearSkip, short int MonthSkip);
void FindMonthDays();
void UpdateDate(short int DateSkip);
void UpdateTime(short int TimeSkip);

void setup()
{
  Serial.begin(115200);
  delay(2000);

  // ----------------I2C DISPLAY----------------
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
  }
  else
  {
    Serial.println("---------I2C Display - 0x3C----------");
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  display.clearDisplay();
  Drawbitmap();
  // delay(2000); // Pause for 2 seconds

  // -------------------------------------------

  // ---------------- APDS 9960 ----------------
  // Set interrupt pin as input
  pinMode(APDS9960_INT, INPUT);

  // Initialize Serial port
  Serial.println();
  Serial.println(F("--------------------------------"));
  Serial.println(F("SparkFun APDS-9960 - GestureTest"));
  Serial.println(F("--------------------------------"));

  // Initialize interrupt service routine
  attachInterrupt(APDS9960_INT, interruptRoutine, FALLING);

  // Initialize APDS-9960 (configure I2C and initial values)
  if (apds.init())
  {
    Serial.println(F("APDS-9960 initialization complete"));
  }
  else
  {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }

  // Start running the APDS-9960 gesture sensor engine
  if (apds.enableGestureSensor(true))
  {
    Serial.println(F("Gesture sensor is now running"));

    // if (apds.setLEDDrive(0))
    // {
    //   Serial.println("LED Drive Set");
    // }
    if (apds.setGestureGain(GGAIN_2X))
    {
      Serial.println("Gesture Gain Set");
    }
  }
  else
  {
    Serial.println(F("Something went wrong during gesture sensor init!"));
  }
  // -------------------------------------------

  setup_wifi();

  // Connect To NTP Server For Time & Date
  Serial.println("---Configuring Time From NTP Server---");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  ParseLocalTime();

  // Clear the buffer
  display.clearDisplay();
  UpdateI2CDisplay();

  prevMillis = 0;
}

void loop()
{
  currentMillis = millis();

  if ((currentMillis - prevMillis > 60 * 1000) && StepsCompleted[0] == true)
  {
    ParseLocalTime();
    prevMillis = currentMillis;
  }

  if (InitialLoop)
  {
    UpdateI2CDisplay();
    InitialLoop = false;
  }

  if (isr_flag == 1)
  {
    detachInterrupt(APDS9960_INT);
    // Serial.println("Interrupt Function");
    handleGesture();
    ControlNavigation();
    isr_flag = 0;
    attachInterrupt(APDS9960_INT, interruptRoutine, FALLING);
  }
}

/*
Set ISR Flag
*/
void interruptRoutine()
{
  isr_flag = 1;
}

/*
Read the Gesture from APDS 9960 & 
Find The Type of Gesture & Set Global State
UP, DOWN, RIGHT, LEFT, NEAR, FAR & NONE
*/
void handleGesture()
{
  bool GestureAvailability = apds.isGestureAvailable();
  // Serial.print("Gesture Avail : ");
  // Serial.println(GestureAvailability);

  if (GestureAvailability)
  {
    int GestureType = apds.readGesture();
    // Serial.print("Gesture Type : ");
    // Serial.println(GestureType);

    switch (GestureType)
    {
    case DIR_UP:
      Serial.println("UP");
      Gesture_Up = true;
      break;
    case DIR_DOWN:
      Serial.println("DOWN");
      Gesture_Down = true;
      break;
    case DIR_LEFT:
      Serial.println("LEFT");
      Gesture_Left = true;
      break;
    case DIR_RIGHT:
      Serial.println("RIGHT");
      Gesture_Right = true;
      break;
    case DIR_NEAR:
      Serial.println("NEAR");
      Gesture_Near = true;
      break;
    case DIR_FAR:
      Serial.println("FAR");
      Gesture_Far = true;
      break;
    default:
      Serial.println("NONE");
    }
  }
}

/*
Updates I2C Display As Splash Screen, 
Shows the next required input.
*/
void UpdateI2CDisplay()
{
  display.clearDisplay();
  display.setTextSize(1); // Draw 2X-scale text
  display.setTextColor(WHITE);

  if (StepsCompleted[0] == false)
  {
    display.setCursor(26, 12);
    display.println("SET DURATION");
    display.setTextSize(1);
    display.setCursor(24, 24);
    display.println("Wave To Start");
  }

  if (StepsCompleted[0] == true && StepsCompleted[1] == false)
  {
    display.setCursor(16, 12);
    display.println("SET YEAR & MONTH");
    display.setTextSize(1);
    display.setCursor(24, 24);
    display.println("Wave To Start");
  }

  if (StepsCompleted[1] == true && StepsCompleted[2] == false)
  {
    display.setCursor(16, 12);
    display.println("SET DATE & TIME");
    display.setTextSize(1);
    display.setCursor(24, 24);
    display.println("Wave To Start");
  }

  display.display();
}

/*
Use Gesture Bool to Initate Actions, 
Control All Parameters.
*/
void ControlNavigation()
{
  // --------------GESTURE UP--------------

  if (Gesture_Up)
  {
    // Checks For Which Step Has to Be Completed,
    // And Initate Required Sequential Step
    if (StepsCompleted[0] == false)
    {
      UpdateDurationHourMinute(2, 3);
    }
    else if (StepsCompleted[1] == false && StepsCompleted[0] == true)
    {
      UpdateYearMonth(1, 3);
    }
    else if (StepsCompleted[2] == false && StepsCompleted[1] == true)
    {
      UpdateDate(3);
    }
    Gesture_Up = false;
  }
  // --------------------------------------

  // -------------GESTURE DOWN-------------

  if (Gesture_Down)
  {
    // Checks For Which Step Has to Be Completed,
    // And Initate Required Sequential Step
    if (StepsCompleted[0] == false)
    {
      UpdateDurationHourMinute(-2, -3);
    }
    else if (StepsCompleted[1] == false && StepsCompleted[0] == true)
    {
      UpdateYearMonth(-1, -3);
    }
    else if (StepsCompleted[2] == false && StepsCompleted[1] == true)
    {
      UpdateDate(-3);
    }
    Gesture_Down = false;
  }
  // --------------------------------------

  // -------------GESTURE LEFT-------------

  if (Gesture_Left)
  {
    // Checks For Which Step Has to Be Completed,
    // And Initate Required Sequential Step
    if (StepsCompleted[0] == false)
    {
      UpdateDurationHourMinute(-1, -1);
    }
    else if (StepsCompleted[1] == false && StepsCompleted[0] == true)
    {
      UpdateYearMonth(-1, -1);
    }
    else if (StepsCompleted[2] == false && StepsCompleted[1] == true)
    {
      UpdateDate(-1);
    }
    Gesture_Left = false;
  }
  // --------------------------------------

  // ------------GESTURE RIGHT-------------

  if (Gesture_Right)
  {
    // Checks For Which Step Has to Be Completed,
    // And Initate Required Sequential Step
    if (StepsCompleted[0] == false)
    {
      UpdateDurationHourMinute(1, 1);
    }
    else if (StepsCompleted[1] == false && StepsCompleted[0] == true)
    {
      UpdateYearMonth(1, 1);
    }
    else if (StepsCompleted[2] == false && StepsCompleted[1] == true)
    {
      UpdateDate(1);
    }
    Gesture_Right = false;
  }
  // --------------------------------------

  // -------------GESTURE NEAR-------------

  // This Gesture Will Set The Value Of The Current Step &
  // Will Set The Next Required Action

  if (Gesture_Near)
  {
    // ==============THIRD STEP==============

    if (StepsCompleted[1] == true && StepsCompleted[2] == false)
    {
      if (!isDateSelected)
      {
        isDateSelected = true;
        StepsCompleted[2] = true;
        InitialLoop = true;
        Serial.println("---------Date Selected---------");
      }
    }
    // ======================================

    // =============SECOND STEP==============

    if (StepsCompleted[0] == true && StepsCompleted[1] == false)
    {

      if (isYearSelected == true && isMonthSelected == false)
      {
        isMonthSelected = true;
        Serial.println("---------Month Selected----------");
      }

      if (isYearSelected == false && isMonthSelected == false)
      {
        isYearSelected = true;
        Serial.println("---------Year Selected---------");
      }

      if (isYearSelected && isMonthSelected)
      {
        StepsCompleted[1] = true;
        InitialLoop = true;
        FindMonthDays();
        Serial.println("--------Step 1 Completed---------");
      }
    }
    // =====================================

    // =============FIRST STEP==============

    if (StepsCompleted[0] == false)
    {
      if (isDurationSelected[0] == true && isDurationSelected[1] == false)
      {
        isDurationSelected[1] = true;
        Serial.println("------Duration 1 Selected-------");
      }

      if (isDurationSelected[0] == false && isDurationSelected[1] == false)
      {
        isDurationSelected[0] = true;
        Serial.println("------Duration 0 Selected-------");
      }

      if (isDurationSelected[0] && isDurationSelected[1])
      {
        StepsCompleted[0] = true;
        InitialLoop = true;
        Serial.println("--------Step 0 Completed---------");
      }
    }
    Gesture_Near = false;
  }
  // --------------------------------------

  // -------------GESTURE FAR--------------

  // This Gesture Revert To The Previous Step
  // By Changing The Boolean to False
  // Mulitple Far Gesture Will Subsequently Go Back in Steps

  if (Gesture_Far)
  {
    if (StepsCompleted[0] == false && StepsCompleted[1] == false)
    {
      // isDurationSelected[0] = false;
      isDurationSelected[1] = false;
      UpdateI2CDisplay();
      Serial.println("----Duration 1 To Be Selected----");
    }
    if (StepsCompleted[0] == true && StepsCompleted[1] == false)
    {
      // isYearSelected Has to be false Only If Year Has to Be Selected Individually
      if (isYearSelected == true && isMonthSelected == false)
      {
        StepsCompleted[0] = false;
        isDurationSelected[0] = true;
        isDurationSelected[1] = false;
        UpdateI2CDisplay();
        Serial.println("--------Revert To Step 0---------");
        Serial.println("----Duration 1 To Be Selected----");
      }
      // Uncomment the Following if Year Should Be Selected Individually
      // else if (isYearSelected == true && isMonthSelected == false)
      // {
      //   isYearSelected = false; // Only If Year Has to Be Selected Individually
      // }
    }

    if (StepsCompleted[1] == true && StepsCompleted[2] == false)
    {
      isMonthSelected = false;
      StepsCompleted[1] = false;
      UpdateI2CDisplay();
      Serial.println("--------Revert To Step 1---------");
    }
    if (StepsCompleted[2] == true && StepsCompleted[3] == false)
    {
      StepsCompleted[2] = false;
      UpdateI2CDisplay();
      Serial.println("--------Revert To Step 2---------");
    }
    Gesture_Far = false;
  }
  // --------------------------------------
}

/*
Skip Hours and Minute With Gesture Inputs, 
Accepts Skip Parameters To Skip Through Values as a Multiple, 
Both Parameters Can Be Positive or Negative
*/
void UpdateDurationHourMinute(short int HourSkip, short int MinuteSkip)
{
  String HeaderText = "Select Duration";
  // String SelectionTemplate = "0X : XX";

  // --------------HOUR--------------

  // isDurationSelected[0] should be False at first (global) inorder to edit it individually
  if (isDurationSelected[0] == false)
  {
    // HeaderText = "Select Hour";
    DurationSelected[0] += HourSkip;

    // Duration in Hours Should be Between 0 & 23
    if (DurationSelected[0] <= 0 || DurationSelected[0] >= 24)
    {
      DurationSelected[0] = 0;
    }
  }
  // --------------------------------

  // ------------MINUTES-------------

  // Checks Whether Hour is Selected
  if (isDurationSelected[1] == false && isDurationSelected[0] == true)
  {
    // HeaderText = "Select Minutes";

    int ArrayLength = sizeof(MinuteStepsArray) / sizeof(MinuteStepsArray[0]); // Array of Length 12

    // Prev Array Position Inorder to Find +ve or -ve Change
    // if Array Position Goes Out of Index (0 - 11)
    short prevMinuteArrayPosition = MonthArrayPosition;
    bool HourChange = false; // To Check Whether Hour Should be Changed if Array Position Goes Out of Index (0 - 11)

    // Change ArrayPosition According to SkipValue
    MinuteArrayPosition += MinuteSkip;

    // Checks Whether Out of Index & Loops Back To The Required Index
    if (MinuteArrayPosition < 0 || (MinuteArrayPosition > (ArrayLength - 1)))
    {
      if (MinuteArrayPosition < 0)
      {
        MinuteArrayPosition = ArrayLength + MinuteArrayPosition;
      }
      else if (MinuteArrayPosition > (ArrayLength))
      {
        MinuteArrayPosition = -(ArrayLength - MinuteArrayPosition);
      }
      else
      {
        MinuteArrayPosition = 0;
      }
      HourChange = true; // To Change Hour Since Array Position Went Out of Index

      Serial.print("MinuteArrayPosition : ");
      Serial.println(MinuteArrayPosition);
    }

    // Change Hour
    if (HourChange == true)
    {
      if (prevMinuteArrayPosition - MinuteArrayPosition < 0)
      {
        DurationSelected[0] += -1;
      }
      else if (prevMinuteArrayPosition - MinuteArrayPosition > 0)
      {
        DurationSelected[0] += 1;
      }
      HourChange = false;
    }

    // Hour Can't Be Less Than Zero
    if (DurationSelected[0] <= 0)
    {
      DurationSelected[0] = 0;
    }

    DurationSelected[1] = MinuteStepsArray[MinuteArrayPosition];
  }

  // --------------------------------

  // -------------RENDER--------------

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  // SetCursorCenterX(HeaderText, 32, 2);
  display.setCursor(18, 2);
  display.println(HeaderText);

  // display.setCursor(12, 24);
  display.setCursor(24, 16);
  display.setTextSize(2);
  display.setTextColor(WHITE);

  // Making it follow "0X" if X is single digit
  if (DurationSelected[0] < 10)
  {
    display.print(0);
  }

  display.print(DurationSelected[0]);
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.print("H");
  display.print(F(" : "));
  display.setTextSize(2);
  display.setTextColor(WHITE);

  if (DurationSelected[1] < 10)
  {
    display.print(0);
  }

  // Making it follow "0X" if X is single digit
  display.print(DurationSelected[1]);
  display.setTextSize(1);
  display.println("M");

  display.display();

  // ---------------------------------
}

/*
Skip Year and Month With Gesture Inputs, 
Accepts Skip Parameters To Skip Through Values as a Multiple, 
Both Parameters Can Be Positive or Negative
*/
void UpdateYearMonth(short int YearSkip, short int MonthSkip)
{

  String HeaderText = "Select Year & Month";
  // String SelectionTemplate = "YYYY : MON";

  // --------------YEAR--------------

  // isYearSelected should be False at first (global) inorder to edit it individually
  if (isYearSelected == false && isMonthSelected == false)
  {
    // HeaderText = "Select Hour";

    YearSelected += YearSkip;
    if (YearSelected <= year)
    {
      YearSelected = year;
    }
  }

  // --------------------------------

  // -------------MONTH--------------

  // Checks Whether Year is Selected
  if (isMonthSelected == false && isYearSelected == true)
  {
    // HeaderText = "Select Minutes";

    int ArrayLength = sizeof(MonthsArray) / sizeof(MonthsArray[0]); // Array of Length 12

    // Prev Array Position Inorder to Find +ve or -ve Change
    // if Array Position Goes Out of Index (0 - 11)
    short prevMonthArrayPosition = MonthArrayPosition;
    bool YearChange = false; // To Check Whether Hour Should be Changed if Array Position Goes Out of Index (0 - 11)

    // Change ArrayPosition According to SkipValue
    MonthArrayPosition += MonthSkip;

    // Checks Whether Out of Index & Loops Back To The Required Index
    if (MonthArrayPosition < 0 || (MonthArrayPosition > (ArrayLength - 1)))
    {
      if (MonthArrayPosition < 0)
      {
        MonthArrayPosition = ArrayLength + MonthArrayPosition;
      }
      else if (MonthArrayPosition > (ArrayLength))
      {
        MonthArrayPosition = -(ArrayLength - MonthArrayPosition);
      }
      else
      {
        MonthArrayPosition = 0;
      }
      YearChange = true; // To Change Year Since Array Position Went Out of Index
    }

    if (YearChange)
    {
      if (prevMonthArrayPosition - MonthArrayPosition < 0)
      {
        YearSelected += -1;
      }
      else if (prevMonthArrayPosition - MonthArrayPosition > 0)
      {
        YearSelected += 1;
      }
      YearChange = false;
    }

    // Year Can't Be Less Than The Current Year
    if (YearSelected <= year)
    {
      YearSelected = year;

      // Month Can't Be Less Than The Current Month If
      // The Year is the Current Year
      if (MonthArrayPosition <= month - 1)
      {
        MonthArrayPosition = month - 1;
      }
    }
    MonthSelected = MonthArrayPosition;
    Serial.print("MonthArrayPosition : ");
    Serial.println(MonthSelected);
  }
  // ---------------------------------

  // -------------RENDER--------------

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  // SetCursorCenterX(HeaderText, 32, 2);
  display.setCursor(12, 2);
  display.println(HeaderText);

  // display.setCursor(12, 24);
  display.setCursor(12, 16);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print(YearSelected);
  display.setTextSize(1);
  display.print("Y");

  display.print(F(" : "));
  display.setTextSize(2);
  display.setTextColor(WHITE);

  display.print(MonthsArray[MonthArrayPosition]);
  display.setTextSize(1);
  display.println("M");

  display.display();
  // ---------------------------------
}

/*
Skip Date With Gesture Inputs, 
Accepts Skip Parameter To Skip Through Values as a Multiple, 
Parameter Can Be Positive or Negative
*/
void UpdateDate(short int DateSkip)
{

  String HeaderText = "Date";
  // String SelectionTemplate = "0X : 0X:XX";

  // --------------DATE--------------

  // short prevDatePosition = DateSelected;
  DateSelected += DateSkip;

  // Loop Back If Array Goes Out of Index
  if (DateSelected < 1)
  {
    DateSelected = MonthDaysLeft + DateSelected;
  }
  else if (DateSelected > MonthDaysLeft)
  {
    DateSelected = DateSelected - MonthDaysLeft;
  }

  // Date Can't be Less Than The Current Date if Current Year and Month are Selected
  if ((MonthSelected <= month - 1) && (YearSelected == year) && (DateSelected <= date))
  {
    DateSelected = date;
  }
  // ---------------------------------

  // -------------RENDER--------------

  display.clearDisplay();
  display.setCursor(12, 16);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print("DATE ");

  if (DateSelected < 10)
  {
    display.print(0);
  }
  display.print(DateSelected);

  display.setTextSize(1);
  display.println(MonthsArray[MonthSelected]);

  display.display();

  // ---------------------------------
}

/*
 Finds The Number of Days When The Month is Selected, 
 & Sets as a Global Variable
*/
void FindMonthDays()
{
  if (((MonthSelected + 1) % 2) != 0)
  {
    MonthDaysLeft = 31; // Odd Months
  }
  else
  {
    if ((MonthSelected + 1) == 2)
    {
      // February
      if (YearSelected / 4 == 0)
      {
        MonthDaysLeft = 29; // Leap Year
      }
      else
      {
        MonthDaysLeft = 28; // Non - Leap
      }
    }
    else
    {
      MonthDaysLeft = 30; // Even Months
    }
  }
}

/*
Set String in I2C Display Horizontally, 
Uses getTextBounds Method.
*/
void SetCursorCenterX(String str, int x, int y)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h); //calc width of new string
  display.setCursor(x - w / 2, y);
}

/*
Set String in I2C Display Vertically & Horizontally, 
Uses getTextBounds Method.
*/
void SetCursorCenterXY(String str, int x, int y)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h); //calc width of new string
  display.setCursor(x - w / 2, y - h / 2);
}

/*
Gets Local Time Set From the NTP Server, If Time Not Set, Tries To Get Time From NTP Server.
gmtOffset_sec, daylightOffset_sec, ntpServer has to be declared as Global Variable.
Check http://www.cplusplus.com/reference/ctime/strftime/ for More Info About Parsing Time Info From Local.
*/
void ParseLocalTime()
{
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed To Obtain Time");
    // Try To Obtain Time From NTPServer Once Again
    Serial.println("---Configuring Time From NTP Server---");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(5000);
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // Serial.print("Day of week: ");
  // Serial.println(&timeinfo, "%A");
  // Serial.print("Month: ");
  // Serial.println(&timeinfo, "%B");
  // Serial.print("Day of Month: ");
  // Serial.println(&timeinfo, "%d");
  // Serial.print("Year: ");
  // Serial.println(&timeinfo, "%Y");
  // Serial.print("Hour: ");
  // Serial.println(&timeinfo, "%H");
  // Serial.print("Hour (12 hour format): ");
  // Serial.println(&timeinfo, "%I");
  // Serial.print("Minute: ");
  // Serial.println(&timeinfo, "%M");
  // Serial.print("Second: ");
  // Serial.println(&timeinfo, "%S");

  // Serial.println("Time variables");

  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  // Serial.println(timeHour);

  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  // Serial.println(timeMinute);

  // char timeWeekDay[3];
  // strftime(timeWeekDay, 3, "%w", &timeinfo); // Sunday = 0 (0 - 6)
  // // strftime(timeWeekDay, 3, "%u", &timeinfo); // Monday = 1 (1-7)
  // Serial.println(timeWeekDay);

  char timeMonth[3];
  strftime(timeMonth, 3, "%m", &timeinfo);
  // Serial.println(timeMonth);

  char timeDay[3];
  strftime(timeDay, 3, "%d", &timeinfo);
  // Serial.println(timeDay);

  char timeYear[6];
  strftime(timeYear, 6, "%Y", &timeinfo);
  // Serial.println(timeYear);

  // --------Global Variables--------

  hour = atoi(timeHour);
  minute = atoi(timeMinute);
  // second = atoi(timeSecond);
  date = atoi(timeDay);
  year = atoi(timeYear);
  month = atoi(timeMonth);

  YearSelected = year;
  MonthArrayPosition = month - 1; // (1 - 12 to 0 - 11)

  // ------Print Global Variables------
  Serial.println("------------------------");
  Serial.print("Hour : ");
  Serial.print(hour);
  Serial.print(" ");
  Serial.print("Minute : ");
  Serial.println(minute);
  // Serial.print("Second : ");
  // Serial.println(second);
  Serial.print("Month : ");
  Serial.print(month);
  Serial.print(" ");
  Serial.print("Date : ");
  Serial.print(date);
  Serial.print(" ");
  Serial.print("Year : ");
  Serial.println(year);
  // Serial.println("");
  Serial.println("------------------------");
}

/* 
Connects To The WiFi Network & Prints Local IP, 
Credentials ssid and password has to be declared Global.
*/
void setup_wifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// Displays The Bitmap Logo While The Display Starts.
void Drawbitmap(void)
{
  // // 128 * 64 I2C Display
  // const int LOGO_HEIGHT = 64;
  // const int LOGO_WIDTH = 128;
  // static const unsigned char PROGMEM logo_bmp[] =
  //     {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xfe, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xf0, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xe0, 0x00, 0xdf, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0x83, 0xf3, 0x1f, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0x07, 0xfe, 0x67, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0x0f, 0xff, 0xc3, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xfe, 0x1f, 0xff, 0x03, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xfc, 0x3f, 0xff, 0x01, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0xff,
  //      0xfc, 0x3f, 0xff, 0x01, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0xff,
  //      0xf8, 0x3f, 0xff, 0x80, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0xff,
  //      0xf8, 0x7f, 0xff, 0x80, 0xef, 0xfc, 0x1f, 0x60, 0xe0, 0xff, 0x07, 0xd8, 0x3e, 0x01, 0xc0, 0x07,
  //      0xf0, 0xce, 0x00, 0x00, 0xef, 0xf1, 0xcf, 0x0e, 0x4e, 0x7c, 0x73, 0xc3, 0x8e, 0x01, 0xff, 0xcf,
  //      0xf3, 0x18, 0x00, 0x00, 0xef, 0xe7, 0xe7, 0x3f, 0x1f, 0x3d, 0xf9, 0xcf, 0xef, 0xbf, 0xff, 0xdf,
  //      0xfc, 0x70, 0x07, 0xff, 0xef, 0xe7, 0xf7, 0x3f, 0x3f, 0xb9, 0xfd, 0xdf, 0xe7, 0xbf, 0xff, 0x9f,
  //      0xf9, 0xff, 0xff, 0xff, 0xef, 0xef, 0xf7, 0x3f, 0x3f, 0xbb, 0xfc, 0xdf, 0xe7, 0xbf, 0xff, 0x3f,
  //      0xf7, 0x7f, 0xff, 0xff, 0xef, 0xe0, 0x03, 0x7f, 0x3f, 0xb8, 0x00, 0xdf, 0xe7, 0xbf, 0xfe, 0x7f,
  //      0xfc, 0x7f, 0xff, 0xff, 0xef, 0xe0, 0x07, 0x7f, 0x3f, 0xb8, 0x01, 0xdf, 0xe7, 0xbf, 0xfc, 0xff,
  //      0xf0, 0x3f, 0xff, 0xff, 0xef, 0xef, 0xff, 0x7f, 0x3f, 0xbb, 0xff, 0xdf, 0xe7, 0xbf, 0xfd, 0xff,
  //      0xe0, 0x3f, 0xff, 0xff, 0xef, 0xef, 0xff, 0x7f, 0x3f, 0xbb, 0xff, 0xdf, 0xe7, 0xbf, 0xf9, 0xff,
  //      0xe0, 0x3f, 0xff, 0xff, 0xe7, 0xe7, 0xff, 0x7f, 0x3f, 0xb9, 0xff, 0xdf, 0xe7, 0xbf, 0xf3, 0xff,
  //      0xf0, 0x3f, 0xff, 0xff, 0xe7, 0xe7, 0xff, 0x7f, 0x3f, 0xbd, 0xff, 0xdf, 0xe7, 0x9f, 0xe7, 0xff,
  //      0xf0, 0x1f, 0xff, 0xff, 0x33, 0x71, 0xc7, 0x7f, 0x3f, 0xbc, 0x79, 0xdf, 0xe7, 0x9c, 0xcf, 0xff,
  //      0xf0, 0x1f, 0xff, 0xff, 0x38, 0x78, 0x0f, 0x7f, 0x3f, 0xbe, 0x03, 0xdf, 0xe7, 0xc1, 0xc0, 0x07,
  //      0xf0, 0x0f, 0xff, 0xfe, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xf0, 0x0f, 0xff, 0xfc, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xf8, 0x07, 0xff, 0xf8, 0x71, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xfe, 0xfb, 0x87, 0xff, 0xbf, 0xef,
  //      0xf8, 0x03, 0xff, 0xf0, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xef, 0xfe, 0xfb, 0xb7, 0xef, 0xbb, 0xef,
  //      0xfc, 0x01, 0xff, 0xe1, 0xf7, 0x0c, 0x21, 0x98, 0x81, 0xdf, 0xb4, 0xe3, 0xb5, 0x83, 0xb0, 0x8f,
  //      0xfc, 0x00, 0x7f, 0x81, 0xf1, 0x73, 0x2d, 0x6b, 0x37, 0xdc, 0xb4, 0xdb, 0x85, 0xef, 0xbb, 0x6f,
  //      0xfe, 0x00, 0x00, 0x03, 0xf7, 0x73, 0x2d, 0x08, 0x39, 0xce, 0xb4, 0xdb, 0xbd, 0x6f, 0xbb, 0x6f,
  //      0xff, 0x00, 0x00, 0x07, 0xf7, 0x73, 0x2d, 0x7b, 0xbd, 0xee, 0xb4, 0xdb, 0xbe, 0x6f, 0xbb, 0x6f,
  //      0xff, 0x80, 0x00, 0x0f, 0xf0, 0x74, 0x2d, 0x08, 0x31, 0xf0, 0x85, 0x23, 0xbe, 0xe3, 0x88, 0x0f,
  //      0xff, 0xc0, 0x00, 0x3f, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xe0, 0x00, 0x7f, 0xff, 0xf8, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xf8, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  //      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  // 128 * 32 I2C Display
  const int LOGO_HEIGHT = 32;
  const int LOGO_WIDTH = 128;
  static const unsigned char PROGMEM logo_bmp[] =
      {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xfc, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xc0, 0x1b, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0x1f, 0xe7, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xfe, 0x3f, 0xf9, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xfe, 0x3f, 0xe0, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0x7f, 0xe0, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0x7f, 0xf0, 0x6f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0xff, 0x80, 0x6f, 0xc1, 0xd0, 0xc3, 0xe0, 0xe0, 0x70, 0x30, 0x0f, 0xff, 0xff,
       0xff, 0xff, 0xfb, 0x30, 0x00, 0x6f, 0x9e, 0xcf, 0x3d, 0xde, 0x6f, 0xbd, 0xff, 0xdf, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0xff, 0xff, 0xef, 0xbe, 0xdf, 0x7d, 0x9f, 0x6f, 0xbd, 0xff, 0xbf, 0xff, 0xff,
       0xff, 0xff, 0xfb, 0x7f, 0xff, 0xef, 0xbe, 0x5f, 0x7d, 0xbf, 0x6f, 0xbd, 0xff, 0x7f, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0x7f, 0xff, 0xef, 0x80, 0xdf, 0x7d, 0x80, 0x6f, 0xbd, 0xfe, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x7f, 0xff, 0xef, 0xbf, 0xdf, 0x7d, 0xbf, 0xef, 0xbd, 0xfc, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x7f, 0xff, 0xef, 0xbf, 0xdf, 0x7d, 0x9f, 0xef, 0xbd, 0xfd, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x3f, 0xff, 0xe7, 0x9f, 0xdf, 0x7d, 0xdf, 0xef, 0xbd, 0xfb, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x3f, 0xff, 0x90, 0xc0, 0xdf, 0x7d, 0xe0, 0x6f, 0xbc, 0x30, 0x0f, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x3f, 0xff, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xf8, 0x1f, 0xff, 0x33, 0xff, 0xff, 0xff, 0x9f, 0xfb, 0x3f, 0xef, 0xef, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0x0f, 0xfe, 0x77, 0xff, 0xff, 0xff, 0x7f, 0xfb, 0x5e, 0xed, 0xef, 0xff, 0xff,
       0xff, 0xff, 0xfc, 0x07, 0xf8, 0xf6, 0x26, 0x24, 0x87, 0xf7, 0xe3, 0x4a, 0x6c, 0x8f, 0xff, 0xff,
       0xff, 0xff, 0xfe, 0x01, 0xe0, 0xf6, 0xbf, 0x84, 0x2f, 0xc7, 0xfb, 0x3a, 0xed, 0x6f, 0xff, 0xff,
       0xff, 0xff, 0xff, 0x00, 0x01, 0xf6, 0xb7, 0x9f, 0xbf, 0x65, 0xfb, 0x76, 0xed, 0x6f, 0xff, 0xff,
       0xff, 0xff, 0xff, 0x00, 0x03, 0xf0, 0xa7, 0xa4, 0xa7, 0x8b, 0x83, 0x76, 0x60, 0x9f, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xc0, 0x0f, 0xff, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xf0, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  display.clearDisplay();

  display.drawBitmap(
      (display.width() - LOGO_WIDTH) / 2,
      (display.height() - LOGO_HEIGHT) / 2,
      logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
  delay(1000);
}