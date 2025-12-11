#include <Servo.h>

const int SERVO_COUNT = 5;

Servo servos[SERVO_COUNT];
int servoPins[SERVO_COUNT] = {8, 9, 10, 11, 12};
int servoSpeed[SERVO_COUNT] = {1,3,2,1,3};
int angle0[4] = {175,110,60,140};
int angle1[3] = {25,155,80};
int angle2[2] = {75,100};
int angle3[2] = {100,70};
int angle4[1] = {0};
int currentAngle[SERVO_COUNT] = {
  angle0[0],
  angle1[2],
  angle2[0],
  angle3[0],
  angle4[0]
};
int startAngle[SERVO_COUNT] = {
  175,
  80,
  75,
  100,
  angle4[0]
};

// int currentAngle[SERVO_COUNT] = {90,25,105,90,90};
// int chargingStationState = 0;

String line;

void moveServoSmooth(int index, int target) {
  int cur = currentAngle[index];
  if (cur == target) return;

  int step = (target > cur) ? servoSpeed[index] : -servoSpeed[index];

  while (cur != target) {
    cur += step;

    if ((step > 0 && cur > target) || (step < 0 && cur < target)) {
      cur = target;
    }

    servos[index].write(cur);
    currentAngle[index] = cur;

    delay(15);
  }
}

void setup() {
  Serial.begin(115200);
    for (int i = 0; i < SERVO_COUNT; i++) {
    servos[i].attach(servoPins[i]);
      servos[i].write(startAngle[i]);
  }
    //     moveServoSmooth(0,angle0[0]);
    // moveServoSmooth(1,angle1[2]);
    // moveServoSmooth(2,angle2[0]);
    // moveServoSmooth(3,angle3[0]);
    // moveServoSmooth(4,angle4[0]);

  // servoReset();
  // replacementAction();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    // Only check the serial message start with '@CMD:'
    if (c == '\n') {
      line.trim();
      if (line.startsWith("@CMD:")) {
        String cmd = line.substring(5);
        cmd.trim();

        if (cmd == "REPLACEMENT_START") {
          Serial.println("Replacing");
          replacementAction();
        }
        else if(cmd == "SERVO_RESET"){
          Serial.println("Resetting");
          servoReset();
        }
      }
      line = "";
    } else {
      if (line.length() < 80) line += c;
      else line = "";
    }
  }
}
void servoReset(){
    moveServoSmooth(0,angle0[0]);
    moveServoSmooth(1,angle1[2]);
    moveServoSmooth(2,angle2[0]);
    moveServoSmooth(3,angle3[0]);
    moveServoSmooth(4,angle4[0]);

  // for (int i = 0; i < SERVO_COUNT; i++) {
  //   if(i!=4){
  //   servos[i].attach(servoPins[i]);
  //     servos[i].write(startAngle[i]);

  //   }
  // }
}
void replacementAction(){
  moveServoSmooth(1,angle1[2]);
  moveServoSmooth(0, angle0[1]);
  delay(50);
  moveServoSmooth(2,angle2[1]);
  delay(200);
  moveServoSmooth(3,angle3[0]);
  delay(500);
  moveServoSmooth(1,angle1[0]);
  delay(50);
  moveServoSmooth(2,angle2[0]);
  delay(200);
  moveServoSmooth(3,angle3[1]);
  delay(500);
  moveServoSmooth(2,angle2[1]);
  delay(200);
  moveServoSmooth(1,angle1[1]);
  delay(30);
  moveServoSmooth(2,angle2[0]);
  delay(200);
  moveServoSmooth(3,angle3[0]);
  delay(500);
  moveServoSmooth(2,angle2[1]);
  delay(200);
  moveServoSmooth(3,angle3[1]);
  delay(500);
  moveServoSmooth(1,angle1[2]);
  if(angle4[0] == 0){
    angle4[0] = 90;
  }else{
    angle4[0] = 0;
  }
  moveServoSmooth(4,angle4[0]);
  moveServoSmooth(0,angle0[2]);
  delay(30);
  moveServoSmooth(1,angle1[1]);
  delay(20);
  moveServoSmooth(2,angle2[0]);
  delay(200);
  moveServoSmooth(2,angle2[1]);
  delay(200);
  moveServoSmooth(1,angle1[0]);
  delay(20);
  moveServoSmooth(2,angle2[0]);
  delay(500);
  moveServoSmooth(3,angle3[0]);
  delay(500);
  moveServoSmooth(2,angle2[1]);
  moveServoSmooth(3,angle3[1]);
  delay(500);
  moveServoSmooth(1,angle1[2]);
  delay(100);
  moveServoSmooth(0,angle0[3]);
  delay(100);
  //   moveServoSmooth(2,angle2[0]);
  // moveServoSmooth(3,angle3[0]);

}