#include <Servo.h>

const int SERVO_COUNT = 5;

Servo servos[SERVO_COUNT];
int servoPins[SERVO_COUNT] = {8, 9, 10, 11, 12};

String input = "";

int servoSpeed[SERVO_COUNT] = {3,3,2,1,3};

// 각 서보의 현재 각도
int currentAngle[SERVO_COUNT] = {178, 25, 110, 70, 0};

void setup() {
  Serial.begin(9600);

  for (int i = 0; i < SERVO_COUNT; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(currentAngle[i]);
  }
}

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

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      if (input.length() >= 2) {
        int servoIndex = input.substring(0, 1).toInt();
        int angle = input.substring(1).toInt();

        if (servoIndex >= 0 && servoIndex < SERVO_COUNT && angle >= 0 && angle <= 180) {
          Serial.print("Servo ");
          Serial.print(servoIndex);
          Serial.print(" -> ");
          Serial.println(angle);

          moveServoSmooth(servoIndex, angle);
        } else {
          Serial.println("Invalid command");
        }
      }

      input = "";
    }
    else if (isDigit(c)) {
      input += c;
    }
  }
}