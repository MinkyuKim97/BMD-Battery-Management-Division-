#include <Servo.h>

const int SERVO_COUNT = 5;

Servo servos[SERVO_COUNT];
int servoPins[SERVO_COUNT] = {8, 9, 10, 11, 12};
int servoSpeed[SERVO_COUNT] = {3,3,2,1,3};
int currentAngle[SERVO_COUNT] = {178, 25, 110, 70, 0};



String line;

void setup() {
  Serial.begin(115200);  // ESP32와 반드시 동일 baud!
    for (int i = 0; i < SERVO_COUNT; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(currentAngle[i]);
  }

}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      line.trim();

      // ✅ 접두어 검사
      if (line.startsWith("@CMD:")) {
        String cmd = line.substring(5); // "@CMD:" 이후
        cmd.trim();

        if (cmd == "DO_ACTION") {
          doAction();
        }
        // 필요하면 else-if로 더 추가:
        // else if (cmd == "NEXT") ...
      }

      line = "";
    } else {
      if (line.length() < 80) line += c;  // 안전 제한
      else line = "";
    }
  }
}

void doAction(){
  Serial.println("Do Action received");
  sendToEspCmd("Do Action received");
  sendToEspCmd("ACTION_1");
}

void sendToEspCmd(const char* cmd) {
  Serial.print("@CMD:");
  Serial.println(cmd);
}
