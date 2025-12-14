// --------------------------------
// Using MG90S micro servo *4 and 
// HD2012MG 20kg torque power servo
// HD2012MG servo on 3rd pin, currently on D10
// --------------------------------
#include <Servo.h>
const int32_t SERVO_COUNT = 5;
Servo servos[SERVO_COUNT];
int32_t servoPins[SERVO_COUNT] = {8, 9, 10, 11, 12};
int32_t servoSpeed[SERVO_COUNT] = {1,3,3,3,3};
int32_t angle0[4] = {175,110,60,140};
int32_t angle1[3] = {25,155,80};
int32_t angle2[2] = {75,100};
int32_t angle3[2] = {100,70};
int32_t angle4[1] = {0};
int32_t currentAngle[SERVO_COUNT] = {
  angle0[0],
  angle1[2],
  angle2[0],
  angle3[0],
  angle4[0]
};
int32_t startAngle[SERVO_COUNT] = {
  175,
  80,
  75,
  100,
  angle4[0]
};
int32_t delayBase = 20;

String line;

// Move servo per steps, to control the speed
void moveServoSmooth(int32_t index, int32_t target) {
  int32_t cur = currentAngle[index];
  if (cur == target) return;

  int32_t step = (target > cur) ? servoSpeed[index] : -servoSpeed[index];

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
  for (int32_t i = 0; i < SERVO_COUNT; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(startAngle[i]);
  }
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      line.trim();
      // Only read 'CMD' serial
      // Because it's sharing the same debug serial line
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
}

// Replacement action
// A sequence of servo actions
void replacementAction(){
  moveServoSmooth(1,angle1[2]);
  moveServoSmooth(0, angle0[1]);
  delay(delayBase);
  moveServoSmooth(2,angle2[1]);
  delay(delayBase * 2);
  moveServoSmooth(3,angle3[1]);
  delay(delayBase * 2);
  moveServoSmooth(1,angle1[0]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[0]);
  delay(delayBase * 2);
  moveServoSmooth(2,angle2[1]);
  delay(delayBase * 2);
  moveServoSmooth(1,angle1[1]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[0]);
  delay(delayBase * 2);
  moveServoSmooth(3,angle3[0]);
  delay(delayBase * 10);
  moveServoSmooth(2,angle2[1]);
  delay(delayBase * 1);
  moveServoSmooth(3,angle3[1]);
  delay(delayBase * 2);
  moveServoSmooth(1,angle1[2]);
  
  if(angle4[0] == 0){
    angle4[0] = 90;
  }else{
    angle4[0] = 0;
  }
  moveServoSmooth(4,angle4[0]);
  moveServoSmooth(0,angle0[2]);
  delay(delayBase * 1);
  moveServoSmooth(1,angle1[1]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[0]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[1]);
  delay(delayBase * 1);
  moveServoSmooth(1,angle1[0]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[0]);
  delay(delayBase * 2);
  moveServoSmooth(3,angle3[0]);
  delay(delayBase * 10);
  moveServoSmooth(2,angle2[1]);
  moveServoSmooth(3,angle3[1]);
  delay(delayBase * 2);
  moveServoSmooth(1,angle1[2]);
  delay(delayBase * 1);
  moveServoSmooth(2,angle2[0]);
  moveServoSmooth(3,angle3[0]);
  moveServoSmooth(0,angle0[3]);
  delay(delayBase * 1);
}