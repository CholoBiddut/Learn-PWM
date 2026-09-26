void setup() {
  pinMode(9, OUTPUT);
  
}

void loop() {
  int potValue = analogRead(A0);
  int pwmValue = map(potValue, 0, 1023, 0, 255);
  analogWrite(9, pwmValue);
  delay(10);

}